#ifndef _TAILNET_HH_
#define _TAILNET_HH_

#include "common.hh"
#include <string>
#include <vector>

namespace TS {
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
    TAILSCALE_CLIENT_DOWN_ERROR,
    TAILSCALE_STARTING_ERROR,
} TailnetError;

/* Runner thread to get tailscale stuffs from */
DWORD __stdcall thread(void *);

s32 init(Global& g);

} /* namespace TS */

#endif //_TAILNET_HH_
