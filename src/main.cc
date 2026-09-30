#include <windivert.h>
#include <windows.h>
#include <shellapi.h>
#include "../assets/assets.h"
#include "gui.hh"

#define WM_TRAYICON (WM_USER + 1)
#define ID_TRAY_EXIT 2001
#define TRAY_ICON_ID 1
#define UI_MESSAGE_QUEUE_SIZE 1024

/* Messages sent from the GUI thread to the main thread */
/* Refresh the mappings displayed in the main table */
/* LPARAM and WPARAM are disregarded */
constexpr UINT WM_TM_REFRESH = WM_APP + 1;

/* Enable a subnet mapping */
/* WPARAM is set to the site id */
/* LPARAM is set to the subnet index in Global */
constexpr UINT WM_TM_ENABLE_MAP = WM_APP + 2;

/* Disable a subnet mapping */
/* WPARAM is set to the site id */
/* LPARAM is set to the subnet index in Global */
constexpr UINT WM_TM_DISABLE_MAP = WM_APP + 3;

HINSTANCE hInst;
NOTIFYICONDATA nid = { 0 };

LRESULT CALLBACK WndProc(HWND, UINT, WPARAM, LPARAM);
void add_tray_icon(HWND hWnd);
void remove_tray_icon();
void start_gui(Global *g);
s32 init_gui_data(Global& g);
void make_test_data(Global& g);

int main(s32 argc, char **argv, char **envp) {
    DEBUG("main()\n");
    return WinMain(
            GetModuleHandleW(nullptr),
            nullptr,
            GetCommandLineA(),
            SW_SHOWDEFAULT
            );
}

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow) {
    DEBUG("WinMain()\n");
    HANDLE instance_mutex = CreateMutexW(
            nullptr,
            FALSE,
            L"Local\\TailMapper.SingleInstance"
            );
    if (instance_mutex == nullptr)
        return 1;

    if (GetLastError() == ERROR_ALREADY_EXISTS) {
        /* We are already running in a different instance */
        CloseHandle(instance_mutex);
        return 0;
    }

    hInst = hInstance;
    Global g {};
    if(init_gui_data(g))
        return 1;
    make_test_data(g);

    DEBUG("Made test data");

    WNDCLASSEX wc = { 0 };
    wc.cbSize = sizeof(WNDCLASSEX);
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInstance;
    wc.lpszClassName = "TailMapperClass";
    RegisterClassEx(&wc);

    DEBUG("RegisterClassEx()");

    HWND hWnd = CreateWindowEx(
        WS_EX_TOOLWINDOW,
        wc.lpszClassName,
        "TailMapper",
        WS_POPUP, 
        CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT,
        NULL, NULL, hInstance, &g
    );

    DEBUG("CreateWindowEx()");

    if (!hWnd) return 0;
    g.main_thread = hWnd;

    add_tray_icon(hWnd);

    MSG msg;
    while (GetMessage(&msg, NULL, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }

    remove_tray_icon();

    CloseHandle(instance_mutex);
    return 0;
}

LRESULT CALLBACK WndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam) {
    Global* g;

    /* Set up global struct so we can see them here */
    if (message == WM_NCCREATE) {
        DEBUG("WndProc():WM_NCCREATE\n");
        auto* create = reinterpret_cast<CREATESTRUCTW*>(lParam);
        g = static_cast<Global*>(create->lpCreateParams);
        SetWindowLongPtrW(hWnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(g));
    } else {
        g = reinterpret_cast<Global*>(GetWindowLongPtrW(hWnd, GWLP_USERDATA));
    }

    switch (message) {

    case WM_TRAYICON:
        switch (LOWORD(lParam)) {

        case WM_RBUTTONUP: {
            POINT curPoint;
            GetCursorPos(&curPoint);

            HMENU hMenu = CreatePopupMenu();
            AppendMenu(hMenu, MF_STRING, ID_TRAY_EXIT, "Exit TailMapper");

            SetForegroundWindow(hWnd); 

            TrackPopupMenu(hMenu, TPM_BOTTOMALIGN | TPM_LEFTALIGN, curPoint.x, curPoint.y, 0, hWnd, NULL);
            DestroyMenu(hMenu);
            break;
        }

        case WM_LBUTTONDBLCLK:
        case WM_LBUTTONUP:
            start_gui(g);
            break;
        }
        break;

    case WM_COMMAND:
        if (LOWORD(wParam) == ID_TRAY_EXIT) {
            DestroyWindow(hWnd);
        }
        break;

    case WM_DESTROY:
        PostQuitMessage(0);
        break;

    default:
        return DefWindowProc(hWnd, message, wParam, lParam);
    }
    return 0;
}

