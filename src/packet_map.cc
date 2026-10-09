#include <windows.h>
#include "packet_map.hh"
#include "windivert.h"

void pm_handle_message(Global *g, MSG msg);
void pm_process_packets(Global *g);
void pm_recalc_filters(Global *g);
void pm_shutdown(Global *g);
void pm_create_filter(Global *g);
void pm_process_remaining_packets(Global *g);
void pm_process_single_packet(Global *g);
void pm_send_packet(Global *g);
void pm_translate_inbound(Global *g);
void pm_translate_outbound(Global *g);
WINDIVERT_IPHDR *pm_calc_ipv4_header_pos(WINDIVERT_IPV6HDR *ipv6);
WINDIVERT_IPV6HDR *pm_calc_ipv6_header_pos(WINDIVERT_IPHDR *ipv4);

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
    WaitForSingleObject(g->host_addrs_ready, INFINITE);

    PostMessage(g->main_window, WM_TM_PM_THREAD_READY, 0, 0);

#define SHUTDOWN_EVENT_IDX 0
#define MESSAGE_EVENT_IDX 1
#define ROV_EVENT_IDX 2
    for(;;) {
        HANDLE events[] = {
            g->shutdown_event,      /* SHUTDOWN_EVENT_IDX */
            g->pm.message_ready,    /* MESSAGE_EVENT_IDX */
            g->pm.rov.hEvent,       /* ROV_EVENT_IDX */
        };
        auto nevents = sizeof(events) / sizeof(*events);

        auto res = WaitForMultipleObjects(nevents, events, 0, INFINITE);
        while(res >= WAIT_OBJECT_0 && res < WAIT_OBJECT_0 + nevents) {
            auto idx = res - WAIT_OBJECT_0;
            switch(idx) {
                case SHUTDOWN_EVENT_IDX:
                    pm_shutdown(g);
                    free(g->pm.pbuf);
                    return 0;
                case MESSAGE_EVENT_IDX:
                    while(PeekMessage(&msg, 0, 0, 0, PM_REMOVE)) {
                        pm_handle_message(g, msg);
                    }
                    ResetEvent(g->pm.message_ready);
                    continue;
                case ROV_EVENT_IDX:
                    pm_process_packets(g);
                    continue;
            }
        }
    }
#undef SHUTDOWN_EVENT_IDX
#undef MESSAGE_EVENT_IDX
#undef ROV_EVENT_IDX
}

void pm_handle_message(Global *g, MSG msg) {
    auto& pm = g->pm;
    switch(msg.message) {
        case WM_TM_ENABLE_MAP: /* Deprecated */
        case WM_TM_DISABLE_MAP: /* Deprecated */
        case WM_TM_RECALC_FILTERS:
            pm_recalc_filters(g);
            return;
    }
}

void pm_recalc_filters(Global *g) {
    /* Map to min/max ip range and add to filter */
    /* Reopen windivert handle when not processing packets */
    if(g->pm.wd_handle)
        pm_shutdown(g);

    /* create a new filter for the new route */
    auto ibf = g->rmap.get_inbound_filter();
    auto obf = g->rmap.get_outbound_filter();
    if(ibf.size() == 0 && obf.size() == 0)
        return;
    g->pm.filter = "(inbound and (" + ibf + ")) or (outbound and (" + obf + "))";
    PRINT("WinDivert filter updated: %s", g->pm.filter.c_str());

    /* Open a new handle with the new mappings */
    HANDLE h = WinDivertOpen(g->pm.filter.c_str(), WINDIVERT_LAYER_NETWORK, 0, 0);

    g->pm.wd_handle = h;

    /* Initiate the recv loop */
    pm_process_packets(g);
}

void pm_process_packets(Global *g) {
    /*
     *  This is a state machine implementation
     *
     *  State Diagram:
     *
     *        [new ctx] -> WinDivertRecvEx() ─async→ [main control function]
     *                          |      ↑                        │
     *                        sync     │                        │
     *                          ↓      │                   rov signaled
     *      pm_process_single_packet()─┘                        │
     *                          ↑                               │
     *                          └───────────────────────────────┘
     *
     */
    BOOL res;
    g->pm.working = true;

    if(g->pm.recv_pending) {
        auto err = GetOverlappedResult(g->pm.wd_handle, &g->pm.rov, (LPDWORD)&g->pm.packet_len, 0);
        if(err) {
            g->pm.recv_pending = false;
            goto exit;
        }
        goto process;
    }

next_packet:
    ResetEvent(g->pm.rov.hEvent);
    res = WinDivertRecvEx(g->pm.wd_handle, g->pm.opbuf, MAX_PACKET_SIZE, &g->pm.packet_len,
                0, &g->pm.addr, &g->pm.addr_len, &g->pm.rov);
    if(res) {
process:
        g->pm.recv_pending = false;
        pm_process_single_packet(g);
        goto next_packet;
    } else {
        auto err = GetLastError();

        if(err == ERROR_IO_PENDING) {
            /* async */
            g->pm.recv_pending = true;
            goto exit;
        } else {
            /* error state, this will inititate a new recv */
            SetLastError(err);
            goto exit;
        }
    }

exit:
    g->pm.working = false;
}

