#include <windows.h>
#include "common.hh"
#include "tailnet.hh"
#include <shlobj.h>
#include <json.hh>

#define REFRESH_INTERVAL_MINUTES 1

using json = nlohmann::json;
using basic_json = nlohmann::basic_json<>;
using enum TS::TailnetError;
using TS::TailnetError, TS::TailnetSite, TS::TailnetAddr;
using enum TailscaleRunnerState;

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
TailscaleState ts_status_state_only(Global *g);
TailnetError ts_startup_checks(Global& g);

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
    TRACE();
    if(check_for_tailscale_executable(g) != OK) {
        /* Prompt for installation */
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

    TRACE("%s", "Tailscale Startup Checks Passed");
    return OK;
}

TailnetError ts_fix_run_state(Global *g) {
    TRACE();
    /* Stopped / NoState */
    /* Could be the client down, could be the service down, check both */
    ts_start_daemon(g);
    ts_start_tailscale(g);

    return OK;
}

TailnetError ts_start_daemon(Global *g) {
    TRACE();
    auto res = OK;
    SC_HANDLE sc_manager = OpenSCManager(0, 0, SC_MANAGER_CONNECT);
    if(!sc_manager)
        return SYSTEM_ERROR;

    auto service = OpenService(sc_manager, "Tailscale", SERVICE_START);
    if(!service) {
        PostMessage(g->main_window, WM_TM_FATAL_ERROR, TME_NO_TAILSCALE_SERVICE, 0);
    }
    {

        /* Check if the service is already up */
        SERVICE_STATUS_PROCESS sc_status;
        DWORD bytes_needed = 0;
        if(QueryServiceStatusEx(service, SC_STATUS_PROCESS_INFO, (LPBYTE)&sc_status, sizeof(sc_status), &bytes_needed)) {
            if(sc_status.dwCurrentState == SERVICE_RUNNING) {
                CloseServiceHandle(service);
                CloseServiceHandle(sc_manager);
                return OK;
            }
        }
    }
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
            TRACE("%s", "WM_TM_REFRESH");
            ts_status_refresh(g);
            break;
        case WM_TM_START_TAILSCALE:
            TRACE("%s", "WM_TM_START_TAILSCALE");
            ts_start_tailscale(g);
            break;
    }
}

DWORD WINAPI TS::thread(void *_g) {
    TRACE("%s", "Tailscale thread started");
    Global *g = (Global*)_g;
    if(!g)
        return 1;

    MSG msg;
    /* force message queue creation */
    PeekMessage(&msg, 0, 0, 0, PM_NOREMOVE);
    g->ts.last_status_check = sclock::now();
    PostMessage(g->main_window, WM_TM_TS_THREAD_READY, 0, 0);

    for(;;) {
        if(g->shutdown_event) {
            TRACE("%s", "Received shutdown event");
            return 0;
        }
        while(true) {
            /* check messages before doing any standard processin, there
             * might be some more important messages for us to handle*/
            BOOL res = PeekMessage(&msg, 0, 0, 0, PM_REMOVE);
            if(!res)
                break;
            ts_handle_message(g, msg);
        }

        auto now = sclock::now();
        if (to_ms(now - g->ts.last_status_check) > min_to_ms(REFRESH_INTERVAL_MINUTES)) {
            ts_status_refresh(g);
        }
        sleep_ms(1);
    }

    return 0;
}

void ts_status_refresh(Global *g) {
    static auto& site_concurrency = g->site_map_1;
    if(g->ts.status_refresh_ongoing)
        return;
    g->ts.status_refresh_ongoing = true;
    TRACE("%s", "Refresh started");

    ShellOutput so {};
    for(auto i = 0; i < 3; i++) {
        TRACE("Retry %d", i);
        /* if this takes more than thirty seconds something is going very wrong,
         * either the user has extremely bad internet or tailscale is crumbling,
         * we should crash, but give it 3 tries before that, just to be friendly */
        auto res = shell_exec(g->tailscale_path, {"status", "--json"}, so, 30);
        if (res == SUCCESS && so.return_code == 0) {
            break;
        } else if((res != SUCCESS || (res == SUCCESS && so.return_code != 0)) && i < 2) {
            continue;
        }
        PostMessage(g->main_window, WM_TM_FATAL_ERROR, TME_TAILSCALE_CLIENT_UNAVAILABLE, 0);
        return;
    }

    ts_parse_status(g, so);
    if (g->ts.state == TailscaleState::NeedsLogin)
        PostMessage(g->main_window, WM_TM_LOGIN_PROMPT, 0, 0);
    else if (g->ts.state == TailscaleState::Stopped)
        PostMessage(0, WM_TM_START_TAILSCALE, 0, 0);

    /* This is kind of dangerous for resource constrained systems,
     * but shouldn't cause problems is 99.9% of systems */
    while(!g->gui_sleeping);
    PRINT("TS Site Count: %llu", g->ts.site_map->size());
    PRINT("GUI Site Count: %llu", g->gui.site_map->size());
    if(g->gui.site_map == &g->site_map_1) {
        g->gui.site_map = &g->site_map_2;
        g->ts.site_map = &g->site_map_1;
    } else {
        g->gui.site_map = &g->site_map_1;
        g->ts.site_map = &g->site_map_2;
    }
    PRINT("TS Site Count: %llu", g->ts.site_map->size());
    PRINT("GUI Site Count: %llu", g->gui.site_map->size());

    g->ts.last_status_check = sclock::now();
    TRACE("Refresh complete at %s", std::format("{:%F %T}", sclock::now()).c_str());
    g->ts.status_refresh_ongoing = false;
}

