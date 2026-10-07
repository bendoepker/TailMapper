#include <windows.h>
#include "packet_map.hh"

void pm_handle_message(Global *g, MSG msg);
void process_packets(Global *g);

DWORD __stdcall PM::thread(void *_g) {
    /* This is the thread that is responsible for actually
     * doing the packet mapping work. There should be absolutely
     * no unnecessary work done, and all work needs to be fast */
    TRACE("%s", "Packet mapping thread started");
    Global *g = (Global*)_g;
    if(!g)
        return 1;

    MSG msg;
    PeekMessage(&msg, 0, 0, 0, PM_NOREMOVE);
    for(;;) {
        if(g->shutdown_event) {
            TRACE("%s", "Received shutdown event");
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
        case WM_TM_ENABLE_MAP:
            /* Process the map to enable */
            /* TODO: */
            return;
        case WM_TM_DISABLE_MAP:
            /* Process the map to disable, don't throw errors, its not that deep */
            /* TODO: */
            return;
    }
}

void process_packets(Global *g) {
}