void add_tray_icon(HWND hWnd) {
    nid.cbSize = sizeof(NOTIFYICONDATA);
    nid.hWnd = hWnd;
    nid.uID = TRAY_ICON_ID;
    nid.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
    nid.uCallbackMessage = WM_TRAYICON; // Custom message sent to WndProc

    nid.hIcon = static_cast<HICON>(
            LoadImageW(
                GetModuleHandleW(nullptr),
                MAKEINTRESOURCEW(IDI_TAILMAPPER),
                IMAGE_ICON,
                0,
                0,
                LR_DEFAULTSIZE
                )
            );

    /* hover text */
    lstrcpy(nid.szTip, "TailMapper");

    Shell_NotifyIcon(NIM_ADD, &nid);
}

void remove_tray_icon() {
    Shell_NotifyIcon(NIM_DELETE, &nid);
}

void start_gui(Global *g) {
    if(!g) return;
    if(g->gui_active) {
        if(WaitForSingleObject(g->gui_thread, 0) == WAIT_OBJECT_0) {
            /* The thread is not actually running */
            goto cont;
        }
        /* gui thread and window are already active, bring them to the front */
        if(IsIconic(g->gui_hwnd))
            ShowWindow(g->gui_hwnd, SW_RESTORE);
        SetForegroundWindow(g->gui_hwnd);
        return;
    }

cont:
    /* gui thread and window are not running, set it to active and create them */
    g->gui_active = true;
    g->gui_thread = CreateThread(NULL, 0, UI::Thread, (void*)g, 0, 0);
    if(!g->gui_thread) {
        g->gui_active = false;
        return;
    }
    printf("GUI Thread: %p\n", g->gui_thread);
}

const char* ip_test_vals2[] = {
    /* IPv4 */
    "192.168.1.1",
    "192.168.1.1:80",
    "10.0.0.1:443",
    "127.0.0.1:1",
    "192.168.21.209:5201",
    "10.20.30.40:65535",

    "192.168.1.0/24",
    "10.0.0.0/8",
    "172.16.0.0/12",
    "192.168.21.209/32",
    "0.0.0.0/0",

    /* IPv6 */
    "::1",
    "::",
    "2001:db8::1",
    "fd7a:115c:a1e0::1",
    "fe80::1234:5678",
    "2001:db8:1234:5678:9abc:def0:1234:5678",

    "[::1]:80",
    "[2001:db8::1]:443",
    "[fd7a:115c:a1e0::1234]:502",
    "[fe80::1234:5678]:65535",
    "[2001:db8:1234:5678:9abc:def0:1234:5678]:1",

    "::/0",
    "::1/128",
    "2001:db8::/32",
    "fd7a:115c:a1e0::/48",
    "fe80::/10",
    "2001:db8:1234:5678::/64",
};

s32 init_gui_data(Global& g) {
    g.gui.font = load_resource(IDR_FONT_APTOS);
    if(g.gui.font.size == 0)
        return 1;

    return 0;
}

void make_test_data(Global& g) {
    DEBUG("make_test_data()\n");
    for(u16 i = 0; i < 28; i++) {
        printf("%d", i);
        g.site_map.insert({i, Site({.id = i, .name = "Disney Land", .ip = parse_ip(ip_test_vals2[i])})});
    }
}
