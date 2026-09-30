#ifndef _GUI_HH_
#define _GUI_HH_

#include <windows.h>
#include "common.hh"

namespace UI {

void Window(Global *g);
ULONG Thread(void *g);
void Frame(Global *g);
void SitesTable(Global *g);
void MappingsTable(Global *g);

}

#endif //_GUI_HH_
