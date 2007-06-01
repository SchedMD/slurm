#ifndef REDIRECTION_H
#define REDIRECTION_H

#include "sockets.h"

extern int g_nIOListenPort;
extern char g_pszIOListenHost[100];
extern LONG g_nConnectionsLeft;
extern HANDLE g_hNoMoreConnectionsEvent;

void RedirectIOLoopThread(HANDLE hReadyEvent);

#endif
