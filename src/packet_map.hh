#ifndef _PACKET_MAP_HH_
#define _PACKET_MAP_HH_

#include "common.hh"
#include <cassert>

#define IP_HDR_HEADROOM 40
#define PACKET_BUFFER_SZ 0x1fffe
#define MAX_PACKET_SIZE 0xffff

using std::pair;

namespace PM {

DWORD __stdcall thread(void *_g);

}

#endif //_PACKET_MAP_HH_
