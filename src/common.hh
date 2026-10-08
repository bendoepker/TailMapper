#ifndef _COMMON_HH_
#define _COMMON_HH_

#include <cassert>
#include <chrono>
#include <cstdlib>
#include <map>
#include <stdint.h>
#include <string>
#include <thread>
#include <type_traits>
#include <vector>
#include <windivert.h>

#if !defined(__BYTE_ORDER__) || !(__BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__)
/* Only supporting little endian system because of the way IPv6 addresses are stored */
/* Its a simple change to implement but not a cost worthy one */
#error "Only little endian systems are supported"
#endif

#if defined(__GNUC__) || defined(__clang__)
#define ALWAYS_INLINE inline __attribute__((always_inline))
#elif defined(_MSC_VER)
#define ALWAYS_INLINE __forceinline
#else
#define ALWAYS_INLINE inline
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

/* tailmapper error codes (for WM_TM_FATAL_ERROR) */
#define TME_FATAL_ERROR_GENERIC 0
#define TME_NO_TAILSCALE_SERVICE 1
#define TME_WIN32_MESSAGE_QUEUE_FAILURE 2
#define TME_TAILSCALE_CLIENT_UNAVAILABLE 3
#define TME_CORRUPTED_EXE 4
#define TME_CORRUPTED_GLOBAL_VARIABLE 5
#define TME_TAILSCALE_STATUS_ERROR 6

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
# if defined TM_TRACE
#  define TRACE(fmt, ...) \
    __error("%s " __VA_OPT__(fmt), __func__ __VA_OPT__(, ##__VA_ARGS__))
# else
#  define TRACE(fmt, ...)
# endif //TM_TRACE
#else 
# define LOG(fmt, ...)
# define ERROR(fmt, ...)
# define PRINT(fmt, ...)
# define TRACE(fmt, ...)
#endif

#ifndef _WINDOWS_
#define HANDLE void*
#define HWND struct HWND__*
#define HINSTANCE struct HINSTANCE__*
#define WM_APP 0x8000
#define DWORD unsigned long
#endif
#define WM_APP_MAX 0xbfff

typedef uint8_t u8;
typedef uint16_t u16;
typedef uint32_t u32;
typedef uint64_t u64;
typedef int8_t s8;
typedef int16_t s16;
typedef int32_t s32;
typedef int64_t s64;

using std::atomic;
using std::map;
using std::pair;
using std::string;
using std::vector;
using sclock = std::chrono::system_clock;
using time_point = std::chrono::time_point<sclock>;
ALWAYS_INLINE auto to_ms(auto&& ...args) {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
            std::forward<decltype(args)>(args)...
            ).count();
};
ALWAYS_INLINE auto sleep_ms(auto arg) {
    return std::this_thread::sleep_for(std::chrono::milliseconds(arg));
};
template <typename T>
ALWAYS_INLINE string wcstoas(T arg) {
    if constexpr (std::is_same_v<T, wchar_t*>) {
        auto n = wcstombs(0, arg, 0);
        string out(n, 0);
        wcstombs(out.data(), arg, n);
        return out;
    } else if constexpr (std::is_same_v<T, std::wstring>) {
        if(arg.empty())
            return {};
        auto n = wcstombs(0, arg.c_str(), 0);
        string out(n, 0);
        wcstombs(out.data(), arg.c_str(), n);
        return out;
    } else {
        static_assert(1, "Passed non-wide character string type to wcstoas");
    }
};

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
/* WPARAM is the error code */
/* LPARAM is ignored */
constexpr u32 WM_TM_FATAL_ERROR = WM_APP + 4;

/* Prompt the user for tailscale login */
/* LPARAM and WPARAM are ignored */
constexpr u32 WM_TM_LOGIN_PROMPT = WM_APP + 5;

/* Tell the tailscale runner to start tailscale via `tailscale up` */
/* LPARAM and WPARAM are ignored */
constexpr u32 WM_TM_START_TAILSCALE = WM_APP + 6;

/* Tell the application to quit */
constexpr u32 WM_TM_CLOSE = WM_APP + 7;

/* Tell the main thread that the runner thread is ready */
constexpr u32 WM_TM_TS_THREAD_READY = WM_APP + 8;

/* Tell the main thread that the packet mapping thread is ready */
constexpr u32 WM_TM_PM_THREAD_READY = WM_APP + 9;

/* Disable all packet maps */
/* LPARAM and WPARAM are ignored */
constexpr u32 WM_TM_DISABLE_ALL_MAPS = WM_APP + 10;

/* Tell the packet mapping thread to recalculate it's filters */
constexpr u32 WM_TM_RECALC_FILTERS = WM_APP + 11;

typedef enum _IPVersion { V4, V6 } IPVersion;

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

/* This is probably unnecessary unless I want to reinvent windiver */
/* hmmm... that sounds fun though */
// class IPRange {
// public:
//     IPRange() = delete;
//     IPRange(IP ip);
//     bool match(u8* bytes, IPVersion ver);
// private:
//     IPVersion version;
//     u8 _bytes[16];      /* Base IP */
//     u8 _filter[16];     /* CIDR Mask */
//     bool (*_match)(u8 *bytes, u8 *_bytes, u8 *filter);
// };

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

class sIP {
public:
    /* simplified IP structure */
    /* no invalid ips allowed here */
    sIP() = delete;
    sIP(IP& ip) {
        assert(!(ip.flags & IP_PARSE_FAILED) && "Invalid IP sent to _sIP()");
        if((ip.flags & IP_FORMAT_IPV4) == IP_FORMAT_IPV4) {
            this->addr[0] = (ip.addr.ipv4 & 0xff000000) >> 24;
            this->addr[1] = (ip.addr.ipv4 & 0x00ff0000) >> 16;
            this->addr[2] = (ip.addr.ipv4 & 0x0000ff00) >> 8;
            this->addr[3] = (ip.addr.ipv4 & 0x000000ff);
            s32 tmp = ip.prefix_length;
            if(tmp > 32) tmp = 32;

            memset(this->mask, 0, 16);
            for(auto i = 0; i < 4; i++) {
                /* translate the cidr suffix to a bitmask */
                if(tmp >= 8) {
                    this->mask[i] = 0xff;
                    tmp -= 8;
                } else {
                    this->mask[i] = (u8)(0xff << (8 - tmp));
                    tmp = 0;
                }
            }

            this->_match = [](u8 *tb, u8 *rb, u8 *filter) {
                return (*((u32*)tb) & *((u32*)filter))
                    == (*((u32*)rb) & *((u32*)filter));
            };

            this->version = 4;

        } else {
            for(auto i = 0; i < 16; i++)
                this->addr[i] = ip.addr.ipv6.u.byte[i];
            s32 tmp = ip.prefix_length;
            if(tmp > 128) tmp = 128;

            memset(this->mask, 0, 16);
            for(auto i = 0; i < 16; i++) {
                /* translate the cidr suffix to a bitmask */
                if(tmp >= 8) {
                    this->mask[i] = 0xff;
                    tmp -= 8;
                } else {
                    this->mask[i] = (u8)(0xff << (8 - tmp));
                    tmp = 0;
                }
            }

            this->_match = [](u8 *tb, u8 *rb, u8 *filter) {
                return (
                    (((u64*)tb)[0] & ((u64*)filter)[0])
                        == (((u64*)rb)[0] & ((u64*)filter)[0])
                    && (((u64*)tb)[1] & ((u64*)filter)[1])
                        == (((u64*)rb)[1] & ((u64*)filter)[1])
                );
            };

            this->version = 6;
        }
    }

    string min_ip() {
        u8 out[16] = {};
        auto i_max = this->version == 4 ? 4 : 16;
        for(auto i = 0; i < i_max; i++)
            out[i] = (this->addr[i] & this->mask[i]);
        if(this->version == 4) {
            return std::format("{}.{}.{}.{}", out[0], out[1], out[2], out[3]);
        } else {
            return std::format("{:02x}{:02x}:{:02x}{:02x}:{:02x}{:02x}:{:02x}{:02x}"
                               "{:02x}{:02x}:{:02x}{:02x}:{:02x}{:02x}:{:02x}{:02x}",
                        out[0], out[1], out[2], out[3], out[4], out[5], out[6], out[7],
                        out[8], out[9], out[10], out[11], out[12], out[13], out[14], out[15]
                    );
        }

    }

    string max_ip() {
        u8 out[16] = {};
        auto i_max = this->version == 4 ? 4 : 16;
        for(auto i = 0; i < i_max; i++)
            out[i] = (this->addr[i] & this->mask[i]) | (this->mask[i] ^ 0xff);
        if(this->version == 4) {
            return std::format("{}.{}.{}.{}", out[0], out[1], out[2], out[3]);
        } else {
            return std::format("{:02x}{:02x}:{:02x}{:02x}:{:02x}{:02x}:{:02x}{:02x}"
                               "{:02x}{:02x}:{:02x}{:02x}:{:02x}{:02x}:{:02x}{:02x}",
                        out[0], out[1], out[2], out[3], out[4], out[5], out[6], out[7],
                        out[8], out[9], out[10], out[11], out[12], out[13], out[14], out[15]
                    );
        }
    }

    bool operator==(const sIP other) {
        if(this->version != other.version)
            return false;

        if(this->version == 4) {
            for(auto i = 0; i < 4; i++) {
                if((this->addr[i] != other.addr[i])
                    || (this->mask[i] != other.mask[i]))
                    return false;
            }
        } else {
            for(auto i = 0; i < 16; i++) {
                if((this->addr[i] != other.addr[i])
                    || (this->mask[i] != other.mask[i]))
                    return false;
            }
        }
        return true;
    }

    string str() const {
        auto out = this->addr;
        if(this->version == 4) {
            return std::format("IPV{} {}.{}.{}.{}", this->version, out[0], out[1], out[2], out[3]);
        } else {
            return std::format("IPV{} {:02x}{:02x}:{:02x}{:02x}:{:02x}{:02x}:{:02x}{:02x}"
                               "{:02x}{:02x}:{:02x}{:02x}:{:02x}{:02x}:{:02x}{:02x}", this->version,
                        out[0], out[1], out[2], out[3], out[4], out[5], out[6], out[7],
                        out[8], out[9], out[10], out[11], out[12], out[13], out[14], out[15]
                    );
        }
    }

    bool match(u8 *bytes, u32 version) {
        if(version != this->version)
            return false;
        return this->_match(bytes, this->addr, this->mask);
    }

    u32 version;
    u8 addr[16];
    u8 mask[16];
    bool (*_match)(u8 *tb, u8 *rb, u8 *filter);
};

/* This is really annoying, but once again windows has poluted
 * the global namespace... really though, shame on you windows
 * devs for windows.h */
namespace Common {
typedef struct _IPAddr {
    u32 version;
    u8 addr[16];
} IPAddr;
}

class RouteMap {
public:
    /*
     *      192.168.10.0/24 <-> fd7a:115c:a1e0:b1a:0:1:c0a8:0a00/120
     *
     *      RouteMap rm {{}};
     *      auto addr = rm.local[fd7a:115c:a1e0:b1a:0:1:c0a8:0a21]
     *      // addr == 192.168.10.21
     *      auto addr = rm.remote[192.168.10.21]
     *      // addr == fd7a:115c:a1e0:b1a:0:1:c0a8:0a21
     */
    RouteMap() = default;
    void add_route(Route& r);
    void remove_route(Route& r);
    bool route_in_map(Route& r);
    string get_outbound_filter();
    string get_inbound_filter();

    /* Get the IPv4 equivalent to an IPv6 addr */
    Common::IPAddr get_inbound_mapping(u8 *addr);

    /* Get the IPv6 equivalent to an IPv4 addr */
    Common::IPAddr get_outbound_mapping(u8 *addr);

    /* .first == local IP && .second == remote IP */
    vector<pair<sIP, sIP>> _routes;
};

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
    time_point last_refresh_sent;
    atomic<bool> status_refresh_ongoing;
    map<u16, Site> *site_map;
    TailscaleState state;
    map<string, Peer> peer_map;
    TailscaleRunnerState runner_thread_state;
} TailscaleData;

