#include <windivert.h>
#include <windows.h>
#include <shellapi.h>
#include "../assets/assets.h"
#include "gui.hh"
#include "tailnet.hh"
#include "packet_map.hh"

#define WM_TRAYICON (WM_USER + 1)
#define ID_TRAY_EXIT 2001
#define TRAY_ICON_ID 1
#define UI_MESSAGE_QUEUE_SIZE 1024

HINSTANCE hInst;
NOTIFYICONDATA nid = { 0 };

LRESULT CALLBACK WndProc(HWND, UINT, WPARAM, LPARAM);
void add_tray_icon(HWND hWnd);
void remove_tray_icon();
void start_gui(Global *g);
s32 init_gui_data(Global& g);
s32 init_global_data(Global& g);
s32 init_pm_data(Global& g);
void post_ts_message(Global& g, UINT msg, WPARAM wParam, LPARAM lParam);
void post_pm_message(Global& g, UINT msg, WPARAM wParam, LPARAM lPARAM);
LRESULT CALLBACK handle_custom_messages(Global& g, UINT msg, WPARAM wParam, LPARAM lParam);
void clean_resources(Global& g);

int main(s32 argc, char **argv, char **envp) {
    TRACE();
    return WinMain(
            GetModuleHandleW(nullptr),
            nullptr,
            GetCommandLineA(),
            SW_SHOWDEFAULT
            );
}

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow) {
    TRACE();
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
    if(init_global_data(g)
        || init_gui_data(g)
        || init_pm_data(g)
        || TS::init(g))
        return 1;

    TRACE("%s", "Data Initialized");

    WNDCLASSEX wc = { 0 };
    wc.cbSize = sizeof(WNDCLASSEX);
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInstance;
    wc.lpszClassName = "TailMapperClass";
    RegisterClassEx(&wc);

    HWND hWnd = CreateWindowEx(
        WS_EX_TOOLWINDOW,
        wc.lpszClassName,
        "TailMapper",
        WS_POPUP, 
        CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT,
        NULL, NULL, hInstance, &g
    );

    if (!hWnd) {
        ERR("Failed to initialize main window");
        return 1;
    }
    g.main_window = hWnd;

    add_tray_icon(hWnd);

    g.ts_thread = CreateThread(NULL, 0, TS::thread, (void*)&g, 0, &g.ts_thread_id);
    if(!g.ts_thread) {
        ERR("Failed to initialize TS thread");
        return 1;
    }

    TRACE("%s", "Threads Initialized");
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
        auto* create = reinterpret_cast<CREATESTRUCTW*>(lParam);
        g = static_cast<Global*>(create->lpCreateParams);
        SetWindowLongPtrW(hWnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(g));
    } else {
        g = reinterpret_cast<Global*>(GetWindowLongPtrW(hWnd, GWLP_USERDATA));
    }

    if(message > WM_APP && message < WM_APP_MAX)
        return handle_custom_messages(*g, message, wParam, lParam);

    switch (message) {

    case WM_TRAYICON:
        switch (LOWORD(lParam)) {

        case WM_RBUTTONUP: {
            POINT curPoint;
            GetCursorPos(&curPoint);

            HMENU hMenu = CreatePopupMenu();
            AppendMenu(hMenu, MF_STRING, ID_TRAY_EXIT, "Exit TailMapper");

            SetForegroundWindow(hWnd);

            auto command = TrackPopupMenu(hMenu, TPM_BOTTOMALIGN | TPM_LEFTALIGN | TPM_RETURNCMD, curPoint.x, curPoint.y, 0, hWnd, NULL);

            PostMessage(hWnd, WM_NULL, 0, 0);

            if(command == ID_TRAY_EXIT) {
                TRACE("%s", "\"Exit Tailmapper\" pressed");
                DestroyWindow(hWnd);
            }

            DestroyMenu(hMenu);
            break;
        }

        case WM_LBUTTONDBLCLK:
        case WM_LBUTTONUP:
            start_gui(g);
            break;
        }
        break;

    case WM_DESTROY:
        clean_resources(*g);
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
    nid.uCallbackMessage = WM_TRAYICON;

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
        if(IsIconic(g->gui_window))
            ShowWindow(g->gui_window, SW_RESTORE);
        SetForegroundWindow(g->gui_window);
        return;
    }

cont:
    /* gui thread and window are not running, set it to active and create them */
    g->gui_active = true;
    g->gui_thread = CreateThread(NULL, 0, UI::thread, (void*)g, 0, 0);
    if(!g->gui_thread) {
        g->gui_active = false;
        return;
    }
}

s32 init_gui_data(Global& g) {
    TRACE();
    g.gui.font = load_resource(IDR_FONT_APTOS);
    g.gui_sleeping = true;
    if(g.gui.font.size == 0)
        return 1;

    return 0;
}

