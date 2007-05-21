#include "Command.h"

HANDLE hCommandAvailableEvent = CreateEvent(NULL, TRUE, FALSE, NULL);
HANDLE hCommandMutex = CreateMutex(NULL, FALSE, NULL);
CommandData *pCommandList = NULL;

// Function name	: CommandData::operator=
// Description	    : 
// Return type		: CommandData& 
// Argument         : CommandData &data
CommandData& CommandData::operator=(CommandData &data)
{
	if (this != &data)
	{
		bCommandInProgress = data.bCommandInProgress;
		hCmd = data.hCmd;
		hCommandComplete = data.hCommandComplete;
		nCommand = data.nCommand;
		if (hCmd.nBufferLength > 0)
			memcpy(pCommandBuffer, data.pCommandBuffer, hCmd.nBufferLength);
		nPort = data.nPort;
		pNext = NULL;
		strcpy(pszHost, data.pszHost);
	}
	return *this;
}

// Function name	: InsertCommand
// Description	    : 
// Return type		: MPD_CMD_HANDLE 
// Argument         : CommandData &data
MPD_CMD_HANDLE InsertCommand(CommandData &data)
{
	WaitForSingleObject(hCommandMutex, INFINITE);
	CommandData *pData = new CommandData;
	//memcpy(pData, &data, sizeof(data));
	*pData = data;
	pData->hCommandComplete = CreateEvent(NULL, TRUE, FALSE, NULL);
	pData->pNext = pCommandList;
	pCommandList = pData;
	SetEvent(hCommandAvailableEvent);
	ReleaseMutex(hCommandMutex);
	return (MPD_CMD_HANDLE)pData;
}

// Function name	: WaitForCommand
// Description	    : 
// Return type		: int 
// Argument         : MPD_CMD_HANDLE hCommand
// Argument         : void *pBuffer
// Argument         : int *pnLength
int WaitForCommand(MPD_CMD_HANDLE hCommand, void *pBuffer, int *pnLength)
{
	CommandData *p = (CommandData*)hCommand;

	WaitForSingleObject(p->hCommandComplete, INFINITE);

	WaitForSingleObject(hCommandMutex, INFINITE);
	if (pCommandList == p)
		pCommandList = pCommandList->pNext;
	else
	{
		CommandData *pCommand = pCommandList;
		while (pCommand->pNext != p)
			pCommand = pCommand->pNext;
		pCommand->pNext = p->pNext;
	}
	if (pBuffer && pnLength)
	{
		if (*pnLength >= p->hCmd.nBufferLength)
		{
			memcpy(pBuffer, p->pCommandBuffer, p->hCmd.nBufferLength);
			*pnLength = p->hCmd.nBufferLength;
		}
		else
			*pnLength = 0;
	}
	delete p;
	ReleaseMutex(hCommandMutex);
	return 0;
}

// Function name	: GetNextCommand
// Description	    : 
// Return type		: CommandData* 
CommandData* GetNextCommand()
{
	while (true)
	{
		WaitForSingleObject(hCommandAvailableEvent, INFINITE);
		
		WaitForSingleObject(hCommandMutex, INFINITE);
		
		CommandData *p = pCommandList;
		while (p)
		{
			if (!p->bCommandInProgress)
			{
				//p->hCommandComplete = CreateEvent(NULL, TRUE, FALSE, NULL);
				p->bCommandInProgress = true;
				CommandData *n = p->pNext;
				ResetEvent(hCommandAvailableEvent);
				while (n)
				{
					if (!n->bCommandInProgress)
					{
						SetEvent(hCommandAvailableEvent);
						break;
					}
					n = n->pNext;
				}
				ReleaseMutex(hCommandMutex);
				return p;
			}
			p = p->pNext;
		}

		ReleaseMutex(hCommandMutex);
	}
	return NULL;
}

// Function name	: MarkCommandCompleted
// Description	    : 
// Return type		: int 
// Argument         : CommandData *pCommand
int MarkCommandCompleted(CommandData *pCommand)
{
	if (pCommand == NULL)
		return 1;

	SetEvent(pCommand->hCommandComplete);
	return 0;
}

// Function name	: CloseCommands
// Description	    : 
// Return type		: int 
int CloseCommands()
{
	CloseHandle(hCommandAvailableEvent);
	CloseHandle(hCommandMutex);
	CommandData * p;
	while (pCommandList)
	{
		p = pCommandList;
		pCommandList = pCommandList->pNext;
		delete p;
	}
	return 0;
}
