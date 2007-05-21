#ifndef GLOBAL_H
#define GLOBAL_H

#include "MPDList.h"
#include "Database.h"

extern MPDList g_List;
extern DatabaseServer g_Database;
extern bool g_bDatabaseIsLocal;
extern bool g_bLeftConnected;
extern bool g_bRightConnected;
extern LONG g_nNextGroupID;
extern LONG g_nMaxGroupID;

#endif