s32 init_global_data(Global& g) {
    TRACE();
    g.site_map_1 = map<u16, Site>{};
    g.site_map_2 = map<u16, Site>{};
    g.shutdown_event = CreateEvent(0, 1, 0, 0);
    g.host_addrs_ready = CreateEvent(0, 1, 0, 0);
    g.config_path = get_config_path();
    g.conf = load_config(g);
    g.rmap = {};

    return 0;
}

s32 init_pm_data(Global& g) {
    TRACE();
    g.pm.message_ready = CreateEvent(0, 1, 0, 0);
    g.pm.rov.hEvent = CreateEvent(0, 1, 0, 0);
    g.pm.sov.hEvent = CreateEvent(0, 1, 0, 0);
    g.pm.working = false;
    g.pm.pbuf = (BYTE*)malloc(PACKET_BUFFER_SZ);
    g.pm.pbufsz = PACKET_BUFFER_SZ;
    g.pm.spbuf = g.pm.pbuf;     /* temp value, this is overwritten by pm_translate_[in/out]bound() */
    g.pm.spbufsz = g.pm.pbufsz; /* temp value, this is overwritten by pm_translate_[in/out]bound() */
    g.pm.opbuf = g.pm.pbuf + IP_HDR_HEADROOM;
    g.pm.opbufsz = PACKET_BUFFER_SZ - IP_HDR_HEADROOM;
    g.pm.send_pending = false;
    return 0;
}

void post_ts_message(Global& g, UINT msg, WPARAM wParam, LPARAM lParam) {
    if(g.ts_thread_id == 0) {
        TRACE("%s%x%s%llx%s%llx%s", "Attempted to post message: '", msg, "' with params: wParam(", wParam, ") lParam(", lParam, ")");
        return;
    }
    PostThreadMessage(g.ts_thread_id, msg, wParam, lParam);
}

void post_pm_message(Global& g, UINT msg, WPARAM wParam, LPARAM lParam) {
    if(g.pm_thread_id == 0 ) {
        return;
    }
    PostThreadMessage(g.pm_thread_id, msg, wParam, lParam);
    SetEvent(g.pm.message_ready);
}

LRESULT CALLBACK handle_custom_messages(Global& g, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch(msg) {
        case WM_TM_REFRESH:
            g.ts.last_refresh_sent = sclock::now();
            post_ts_message(g, msg, wParam, lParam);
            return 0;
        case WM_TM_ENABLE_MAP:
            // Deprecated
            //post_pm_message(g, msg, wParam, lParam);
            return 0;
        case WM_TM_DISABLE_MAP:
            // Deprecated
            //post_pm_message(g, msg, 0, 0);
            return 0;
        case WM_TM_FATAL_ERROR:
            ERR("%s", tme_message(wParam));
            if(wParam == TME_FATAL_ERROR_GENERIC)
                wParam = -1;
            clean_resources(g);
            PostQuitMessage(wParam);
            return wParam;
        case WM_TM_LOGIN_PROMPT:
            /* TODO: */
            return 1;
        case WM_TM_START_TAILSCALE:
            post_ts_message(g, msg, 0, 0);
            return 0;
        case WM_TM_CLOSE:
            clean_resources(g);
            PostQuitMessage(0);
            return 0;
        case WM_TM_TS_THREAD_READY:
            post_ts_message(g, WM_TM_REFRESH, 0, 0);
            return 0;
        case WM_TM_PM_THREAD_READY:
            return 0;
        case WM_TM_DISABLE_ALL_MAPS:
            /* TODO: */
            return 0;
        case WM_TM_RECALC_FILTERS:
            post_pm_message(g, WM_TM_RECALC_FILTERS, 0, 0);
            return 0;
        default:
            return 1;
    }
}

void clean_resources(Global& g) {
    /* signal the threads to close, they get 2 seconds of grace
     * period before they are forcefully closed */
    TRACE();
    SetEvent(g.shutdown_event);
    if(g.gui_thread) {
        if(WaitForSingleObject(g.gui_thread, 2000) == WAIT_TIMEOUT) {
            ERR("%s", "GUI Thread failed to close in time, forcefully closing it");
            TerminateThread(g.gui_thread, 1);
        }
        CloseHandle(g.gui_thread);
        g.gui_thread = 0;
    }
    TRACE("%s", "GUI Thread closed");
    if(g.ts_thread) {
        if(WaitForSingleObject(g.ts_thread, 2000) == WAIT_TIMEOUT) {
            ERR("%s", "Tailscale runner thread failed to close in time, forcefully closing it");
            TerminateThread(g.ts_thread, 1);
        }
        CloseHandle(g.ts_thread);
        g.ts_thread = 0;
    }
    TRACE("%s", "Tailscale runner thread closed");
    if(g.pm_thread) {
        if(WaitForSingleObject(g.pm_thread, 2000) == WAIT_TIMEOUT) {
            ERR("%s", "Packet mapping thread failed to close in time, forcefully closing it");
            TerminateThread(g.pm_thread, 1);
        }
        CloseHandle(g.pm_thread);
        g.pm_thread = 0;
    }
    TRACE("%s", "Packet mapping thread closed");
}