void pm_process_remaining_packets(Global *g) {
    /* g->pm.wd_handle is guaranteed to be open here, but receiving is shutdown */
    g->pm.working = true;

    /* This is just an extension on the normal pm_process_packets() that will block
     * until all of the remaining packets are consumed, this starts from [main control function]
     *
     *  State Diagram:
     *
     *                     WinDivertRecvEx()         [main control function]
     *                          |      ↑                        │
     *                       awaited   │                        │
     *                          ↓      │                shutdown signaled
     *                 [process label]─┘                        │
     *                          ↑                               │
     *                          └───────────────────────────────┘
     *
     */

    /* the branch here is really just a formality on the boolean, this should always happen */
    if(g->pm.recv_pending) [[likely]] {
        auto err = GetOverlappedResult(g->pm.wd_handle, &g->pm.rov, (LPDWORD)&g->pm.packet_len, 0);
        if(err) {
            g->pm.recv_pending = false;
        }
        goto process;
    }

    for(;;) {
        ResetEvent(g->pm.rov.hEvent);
        if(WinDivertRecvEx(g->pm.wd_handle, g->pm.opbuf, MAX_PACKET_SIZE, &g->pm.packet_len,
                0, &g->pm.addr, &g->pm.addr_len, &g->pm.rov)) {
process:
            pm_process_single_packet(g);
            continue;
        } else {
            auto err = GetLastError();
            if(err == ERROR_IO_PENDING)  {
                GetOverlappedResult(g->pm.wd_handle, &g->pm.rov, (LPDWORD)&g->pm.packet_len, 1);
                goto process;
            } else if(err == ERROR_NO_DATA) {
                break;
            }
        }
    }

    g->pm.working = false;
}

void pm_shutdown(Global *g) {
    if(g->pm.wd_handle) {
        WinDivertShutdown(g->pm.wd_handle, WINDIVERT_SHUTDOWN_RECV);
        pm_process_remaining_packets(g);
        WinDivertShutdown(g->pm.wd_handle, WINDIVERT_SHUTDOWN_SEND);
        WinDivertClose(g->pm.wd_handle);
        g->pm.wd_handle = 0;
    }
}

void pm_process_single_packet(Global *g) {
    /*
     *  g->pm.opbuf
     *       .packet_len
     *       .addr
     *       .addr_len
     *       .ov
     *  are filled with a captured packet
     */
    if(g->pm.addr.Outbound) {
        pm_translate_outbound(g);
    } else {
        pm_translate_inbound(g);
    }

    /* Send and wait for the operation to complete */
    WinDivertHelperCalcChecksums(g->pm.spbuf, g->pm.packet_len, &g->pm.addr, 0);
    ResetEvent(g->pm.sov.hEvent);
    auto res = WinDivertSendEx(g->pm.wd_handle, g->pm.spbuf, g->pm.spbufsz, 0,
            0, &g->pm.addr, g->pm.addr_len, &g->pm.sov);
    if(res)
        return;

    DWORD err = GetLastError();
    if(err != ERROR_IO_PENDING) {
        SetLastError(err);
        return;
    }

    DWORD bytes_sent = 0;
    GetOverlappedResult(g->pm.wd_handle, &g->pm.sov, &bytes_sent, 1);
}


void pm_translate_inbound(Global *g) {
    /*
     *  at this point g->pm.opbuf / g->pm.packet_len specify the current packet
     */
    WINDIVERT_IPV6HDR *ipv6;
    auto res = WinDivertHelperParsePacket(
            g->pm.opbuf, g->pm.packet_len,
            0, &ipv6, 0,
            0, 0, 0, 0,
            0, 0, 0, 0);
    if(!ipv6 || ipv6->Version != 6)
        return;

    /* construct the new packet out of place first */
    u8 version = 4;
    u8 ihl = 5;
    u8 dscp = 0;
    u16 fragmentation = 0;
    u8 ttl = ipv6->HopLimit;
    u8 protocol = ipv6->NextHdr;
    u32 src_addr;
    u32 dst_addr = g->host_addr_ipv4;
    auto _saddr = g->rmap.get_inbound_mapping((u8*)ipv6->SrcAddr);
    memcpy(&src_addr, _saddr.addr, 4);

    WINDIVERT_IPHDR *ipv4 = pm_calc_ipv4_header_pos(ipv6);
    memset(ipv4, 0, 20);
    /* TODO: finish packet manipulation */

    g->pm.addr.IPv6 = 0;
    if(g->pm.addr.IPChecksum) {
        /* TODO: Calc checksums */
    }
}

void pm_translate_outbound(Global *g) {
    WINDIVERT_IPHDR *ipv4;
    auto res = WinDivertHelperParsePacket(
            g->pm.opbuf, g->pm.packet_len,
            &ipv4, 0, 0,
            0, 0, 0, 0,
            0, 0, 0, 0);
    if(!res)
        return;
    /* TODO: */
}

WINDIVERT_IPHDR *pm_calc_ipv4_header_pos(WINDIVERT_IPV6HDR *ipv6) {
    /* Calculate where the ipv4 header should start */
    /* IPv6 headers are a fixed size of 40 bytes */
    constexpr u32 ipv4sz = 20;
    constexpr u32 ipv6sz = 40;
    BYTE *pdata = ((BYTE*)ipv6) + ipv6sz;
    return (WINDIVERT_IPHDR*)(pdata - ipv4sz);
}

WINDIVERT_IPV6HDR *pm_calc_ipv6_header_pos(WINDIVERT_IPHDR *ipv4) {
    /* Calculate where the ipv6 header should start */
    /* IPv4 headers are typically 20 bytes, but the size is not fixed */
    constexpr u32 ipv6sz = 40;
    BYTE *pdata = ((BYTE*)ipv4) + ipv4->HdrLength;
    return (WINDIVERT_IPV6HDR*)(pdata - ipv6sz);
}
