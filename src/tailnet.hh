#ifndef _TAILNET_HH_
#define _TAILNET_HH_

#include "common.hh"
#include <string>
#include <vector>

typedef struct _TailnetAddr {
    IP tailnet_addr;
    IP local_addr;
} TailnetAddr;

typedef struct _TailnetSite {
    std::vector<TailnetAddr> routes;
    u16 site_id;
    std::string site_friendly_name; // Unused for the time being
} TailnetSite;

typedef enum _TailnetError {
    OK = 0,
    SYSTEM_ERROR,
    INVALID_FILE_PATH_ERROR,
    INVALID_JSON_ERROR,
    TAILSCALE_LOGGED_OUT_ERROR,
    TAILSCALE_DAEMON_DOWN_ERROR,
} TailnetError;

/* List out available sites from the tailscale command */
std::vector<TailnetSite> get_available_sites();

/* Perform startup checks, is tailscale on path, is account logged in? */
TailnetError tailscale_startup_checks(Global& g);

/* Show error screen for tailscale startup failure */
void tailscale_startup_failure();

#endif //_TAILNET_HH_
