#ifndef GET_RETURN_THREAD_H
#define GET_RETURN_THREAD_H

#include "Command.h"

struct GetReturnThreadArg
{
	char pszDbsID[256], *pszDbsKey;
	CommandData command;
	CommandData *pCommand;
};

void GetReturnThread(GetReturnThreadArg *pArg);
void GetThread(GetReturnThreadArg *pArg);

#endif
