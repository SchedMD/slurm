#include "global.h"

MPDList g_List;
DatabaseServer g_Database;
bool g_bDatabaseIsLocal = false;
bool g_bLeftConnected = false;
bool g_bRightConnected = false;
LONG g_nNextGroupID = -1;
LONG g_nMaxGroupID = -1;
