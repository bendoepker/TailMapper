#include <windows.h>
#include "common.hh"
#include "tailnet.hh"
#include <shlobj.h>
#include <json.hh>

using json = nlohmann::json;
using basic_json = nlohmann::basic_json<>;
using enum TS::TailnetError;
using TS::TailnetError, TS::TailnetSite, TS::TailnetAddr;

TailnetError check_for_tailscale_executable(Global& g);
TailnetError check_tailscale_state(Global& g);
void ts_handle_message(Global *g, MSG& msg);
void ts_status_refresh(Global *g);
void ts_parse_status(Global *g, ShellOutput& so);
TailscaleState ts_parse_state(basic_json& state);
void ts_start_tailscale(Global *g);
void ts_parse_peers(Global *g, basic_json& _json);
TailnetError ts_fix_run_state(Global *g);
TailnetError ts_start_daemon(Global *g);
vector<Route> ts_parse_peer_routes(basic_json& _json);

TailnetError check_for_tailscale_executable(Global& g) {
    s32 err;
    PWSTR program_files_path = NULL;
    HRESULT hr = S_OK;
    DWORD file_attribs;
    string full_path;
    std::wstring pfp;

    hr = SHGetKnownFolderPath(FOLDERID_ProgramFiles, 0, NULL, &program_files_path);
    if (FAILED(hr))
        return SYSTEM_ERROR;

    pfp = std::wstring {program_files_path};
    SafeCoTaskMemFree(program_files_path);
    full_path = string(pfp.begin(), pfp.end());
    full_path += "\\Tailscale\\tailscale.exe";

    file_attribs = GetFileAttributesA(full_path.c_str());
    if (file_attribs == INVALID_FILE_ATTRIBUTES
        || (file_attribs & FILE_ATTRIBUTE_DIRECTORY) == FILE_ATTRIBUTE_DIRECTORY)
        err = INVALID_FILE_PATH_ERROR;

    g.tailscale_path = string(full_path);

    return OK;
}

TailnetError check_tailscale_state(Global& g) {
    /* { "BackendState": "NeedsLogin" } */
    ShellOutput so {};
    auto err = shell_exec(g.tailscale_path, {"status", "--json"}, so);
    if(err != SUCCESS) {
        if(so.std_err.find("failed to connect to local tailscaled process; is the Tailscale service running?")
                != string::npos)
            return TAILSCALE_DAEMON_DOWN_ERROR;
        return SYSTEM_ERROR;
    }
    try {
        json status = json::parse(so.std_out);
        string state;

        if (!status.contains("BackendState")
            || !(status["BackendState"].is_string()))
            return INVALID_JSON_ERROR;
        state = status["BackendState"];
        if (state == "NeedsLogin")
            return TAILSCALE_LOGGED_OUT_ERROR;
        if (state == "Stopped"
            || state == "NoState")
            return TAILSCALE_CLIENT_DOWN_ERROR;
    }
    catch (const json::parse_error& e) {
        return INVALID_JSON_ERROR;
    }
    return OK;
}

/* Perform startup checks, is tailscale installed, is account logged in? */
TailnetError ts_startup_checks(Global& g) {
    if(check_for_tailscale_executable(g) != OK) {
        /* Prompt for installation */
        /* TODO: */
    }

retest:
    if(auto res = check_tailscale_state(g); res != OK) {
        if(res == TAILSCALE_LOGGED_OUT_ERROR) {
            PostMessage(g.main_window, WM_TM_LOGIN_PROMPT, 0, 0);
            return OK;
        }
        if(res == TAILSCALE_DAEMON_DOWN_ERROR) {
            /* the daemon did not respond at all, make sure it is installed and start it */
            if(ts_start_daemon(&g) != OK)
                return SYSTEM_ERROR;
            goto retest;
        }
        if(res == TAILSCALE_CLIENT_DOWN_ERROR) {
            if(ts_fix_run_state(&g) != OK)
                return SYSTEM_ERROR;
            goto retest;
        }
    }
    return OK;
}

TailnetError ts_fix_run_state(Global *g) {
}