typedef struct _PacketMapData {
    HANDLE wd_handle;
    atomic<bool> working;
    string filter;
    vector<string> inbound_rules, outbound_rules;

    /* working data fields */
    OVERLAPPED ov;
    WINDIVERT_ADDRESS addr;
    UINT packet_len;
} PacketMapData;

typedef struct _Config {
    string control_plane;
    bool unattended;
} Config;

typedef struct _Global {
    string tailscale_path;
    vector<string> tailscale_up_params;
    string data_dir;
    string config_path;

    HWND main_window;
    atomic<bool> shutdown_event;

    /* tailscale runner thread */
    HANDLE ts_thread;
    DWORD ts_thread_id;

    /* packet mapping worker thread */
    HANDLE pm_thread;
    DWORD pm_thread_id;

    /* gui thread */
    atomic<bool> gui_active;
    atomic<bool> gui_sleeping;
    HANDLE gui_thread;
    HWND gui_window;

    /* long lived data, regardless of GUI state */
    GUIData gui;
    TailscaleData ts;
    PacketMapData pm;

    /* double buffer for thread synchronizaiton */
    map<u16, Site> site_map_1;
    map<u16, Site> site_map_2;

    RouteMap rmap;

    Config conf;
} Global;

string shell_error_to_string(s32 error_code);
s32 shell_exec(string program, vector<string> args, ShellOutput& output, s32 timeout_secs = 5);

