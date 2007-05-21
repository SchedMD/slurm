#ifndef NT_GLOBAL_CPP_H
#define NT_GLOBAL_CPP_H

#include <winsock2.h>
#include <windows.h>

#include "nt_common.h"
#include "nt_global.h"
#include "nt_tcp_sockets.h"
#include "nt_log.h"
#include "MessageQueue.h"
#include "vipl.h"
#include "Database.h"

#define MULTI_COLOR_OUTPUT

#define MAX_NUM_NICS 4

#define NT_TCP_CONNECT_INFO_CMD		0
#define NT_TCP_PROCESS_INFO_CMD		1
#define NT_TCP_END_CMD				2

#define NT_TCP_CTRL_CMD_INIT_DATA_TO_ROOT		0
#define NT_TCP_CTRL_CMD_PROCESS_CONNECT_INFO	1
#define NT_TCP_CTRL_CMD_PROCESS_INFO			2
#define NT_TCP_CTRL_CMD_POST_IN_DONE			3
#define NT_TCP_CTRL_CMD_ALL_IN_DONE				4
#define NT_TCP_CTRL_CMD_ABORT					5

#define NT_COMM_CMD_ADD_SOCKET		1
#define NT_COMM_CMD_EXIT			2

#define NT_HOSTNAME_LEN				100
#define NT_EXENAME_LEN				256
#define NT_TCP_WAIT_TIME			10000
#define NT_CREATE_THREAD_RETRIES     5
#define NT_CREATE_THREAD_SLEEP_TIME  250

#define NT_MSG_READING_TAG			0
#define NT_MSG_READING_LENGTH		1
#define NT_MSG_READING_BUFFER		2

//#define NT_THREAD_STACK_SIZE 1024*1024
#define NT_THREAD_STACK_SIZE 0

struct NT_Message					// Structure accessed by completion port or via thread to store the current message
{
	int tag;
	int length;
	void *buffer;
	int nRemaining;
	DWORD nRead;
	OVERLAPPED ovl;
	MessageQueue::MsgQueueElement *pElement;
	int state;					// NT_MSG_READING_TAG, NT_MSG_READING_LENGTH, NT_MSG_READING_BUFFER
};

struct VI_Info
{
	LONG valid, lock;
	VIP_NIC_HANDLE		hNic;
	VIP_VI_HANDLE		hVi;
	VIP_VI_ATTRIBUTES	Vi_RemoteAttribs;
	//VIP_VI_ATTRIBUTES	Vi_LocalAttribs;
	VIP_DESCRIPTOR		*pRecvDesc, **pSendDesc, *pDesc;
	VIP_MEM_HANDLE		mhSend, mhReceive;
	//VIP_RETURN			dwStatus;
	void *pSendDescriptorBuffer, *pReceiveDescriptorBuffer;
	//void *pDataBuffer;
	
	//char localbuf[40];
	char remotebuf[40];
	VIP_NET_ADDRESS *pLocalAddress;
	VIP_NET_ADDRESS *pRemoteAddress;
	unsigned char *descriminator;
	int descriminator_len;
	
	int nCurSendIndex;
	long nPostedSends;
	int nNumSendDescriptors;
	int nNumRecvDescriptors;
	int nReceivesPerAck;
	int nSendsPerAck;
	long nSendAcked;
	unsigned int nNumSent, nNumReceived, nSequenceNumberSend, nSequenceNumberReceive;
};
// global structures
struct NT_ipvishm_ProcEntry
{
	NT_Message msg;
	VI_Info vinfo;

	SOCKET sock;					// Communication socket
	WSAEVENT sock_event;			// Communication socket event
	HANDLE hConnectLock;
	int listen_port;				// Port where thread is listening for socket connections
	int control_port;				// Port where thread is listening for control message socket connections

	int shm;						// FALSE(0) or TRUE(1) if this host can be reached through shared memory
	int via;						// FALSE(0) or TRUE(1) if this host can be reached through VI


	// Description of process
	long pid;						// process id
	char host[NT_HOSTNAME_LEN];		// host where process resides
	char exename[NT_EXENAME_LEN];	// command line launched on the node
	int multinic;
	int num_nics;
	unsigned int nic_ip[MAX_NUM_NICS];

	HANDLE hValidDataEvent;			// Event signalling the data in this structure is valid 
									// This does not include sock, sock_event or vinfo
};

struct ControlLoopClientArg
{
	SOCKET sock;
	HANDLE sock_event;
};

// nt_ipvishm_comport.cpp
extern HANDLE g_hCommPortThread;
extern HANDLE g_hCommPort;
extern HANDLE g_hCommPortEvent;
extern int g_nCommPortCommand;
extern int g_NumCommPortThreads;
int ConnectTo(int remote_iproc);
void CommPortThread(HANDLE hReadyEvent);

// nt_vi.cpp
bool InitVI();
void EndVI();
void NT_ViSend(int type, void *buffer, unsigned int length, int to);
int ConnectViTo(int nRemoteRank);
extern unsigned char g_ViDescriminator[16];
extern int g_nViDescriminator_len;
extern bool	g_bViClosing;
int ViWorkerThread(int bRepeating);

// nt_ipvishm_priv.cpp
extern NT_ipvishm_ProcEntry *g_pProcTable;
extern bool g_bInNT_ipvishm_End;
extern MessageQueue g_MsgQueue;
extern char g_pszHostName[NT_HOSTNAME_LEN];
extern char g_pszRootHostName[NT_HOSTNAME_LEN];
extern int g_nRootPort;
extern HANDLE *g_aRunningProcess;
extern char g_pszJobID[100];
extern bool g_bUseDatabase;
extern Database g_Database;
extern bool g_bUseBNR;
extern unsigned int g_nNicMask;
extern unsigned int g_nNicNet;
extern bool g_bMultinic;

// nt_smp.cpp
class ShmemLockedQueue;
extern ShmemLockedQueue **g_pShmemQueue;
extern HANDLE *g_hShpSendCompleteEvent;
extern HANDLE *g_hProcesses;
void InitSMP();
void EndSMP();
void NT_ShmSend(int type, void *buffer, int length, int to);
int GetShmemClique();

// nt_ipvishm_control_loop.cpp
void ControlLoopClientThread(ControlLoopClientArg *arg);
void ControlLoopThread(HANDLE hReadyEvent);
bool SendInitDataToRoot();
bool GetProcessConnectInfo(int iproc);
bool GetProcessInfo(int iproc);
bool SendInDoneMsg();
extern HANDLE g_hAllInDoneEvent;
extern HANDLE g_hOkToPassThroughDone;
extern HANDLE g_hControlLoopThread;
extern HANDLE g_hStopControlLoopEvent;
extern HANDLE g_hEveryoneConnectedEvent;

#endif
