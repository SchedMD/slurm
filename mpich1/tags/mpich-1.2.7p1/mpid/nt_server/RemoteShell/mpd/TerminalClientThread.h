#ifndef TERMINAL_CLIENT_THREAD_H
#define TERMINAL_CLIENT_THREAD_H

#include <winsock2.h>
#include <windows.h>

struct TerminalClientThreadArg
{
	HANDLE hInput, hOutput, hEndOutput;
};

void TerminalClientThread(TerminalClientThreadArg *pArg);

#endif
