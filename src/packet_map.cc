#include <windows.h>
#include "packet_map.hh"
#include "windivert.h"

void pm_handle_message(Global *g, MSG msg);
void process_packets(Global *g);
void pm_recalc_filters(Global *g);
void pm_shutdown(Global *g);
void pm_create_filter(Global *g);

DWORD __stdcall PM::thread(void *_g) {
    /* This is the thread that is responsible for actually
     * doing the packet mapping work. There should be absolutely
     * no unnecessary work done, and all work needs to be fast */
    TRACE("%s", "Packet mapping thread started");
    Global *g = (Global*)_g;
    if(!g)
        return 1;

    g->pm.ov.hEvent = CreateEvent(0, 1, 0, 0);

    MSG msg;
    PeekMessage(&msg, 0, 0, 0, PM_NOREMOVE);

    PostMessage(g->main_window, WM_TM_PM_THREAD_READY, 0, 0);

    for(;;) {
        if(g->shutdown_event) {
            TRACE("%s", "Received shutdown event");
            pm_shutdown(g);
            return 0;
        }
        while(PeekMessage(&msg, 0, 0, 0, PM_REMOVE)) {
            pm_handle_message(g, msg);
        }
        process_packets(g);
    }
}

void pm_handle_message(Global *g, MSG msg) {
    auto& pm = g->pm;
    switch(msg.message) {
        case WM_TM_RECALC_FILTERS:
            pm_recalc_filters(g);
            return;
    }
}

void pm_recalc_filters(Global *g) {
    /* Map to min/max ip range and add to filter */
    /* Reopen windivert handle when not processing packets */
    WinDivertClose(g->pm.wd_handle);
    g->pm.wd_handle = 0;

    /* create a new filter for the new route */
    pm_create_filter(g);

    /* Open a new handle with the new mappings */
    HANDLE h = WinDivertOpen(g->pm.filter.c_str(), WINDIVERT_LAYER_NETWORK, 0, 0);

    g->pm.wd_handle = h;
}

void process_packets(Global *g) {
    if(!g->pm.wd_handle) {
        return;
    }

    g->pm.working = true;

    g->pm.working = false;
}

void pm_create_filter(Global *g) {
    auto ibf = g->rmap.get_inbound_filter();
    auto obf = g->rmap.get_outbound_filter();
    g->pm.filter = "(inbound and (" + ibf + ")) or (outbound and (" + obf + "))";
    PRINT("WinDivert filter updated: %s", g->pm.filter.c_str());
}
