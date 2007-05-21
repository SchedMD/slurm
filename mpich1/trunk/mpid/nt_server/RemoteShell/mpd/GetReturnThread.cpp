#include "GetReturnThread.h"
#include "global.h"

// Function name	: GetReturnThread
// Description	    : 
// Return type		: void 
// Argument         : GetReturnThreadArg *pArg
void GetReturnThread(GetReturnThreadArg *pArg)
{
	int n = 0;
	void *pDbsValue;
	char *pBuf;
	MPD_CMD_HANDLE hCommand;

	g_Database.Get(pArg->pszDbsID, pArg->pszDbsKey, pDbsValue, &n);
	if (n>0)
	{
		pDbsValue = new char[n];
		if (g_Database.Get(pArg->pszDbsID, pArg->pszDbsKey, pDbsValue, &n) == MPI_DBS_SUCCESS)
		{
			pArg->command.hCmd.nBufferLength = 2 * sizeof(unsigned long) + 2 * sizeof(int);
			pBuf = &pArg->command.pCommandBuffer[2 * sizeof(unsigned long) + sizeof(int)];
			*((int *)(pBuf)) = n;
			pBuf += sizeof(int);
			memcpy(pBuf, pDbsValue, n);
			delete pArg->pszDbsKey;
		}
		delete pDbsValue;
	}
	pArg->command.hCmd.nBufferLength += n;
	pArg->command.nCommand = MPD_CMD_FORWARD;
	
	hCommand = InsertCommand(pArg->command);
	//WaitForCommand(hCommand);

	delete pArg;
}

// Function name	: GetThread
// Description	    : 
// Return type		: void 
// Argument         : GetReturnThreadArg *pArg
void GetThread(GetReturnThreadArg *pArg)
{
	int n = 0;
	void *pDbsValue = NULL;

	g_Database.Get(pArg->pszDbsID, pArg->pszDbsKey, pDbsValue, &n);
	if (n>0)
	{
		pDbsValue = new char[n];
		if (g_Database.Get(pArg->pszDbsID, pArg->pszDbsKey, pDbsValue, &n) == MPI_DBS_SUCCESS)
		{
			pArg->pCommand->hCmd.nBufferLength = n;
			memcpy(pArg->pCommand->pCommandBuffer, pDbsValue, n);
			delete pArg->pszDbsKey;
		}
		delete pDbsValue;
	}

	MarkCommandCompleted(pArg->pCommand);
	delete pArg;
}