s32 TS::init(Global& g) {
    TRACE("%s", "Tailscale runner init");
    g.ts.site_map = &g.site_map_1;
    g.gui.site_map = &g.site_map_2;
    g.ts.runner_thread_state = InitializingTailscale;

    g.tailscale_up_params = {
        "up",
        "--login-server=" + g.conf.control_plane,
        g.conf.unattended ? "--unattended" : "--unattended=false",
    };
    if(auto res = ts_startup_checks(g); res != OK)
        return res;

    return 0;
}

void ts_parse_status(Global *g, ShellOutput& so) {
    TRACE();
    /* so is guaranteed to have succeeded with so.std_out = `tailscale status --json` output */
    try {
        auto output = json::parse(so.std_out);
        g->ts.state = ts_parse_state(output);
        if(g->ts.state == TailscaleState::Running)
            ts_parse_peers(g, output);
        else {
            ts_fix_run_state(g);
        }
    } catch(const json::parse_error& e) {
        /* this shouldn't be a possible route, if it does happen we just clear the output buffer */
        g->ts.site_map->clear();
        return;
    }
}

TailscaleState ts_parse_state(basic_json& _json) {
    TRACE();
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
    TRACE();

    try {
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
                    !peer.contains("Online") || !peer["Online"].is_boolean() ||
                    (!peer.contains("TailscaleIPs") &&
                        !peer["TailscaleIPs"].is_array() &&
                        !peer["TailscaleIPs"][0].is_string())
                    )
                continue;

            Peer p = {};
            p.id = peer["ID"];
            p.public_key = peer["PublicKey"];
            p.os = peer["OS"];
            p.hostname = peer["HostName"];
            p.relay = peer["Relay"];
            p.site_id = 0;
            p.ip = parse_ip(peer["TailscaleIPs"][0]);
            p.routes = ts_parse_peer_routes(peer["PrimaryRoutes"]);
            p.online = peer["Online"];
            p.routes.size() > 0 ? p.site_id = p.routes[0].site_id : 0;
            pm.insert({p.public_key, p});
        }

        for(auto& [k, v] : pm) {
            for(auto& route : v.routes) {
                if(g->ts.site_map->contains(route.site_id)) {
                    g->ts.site_map->at(route.site_id).advertised_routes.push_back(route);
                } else {
                    g->ts.site_map->insert(
                            {route.site_id,
                                {.id = route.site_id,
                                .name = "", /* TODO: Get name assignments from a shared source */
                                .ip = v.ip,
                                .advertised_routes = {route}}
                            });
                }
            }
        }

#define IPCSTR(x) ip_to_str(x).c_str()
        for(auto& [k, v] : *g->ts.site_map) {
            PRINT("Site Id: %u", k);
            PRINT("\tName: %s", v.name.c_str());
            PRINT("\tMain IP: %s", ip_to_str(v.ip).c_str());
            PRINT("\tRoutes:");
            for(auto& r : v.advertised_routes) {
                PRINT("\t> %s | %s", IPCSTR(r.local_ip), IPCSTR(r.remote_ip));
            }
        }
#undef IPCSTR

    } catch(json::parse_error& e) {
        PostMessage(g->main_window, WM_TM_FATAL_ERROR, TME_TAILSCALE_STATUS_ERROR, 0);
    } catch(json::type_error& e) {
        PostMessage(g->main_window, WM_TM_FATAL_ERROR, TME_TAILSCALE_STATUS_ERROR, 0);
    }
    TRACE("%s", "Parsed all peers");
}

vector<Route> ts_parse_peer_routes(basic_json& routes) {
    TRACE();
    auto out = vector<Route>{};
    for(auto& route : routes) {
        IP ip = parse_ip(route);
        if((ip.flags & IP_PARSE_FAILED) == IP_PARSE_FAILED)
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
    ShellOutput so {};
    if(shell_exec(g->tailscale_path, g->tailscale_up_params, so, 10) || so.return_code != 0) {
        PostMessage(g->main_window, WM_TM_FATAL_ERROR, TME_TAILSCALE_CLIENT_UNAVAILABLE, 0);
        return;
    }
    g->ts.state = ts_status_state_only(g);
}

TailscaleState ts_status_state_only(Global *g) {
    try {
        ShellOutput so {};
        if(shell_exec(g->tailscale_path, {"status", "--json"}, so))
            throw std::exception();
        auto output = json::parse(so.std_out);
        return ts_parse_state(output);
    } catch(std::exception) {
        /* this shouldn't be a possible route, if it does happen we just clear the output buffer */
        return TailscaleState::InvalidState;
    }
}