TailnetError ts_start_daemon(Global *g) {
    auto res = OK;
    SC_HANDLE sc_manager = OpenSCManager(0, 0, SC_MANAGER_CONNECT);
    if(!sc_manager)
        return SYSTEM_ERROR;

    auto service = OpenService(sc_manager, "Tailscale", SERVICE_START);
    if(StartService(service, 0, NULL)) {
        /* StartService is asynchronous, wait for the service to be running */
        SERVICE_STATUS_PROCESS sc_status;
        DWORD bytes_needed = 0;
        while(QueryServiceStatusEx(service, SC_STATUS_PROCESS_INFO, (LPBYTE)&sc_status, sizeof(sc_status), &bytes_needed)) {
            if(sc_status.dwCurrentState == SERVICE_START_PENDING) {
                sleep_ms(sc_status.dwCheckPoint);
                continue;
            }
            break;
        }
        if(sc_status.dwCurrentState == SERVICE_RUNNING)
            return OK;
        res = SYSTEM_ERROR;
    } else {
        res = SYSTEM_ERROR;
    }
    if(service)
        CloseServiceHandle(service);
    CloseServiceHandle(sc_manager);
    return res;
}

void ts_handle_message(Global *g, MSG& msg) {
    /* no defaulting to DefWinProc here, all messages should be custom */
    auto message = msg.message;
    auto lParam = msg.lParam;
    auto wParam = msg.wParam;
    switch(message) {
        case WM_TM_REFRESH:
            ts_status_refresh(g);
            break;
        case WM_TM_START_TAILSCALE:
            ts_start_tailscale(g);
            break;
    }
}

ULONG TS::thread(void *_g) {
    Global *g = (Global*)_g;
    if(!g)
        return 1;

    MSG msg;
    /* force message queue creation */
    PeekMessage(&msg, 0, 0, 0, PM_NOREMOVE);

    for(;;) {
        while(true) {
            /* check messages before doing any standard processin, there
             * might be some more important messages for us to handle*/
            BOOL res = PeekMessage(&msg, 0, 0, 0, PM_REMOVE);
            if(res == -1) {
                g->ts_thread = 0;
                PostMessage(g->main_window, WM_TM_FATAL_ERROR, 0, 0);
                return 1;
            }
            ts_handle_message(g, msg);
        }

        auto now = sclock::now();
        if (to_ms(now - g->ts.last_status_check) > min_to_ms(5)) {
            ts_status_refresh(g);
        }
    }

    return 0;
}

void ts_status_refresh(Global *g) {
    static auto& site_concurrency = g->site_map_1;
    if(g->ts.status_refresh_ongoing)
        return;
    g->ts.status_refresh_ongoing = true;

    ShellOutput so {};
    for(auto i = 0; i < 3; i++) {
        /* if this takes more than thirty seconds something is going very wrong,
         * either the user has extremely bad internet or tailscale is crumbling,
         * we should crash, but give it 3 tries before that, just to be friendly */
        auto res = shell_exec(g->tailscale_path, {"status", "--json"}, so, 30);
        if (res == SUCCESS && so.return_code == 0) {
            break;
        } else if((res != SUCCESS || (res == SUCCESS && so.return_code != 0)) && i < 2) {
            continue;
        }
        PostMessage(g->main_window, WM_TM_FATAL_ERROR, 0, 0);
        PostMessage(0, WM_TM_FATAL_ERROR, 0, 0);
        PostMessage(g->gui_window, WM_TM_FATAL_ERROR, 0, 0);
    }

    ts_parse_status(g, so);
    if (g->ts.state == TailscaleState::NeedsLogin)
        PostMessage(g->main_window, WM_TM_LOGIN_PROMPT, 0, 0);
    else if (g->ts.state == TailscaleState::Stopped)
        PostMessage(0, WM_TM_START_TAILSCALE, 0, 0);

    /* This is kind of dangerous for resource constrained systems,
     * but shouldn't cause problems is 99.9% of systems */
    while(!g->gui_sleeping);
    if(g->gui.site_map == &g->site_map_1) {
        g->gui.site_map = &g->site_map_2;
        g->ts.site_map = &g->site_map_1;
    } else {
        g->gui.site_map = &g->site_map_1;
        g->ts.site_map = &g->site_map_2;
    }

    g->ts.last_status_check = sclock::now();
    g->ts.status_refresh_ongoing = false;
}

s32 TS::init(Global& g) {
    g.ts.site_map = &g.site_map_1;
    g.gui.site_map = &g.site_map_2;

    return 0;
}

