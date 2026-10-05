#ifndef _COMMON_HH_
#define _COMMON_HH_

#include <stdint.h>
#include <string>
#include <vector>
#include <chrono>
#include <thread>
#include <map>

typedef uint8_t u8;
typedef uint16_t u16;
typedef uint32_t u32;
typedef uint64_t u64;
typedef int8_t s8;
typedef int16_t s16;
typedef int32_t s32;
typedef int64_t s64;
typedef std::vector<std::string> strvec;

using std::string;
using std::vector;
using std::map;
using sclock = std::chrono::system_clock;
using time_point = std::chrono::time_point<sclock>;
inline auto to_ms = [](auto&& ...args) {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
            std::forward<decltype(args)>(args)...
            ).count();
};
inline auto sleep_ms = [](auto arg) {
    return std::this_thread::sleep_for(std::chrono::milliseconds(arg));
};

#if !defined(__BYTE_ORDER__) || !(__BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__)
/* Only supporting little endian system because of the way IPv6 addresses are stored */
/* Its a simple change to implement but not a cost worthy one */
#error "Only little endian systems are supported"
#endif

#define U8MAX  0xff
#define U16MAX 0xffff
#define U32MAX 0xffffffff
#define U64MAX 0xffffffffffffffff

#define SUCCESS 0
#define PIPE_CREATION_ERROR -1
#define PROCESS_CREATION_ERROR -2
#define PIPE_ATTR_ERROR -3
#define EXIT_CODE_READ_FAILURE -4
#define READ_PIPE_ERROR -5
#define PROCESS_TIMEOUT -6
#define PROCESS_WAIT_ERROR -7

#define IP_PARSE_FAILED 0x80000000
#define IP_FORMAT_IPV4 0x00000001
#define IP_FORMAT_IPV6 0x00000002
#define IP_HAS_PORT 0x00000004
#define IP_HAS_PREFIX_LENGTH 0x00000008

#define BITMASK8_SUB(in, f1, f2)  (in & (U8MAX  ^ f1) | f2)
#define BITMASK16_SUB(in, f1, f2) (in & (U16MAX ^ f1) | f2)
#define BITMASK32_SUB(in, f1, f2) (in & (U32MAX ^ f1) | f2)
#define BITMASK64_SUB(in, f1, f2) (in & (U64MAX ^ f1) | f2)

#define KB(x) (1024 * x)
#define MB(x) (1024 * KB(x))

#define SafeCloseHandle(x) do { if(x && x != INVALID_HANDLE_VALUE) CloseHandle(x); x = NULL; } while(0)
#define SafeCoTaskMemFree(x) do { if(x) CoTaskMemFree(x); x = NULL; } while(0)
#define SafeFree(x) do { if(x) free(x); x = NULL; } while(0)

__attribute__((format(printf, 1, 2)))
void __error(const char* s, ...);
__attribute__((format(printf, 1, 2)))
void __print(const char* s, ...);