IP parse_ip(string str);
void test_ips();
string ip_to_str(IP ip);

EmbeddedResource load_resource(s32 resource_id);

string read_entire_file(string absolute_path);
string get_config_path();

Config load_config(Global& g);

ALWAYS_INLINE u16 extract_site_id(IP& ip) {
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

ALWAYS_INLINE IP local_ip(IP& ip) {
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

ALWAYS_INLINE IP remote_ip(IP& ip) {
    /*
     *  remote ip is only valid if it is IPv6, otherwise there
     *  is no defined local-remote relationship with 4via6
     */
    if(ip.flags & IP_FORMAT_IPV6)
        return ip;
    return {};
}

ALWAYS_INLINE const char *tme_message(u64 tme) {
    switch(tme) {
        case TME_FATAL_ERROR_GENERIC: return "Generic Fatal Error Occurred";
        case TME_NO_TAILSCALE_SERVICE: return "The tailscale service was not found";
        case TME_WIN32_MESSAGE_QUEUE_FAILURE: return "The Win32 message queue failed to initialize";
        case TME_TAILSCALE_CLIENT_UNAVAILABLE: return "The tailscale client was not found";
        case TME_CORRUPTED_EXE: return "This executable is corrupted, reinstall to fix the error";
        case TME_CORRUPTED_GLOBAL_VARIABLE: return "The global state was corrupted";
        case TME_TAILSCALE_STATUS_ERROR: return "The tailscale status has produced an error";
        default: return "An undefined fatal error has occurred";
    }
}

inline void print_ip(IP ip) {
    if(ip.flags & IP_PARSE_FAILED) {
        printf("Invalid IP\n");
    } else if(ip.flags & IP_FORMAT_IPV4) {
        printf("Address: %d.%d.%d.%d\nPort: %d\nCIDR: %d\n",
                ((ip.addr.ipv4 & 0xff000000) >> 24),
                ((ip.addr.ipv4 & 0x00ff0000) >> 16),
                ((ip.addr.ipv4 & 0x0000ff00) >> 8),
                ((ip.addr.ipv4 & 0x000000ff)),
                (ip.flags & IP_HAS_PORT) ? ip.port : 0,
                (ip.flags & IP_HAS_PREFIX_LENGTH) ? ip.prefix_length : 0
                );
    } else {
        printf("Address: %x%x:%x%x:%x%x:%x%x:%x%x:%x%x:%x%x:%x%x\nPort: %d\nCIDR: %d\n",
                ip.addr.ipv6.u.byte[0],
                ip.addr.ipv6.u.byte[1],
                ip.addr.ipv6.u.byte[2],
                ip.addr.ipv6.u.byte[3],
                ip.addr.ipv6.u.byte[4],
                ip.addr.ipv6.u.byte[5],
                ip.addr.ipv6.u.byte[6],
                ip.addr.ipv6.u.byte[7],
                ip.addr.ipv6.u.byte[8],
                ip.addr.ipv6.u.byte[9],
                ip.addr.ipv6.u.byte[10],
                ip.addr.ipv6.u.byte[11],
                ip.addr.ipv6.u.byte[12],
                ip.addr.ipv6.u.byte[13],
                ip.addr.ipv6.u.byte[14],
                ip.addr.ipv6.u.byte[15],
                (ip.flags & IP_HAS_PORT) ? ip.port : 0,
                (ip.flags & IP_HAS_PREFIX_LENGTH) ? ip.prefix_length : 0
                );
    }
}

inline string join_strs(string joiner, vector<string>strs) {
    if(strs.size() == 0)
        return "";
    if(strs.size() == 1)
        return strs[0];
    string out = strs[0];
    for(auto i = 1; i < strs.size(); i++) {
        out += (joiner + strs[i]);
    }
    return out;
}


#endif //_COMMON_HH_
