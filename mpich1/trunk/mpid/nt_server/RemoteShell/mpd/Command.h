#ifndef COMMAND_H
#define COMMAND_H

#include <winsock2.h>
#include <windows.h>

#define MPD_CMD_ADD				10
#define MPD_CMD_REMOVE			11

#define MPD_CMD_LAUNCH			20
#define MPD_CMD_LAUNCH_RET		21
#define MPD_CMD_LAUNCH_EXITCODE	22
#define MPD_CMD_KILL			23
#define MPD_CMD_KILL_GROUP		24
#define MPD_CMD_INCREMENT		25
#define MPD_CMD_DECREMENT		26
#define MPD_CMD_PS				27

#define MPD_CMD_PUTC 			30
#define MPD_CMD_PUT 			31
#define MPD_CMD_GET				32
#define MPD_CMD_GETRETURN		33
#define MPD_CMD_DELETE_ID		34
#define MPD_CMD_DELETE_KEY		35

#define MPD_CMD_ENABLE			40
#define MPD_CMD_DISABLE			41

#define MPD_CMD_FORWARD			50

#define MPD_CMD_QUIT			60

#define MPD_CMD_DESTROY_RING	70
#define MPD_CMD_HOSTS			71
#define MPD_CMD_RUN_THE_RING	72
#define MPD_CMD_PRINT_LIST		73
#define MPD_CMD_PRINT_LISTS		74
#define MPD_CMD_PRINT_DATABASE	75

#define MPD_CMD_CPUSAGE			80

#define CMD_BUFF_SIZE			4096

typedef struct CommandData * MPD_CMD_HANDLE;

struct CommandHeader
{
	unsigned long nSrcIP;
	int nSrcPort;
	char cCommand;
	CommandData *pData;
	int nBufferLength;
};

struct CommandData
{
	int nCommand;
	CommandHeader hCmd;
	char pCommandBuffer[CMD_BUFF_SIZE];
	int nPort;
	char pszHost[100];
	bool bSuccess;

	HANDLE hCommandComplete;
	bool bCommandInProgress;

	CommandData *pNext;

	CommandData():hCommandComplete(NULL), bCommandInProgress(false), bSuccess(true) { hCmd.nBufferLength = 0; pszHost[0] = '\0'; };
	~CommandData() { if (hCommandComplete) CloseHandle(hCommandComplete); };
	CommandData& operator=(CommandData &data);
};

MPD_CMD_HANDLE InsertCommand(CommandData &data);
int	WaitForCommand(MPD_CMD_HANDLE hCommand, void *pBuffer = NULL, int *pnLength = NULL);
CommandData* GetNextCommand();
int	MarkCommandCompleted(CommandData *pCommand);
int CloseCommands();

#endif
