#ifndef SERVER_THREAD_H
#define SERVER_THREAD_H

void SocketServerThread(int port);
extern HANDLE g_hStopSocketLoopEvent;

#endif
