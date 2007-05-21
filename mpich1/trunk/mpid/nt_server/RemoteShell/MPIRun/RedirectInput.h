#ifndef REDIRECT_INPUT_H
#define REDIRECT_INPUT_H

#include <windows.h>

struct RedirectInputThreadArg
{
	HANDLE hEvent;
	IStream **ppStream;
};

void RedirectInputThread(RedirectInputThreadArg *arg);

#endif