void ts_parse_status(Global *g, ShellOutput& so) {
    /* so is guaranteed to have succeeded with so.std_out = `tailscale status --json` output */
    try {
        auto output = json::parse(so.std_out);
        g->ts.state = ts_parse_state(output);
        ts_parse_peers(g, output);
    } catch(const json::parse_error& e) {
        /* this shouldn't be a possible route, if it does happen we just clear the output buffer */
        g->ts.site_map->clear();
        return;
    }

}

TailscaleState ts_parse_state(basic_json& _json) {
    if(_json.contains("BackendState") && _json["BackendState"].is_string()) {
        string state = _json["BackendState"];
        if      (state == "NoState")            { return TailscaleState::NoState; }
        else if (state == "NeedsLogin")         { return TailscaleState::NeedsLogin; }
        else if (state == "NeedsMachineAuth")   { return TailscaleState::NeedsMachineAuth; }
        else if (state == "Stopped")            { return TailscaleState::Stopped; }
        else if (state == "Starting")           { return TailscaleState::Starting; }
        else if (state == "Running")            { return TailscaleState::Running; }
        else if (state == "InUseOtherUser")     { return TailscaleState::InUseOtherUser; }
        else    /* Undiscovered state */        { return TailscaleState::InvalidState; }
    } else {
        return TailscaleState::InvalidState;
    }
}

void ts_parse_peers(Global *g, basic_json& _json) {
    g->ts.site_map->clear();
    auto& pm = g->ts.peer_map;
    if(!_json.contains("Peer") || _json["Peer"].is_null())
        return;
    auto peers = _json["Peer"];
    for(auto& peer : peers) {
        /*
         * Peer object (represents a device connected to the tailnet)
         *  "nodekey:..." -> top level peer identifier, equal to .PublicKey, volatile key
         *  .ID -> stable device identifier
         *  .PublicKey -> same as top level identifier
         *  .HostName -> os host name, 'archlinux' in 'user@archlinux'
         *  .OS -> the underlying os, one of [linux, windows, macos, ios, android, freebsd, openbsd, synology, qnap, unknown]
         *  .TailscaleIPs -> tailscale routes to the node itself
         *      this uses IP addresses with no CIDR or port
         *      these are stable for the lifetime of the __registation__ not just the __connection__
         *  .PrimaryRoutes -> routes which are active on this node
         *      this uses IP addresses with CIDR
         *  .AllowedIPs -> routes which CAN be active on this node
         *      this is typically just (.TailscaleIPs U .PrimaryRoutes) in set notation
         *  .Relay -> the DERP region for this node
         *      this can by used to index 'controlplane.tailscale.com/derpmap/default'
         *      for the actual region that it is relaying from
         *  .Online -> it is connected to the control plane
         *  .Active -> recently exchanged traffic with this machine
         */

        /* Required fields, if they aren't present the peer doesn't
         * get added to the map and is therefore unselectable */
        if (!peer.contains("ID") ||
                !peer.contains("PublicKey") ||
                !peer.contains("OS") ||
                !peer.contains("HostName") ||
                !peer.contains("Relay") ||
                !peer.contains("PrimaryRoutes") ||
                !peer.contains("Online") ||
                (!peer.contains("TailscaleIPs") &&
                    !peer["TailscaleIPs"].is_array() &&
                    !peer["TailscaleIPs"][0].is_string())
                )
            continue;

        Peer p = {
            .id = peer["ID"],
            .public_key = peer["PublicKey"],
            .os = peer["OS"],
            .hostname = peer["HostName"],
            .relay = peer["Relay"],
            .site_id = 0,
            .ip = parse_ip(peer["TailscaleIPs"][0]),
            .routes = peer.contains("PrimaryRoutes")
                ? ts_parse_peer_routes(peer["PrimaryRoutes"])
                : vector<Route>{},
            .online = peer["Onine"]
        };
        p.routes.size() > 0 ? p.site_id = p.routes[0].site_id : 0;
        pm.insert({p.public_key, p});
    }
}

vector<Route> ts_parse_peer_routes(basic_json& routes) {
    auto out = vector<Route>{};
    for(auto& route : routes) {
        IP ip = parse_ip(route);
        if(ip.flags & IP_PARSE_FAILED)
            continue;
        out.push_back({
                .site_id = extract_site_id(ip),
                .local_ip = local_ip(ip),
                .remote_ip = remote_ip(ip)
                });
    }
    return out;
}

void ts_start_tailscale(Global *g) {
    /* TODO: */
}