#if defined TM_DEBUG
# define LOG(fmt, ...) \
    __error("[LOG] " fmt, ##__VA_ARGS__)
# define ERR(fmt, ...) \
    __error("[ERROR] " fmt, ##__VA_ARGS__)
# define PRINT(fmt, ...) \
    __print("" fmt, ##__VA_ARGS__)
#else 
# define log(fmt, ...)
# define error(fmt, ...)
# define print(fmt, ...)
#endif

#ifndef _WINDOWS_
#define HANDLE void*
#define HWND struct HWND__*
#define HINSTANCE struct HINSTANCE__*
#define WM_APP 0x8000
#define DWORD unsigned long
#endif

constexpr u32 min_to_ms(u64 x) { return x * 60 * 1000; }
constexpr u32 hour_to_ms(u64 x) { return x * 60 * 1000 * 60; }

/* Messages sent from the GUI thread to the main thread */
/* Refresh the mappings displayed in the main table */
/* LPARAM and WPARAM are disregarded */
constexpr u32 WM_TM_REFRESH = WM_APP + 1;

/* Enable a subnet mapping */
/* WPARAM is set to the site id */
/* LPARAM is set to the subnet index in Global */
constexpr u32 WM_TM_ENABLE_MAP = WM_APP + 2;

/* Disable a subnet mapping */
/* WPARAM is set to the site id */
/* LPARAM is set to the subnet index in Global */
constexpr u32 WM_TM_DISABLE_MAP = WM_APP + 3;

/* A fatal error happened, crash the app */
constexpr u32 WM_TM_FATAL_ERROR = WM_APP + 4;

/* Prompt the user for tailscale login */
/* LPARAM and WPARAM are ignored */
constexpr u32 WM_TM_LOGIN_PROMPT = WM_APP + 5;

/* Tell the tailscale runner to start tailscale via `tailscale up` */
/* LPARAM and WPARAM are ignored */
constexpr u32 WM_TM_START_TAILSCALE = WM_APP + 6;

typedef struct _IP {
    u32 flags;
    u16 prefix_length;
    u16 port;
    union {
        struct {
            union {
                u8 byte[16];
                u16 word[8];
            } u;
        } ipv6;
        u32 ipv4;
    } addr;
    string str;
} IP;

typedef struct _Route {
    u16 site_id;
    IP local_ip;
    IP remote_ip;
} Route;

typedef struct _Site {
    u16 id;
    string name;
    IP ip;
    vector<Route> advertised_routes;
} Site;

typedef struct _Peer {
    string id;
    string public_key;
    string os;
    string hostname;
    string relay;
    u16 site_id;
    IP ip;
    vector<Route> routes;
    bool online;
} Peer;

enum class TailscaleState {
    InvalidState, /* Only used for an actual error state, not odd states */
    NoState,
    NeedsLogin,
    NeedsMachineAuth,
    Stopped,
    Starting,
    Running,
    InUseOtherUser,
};

/* the state of the tailscale runner thread */
enum class TailscaleRunnerState {
    InitializingTailscale,
    AwaitingTailscaleLogin,
    Running,
};

typedef struct _ShellOutput {
    u32 return_code;
    string std_out;
    string std_err;
} ShellOutput;

typedef struct _EmbeddedResource {
    const void* data;
    u64 size;
} EmbeddedResource;

typedef struct _GUIData {
    bool site_popup_open;
    u32 selected_site;
    s32 last_window_width;
    s32 last_window_height;
    EmbeddedResource font;
    map<u16, Site> *site_map;
} GUIData;

typedef struct _TailscaleData {
    time_point last_status_check;
    bool status_refresh_ongoing;
    map<u16, Site> *site_map;
    TailscaleState state;
    map<string, Peer> peer_map;
    TailscaleRunnerState runner_thread_state;
} TailscaleData;

typedef struct _Global {
    string tailscale_path;
    HWND main_window;

    /* tailscale runner thread */
    HANDLE ts_thread;
    DWORD ts_thread_id;

    /* packet mapping worker thread */
    HANDLE pm_thread;
    DWORD pm_thread_id;

    /* gui thread */
    bool gui_active;
    bool gui_sleeping;
    HANDLE gui_thread;
    HWND gui_window;

    /* this is long lived, between guis */
    GUIData gui;
    TailscaleData ts;

    /* double buffer for thread synchronizaiton */
    map<u16, Site> site_map_1;
    map<u16, Site> site_map_2;

    vector<Route> active_routes;
} Global;

string shell_error_to_string(s32 error_code);
s32 shell_exec(string program, strvec args, ShellOutput& output, s32 timeout_secs = 5);

IP parse_ip(string str);
void test_ips();
string ip_to_str(IP ip);

EmbeddedResource load_resource(s32 resource_id);

inline u16 extract_site_id(IP& ip) {
    /*
     *                 Site Id
     *                 |  IPv4 Address
     *                 |  |
     *  XX:XX:XX:XX:XX:YY:ZZ:ZZ
     *  0  2  4  6  8  10 12 14
     *   1  3  5  7  9  11 13 15
     *
     * the ip is stored in network order so we have to byte swap it
     */
    if(ip.flags & IP_FORMAT_IPV6)
        return (((u16)ip.addr.ipv6.u.byte[10]) << 8)
            + (u16)ip.addr.ipv6.u.byte[11];
    return 0;
}

inline IP local_ip(IP& ip) {
    /*
     *  local ip is only valid in IPv6 since there is
     *  no defined local-remote relationship with 4via6
     *  without the IPv6 address present
     */
#define _4VIA6_CONVERSION(x)    \
    (((u32)x[12] << 24)         \
    + ((u32)x[13] << 16)        \
    + ((u32)x[14] << 8)         \
    + ((u32)x[15]))


    if(ip.flags & IP_FORMAT_IPV6)
        return IP {
            .flags = BITMASK32_SUB(ip.flags, IP_FORMAT_IPV6, IP_FORMAT_IPV4),
            .prefix_length = (u16)(ip.prefix_length - (u16)96),
            .port = ip.port,
            .addr = {.ipv4 = _4VIA6_CONVERSION(ip.addr.ipv6.u.byte)},
        };
    return {};
#undef _4VIA6_CONVERSION
}

inline IP remote_ip(IP& ip) {
    /*
     *  remote ip is only valid if it is IPv6, otherwise there
     *  is no defined local-remote relationship with 4via6
     */
    if(ip.flags & IP_FORMAT_IPV6)
        return ip;
    return {};
}

#endif //_COMMON_HH_
