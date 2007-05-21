#ifndef REDIRECTION_H
#define REDIRECTION_H

extern int g_nIOListenPort;
extern char g_pszIOListenHost[100];

void RedirectIOLoopThread(void * hReadyEvent);
void SetConnectionsLeft(int n);
void WaitForAllConnections();

#endif
