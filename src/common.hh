#ifndef _COMMON_HH_
#define _COMMON_HH_

#include <stdint.h>
#include <string>
#include <vector>
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

#define U16MAX 0xffff

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

#define KB(x) (1024 * x)
#define MB(x) (1024 * KB(x))

#define SafeCloseHandle(x) do { if(x && x != INVALID_HANDLE_VALUE) CloseHandle(x); x = NULL; } while(0)
#define SafeCoTaskMemFree(x) do { if(x) CoTaskMemFree(x); x = NULL; } while(0)
#define SafeFree(x) do { if(x) free(x); x = NULL; } while(0)
#define DEBUG(x) fprintf(stderr, x)

#ifndef _WINDOWS_
#define HANDLE void*
#define HWND struct HWND__*
#define HINSTANCE struct HINSTANCE__*
#endif

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

typedef struct _Site {
    u16 id;
    string name;
    IP ip;
    vector<IP> advertised_routes;
} Site;

typedef struct _Route {
    u16 site_id;
    IP ip;
} Route;

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
} GUIData;

typedef struct _Global {
    string tailscale_path;
    HWND main_thread;

    bool gui_active;
    HANDLE gui_thread;
    HWND gui_hwnd;

    /* this is long lived, between guis */
    GUIData gui;

    map<u16, Site> site_map;
    vector<Route> active_routes;
} Global;

string shell_error_to_string(s32 error_code);
s32 shell_exec(string program, strvec args, ShellOutput& output, s32 timeout_secs = 5);

IP parse_ip(string str);
void test_ips();
string ip_to_str(IP ip);

EmbeddedResource load_resource(s32 resource_id);

#endif //_COMMON_HH_
