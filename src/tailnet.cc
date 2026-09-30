#include <windows.h>
#include "tailnet.hh"
#include <shlobj.h>
#include <json.hh>
#undef SUCCESS

using json = nlohmann::json;

TailnetError check_for_tailscale_executable(Global& g);
TailnetError check_if_tailscale_logged_in(Global& g);

/* List out available sites from the tailscale command */
std::vector<TailnetSite> get_available_sites() {
    /* TODO: */
}

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

TailnetError check_if_tailscale_logged_in(Global& g) {
    /* { "BackendState": "NeedsLogin" } */
    ShellOutput so {};
    auto err = shell_exec(g.tailscale_path, {"status", "--json"}, so);
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
            return TAILSCALE_DAEMON_DOWN_ERROR;
    }
    catch (const json::parse_error& e) {
        return INVALID_JSON_ERROR;
    }
    return OK;
}

/* Perform startup checks, is tailscale installed, is account logged in? */
TailnetError tailscale_startup_checks(Global& g) {
    if(check_for_tailscale_executable(g) != OK) {
        /* Prompt for installation */
        /* TODO: */
    }

    if(check_if_tailscale_logged_in(g) != OK) {
        /* Prompt for login */
        /* TODO: */
    }
    return OK;
}

/* Show error screen for tailscale startup failure */
void tailscale_startup_failure() {
    /* TODO: */
}
