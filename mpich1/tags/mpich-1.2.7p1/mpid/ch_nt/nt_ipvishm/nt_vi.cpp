#include "nt_global_cpp.h"
#include "parsecliques.h"
#include "lock.h"
//#include <math.h>
#include "vipl.h"
#include "bnrfunctions.h"

// Definitions
#define VITIMEOUT					10000			/* 10 seconds */
#define ADDR_LEN					6
#define DESIRED_PACKET_LENGTH       64*1024
#define INITIAL_NUM_CQ_ENTRIES		64
#define CQ_ENTRIES_INCREMENT		32

// Global variables
char			g_pszNicBaseName[100]	= "nic"; // base name used to generate nic names - in case there are multiple nics per host
bool			g_bViUsePolling			= false;
VIP_NIC_HANDLE	g_hViNic				= NULL;
VIP_CQ_HANDLE	g_hViCQ					= NULL;
VIP_ULONG		g_viMTU					= 32768; // VI implementations are required to handle 32k
HANDLE			g_hViListenThread		= NULL;
HANDLE			g_hViWorkerThread		= NULL;
unsigned char	g_ViDescriminator[16]	= "MPICHisGreat";
int				g_nViDescriminator_len	= 12;
int				g_nNumCQEntries			= INITIAL_NUM_CQ_ENTRIES;

VIP_VI_HANDLE	g_hConnectToVi			= NULL;
VIP_VI_HANDLE	g_hListenThreadVi		= NULL;
int				g_nConnectGate			= 0;
int				g_nListenGate			= 0;
int				g_nWorkerGate			= 0;
struct ClosedVINode
{
	VIP_VI_HANDLE hVi;
	ClosedVINode *pNext;
};
ClosedVINode *	g_pClosedViList			= NULL;
bool			g_bViClosing 			= false;
bool			g_bViSingleThreaded		= false;


// Local function prototypes
bool ViSendMsg(VI_Info *vinfo, void *pBuffer, unsigned int length);
bool ViSendFirstPacket(VI_Info *vinfo, void *&pBuffer, unsigned int &length, int tag);
bool ViSendPacket(VI_Info *vinfo, void *pBuffer, unsigned int length);
bool ViFlushPackets(VI_Info *vinfo);
bool ViSendAck(VI_Info *vinfo);
bool ViRecvAck(VI_Info *vinfo);
bool AssertSuccess(int status, char *msg, VIP_DESCRIPTOR *desc = NULL);

// Vipl function pointers
VIP_RETURN (VI_CALL *VipOpenNic)(    IN const VIP_CHAR *DeviceName,    OUT VIP_NIC_HANDLE *NicHandle);
VIP_RETURN (VI_CALL *VipCloseNic)(    IN VIP_NIC_HANDLE NicHandle);
VIP_RETURN (VI_CALL *VipQueryNic)(    IN VIP_NIC_HANDLE NicHandle,    OUT VIP_NIC_ATTRIBUTES *Attributes);
VIP_RETURN (VI_CALL *VipRegisterMem)(    IN VIP_NIC_HANDLE NicHandle,    IN VIP_PVOID VirtualAddress,    IN VIP_ULONG Length,    IN VIP_MEM_ATTRIBUTES *MemAttributes,    OUT VIP_MEM_HANDLE *MemHandle);
VIP_RETURN (VI_CALL *VipDeregisterMem)(    IN VIP_NIC_HANDLE NicHandle,    IN VIP_PVOID VirtualAddress,    IN VIP_MEM_HANDLE MemHandle);
VIP_RETURN (VI_CALL *VipQueryMem)(    IN VIP_NIC_HANDLE NicHandle,    IN VIP_PVOID VirtualAddress,    IN VIP_MEM_HANDLE MemHandle,    OUT VIP_MEM_ATTRIBUTES *MemAttributes);
VIP_RETURN (VI_CALL *VipSetMemAttributes)(    IN VIP_NIC_HANDLE NicHandle,    IN VIP_PVOID VirtualAddress,    IN VIP_MEM_HANDLE MemHandle,    IN VIP_MEM_ATTRIBUTES *MemAttributes);
VIP_RETURN (VI_CALL *VipErrorCallback)(    IN VIP_NIC_HANDLE NicHandle,    IN VIP_PVOID Context,    IN VIP_ERROR_HANDLER ErrorHandler);
VIP_RETURN (VI_CALL *VipQuerySystemManagementInfo)(    IN VIP_NIC_HANDLE NicHandle,    IN VIP_ULONG InfoType,    OUT VIP_PVOID SysManInfo);
VIP_RETURN (VI_CALL *VipCreatePtag)(    IN VIP_NIC_HANDLE NicHandle,    OUT VIP_PROTECTION_HANDLE *ProtectionTag);
VIP_RETURN (VI_CALL *VipDestroyPtag)(    IN VIP_NIC_HANDLE NicHandle,    IN VIP_PROTECTION_HANDLE ProtectionTag);
VIP_RETURN (VI_CALL *VipCreateVi)(    IN VIP_NIC_HANDLE NicHandle,    IN VIP_VI_ATTRIBUTES *ViAttributes,    IN VIP_CQ_HANDLE SendCQHandle,    IN VIP_CQ_HANDLE RecvCQHandle,    OUT VIP_VI_HANDLE *ViHandle);
VIP_RETURN (VI_CALL *VipDestroyVi)(    IN VIP_VI_HANDLE ViHandle);
VIP_RETURN (VI_CALL *VipQueryVi)(    IN VIP_VI_HANDLE ViHandle,    OUT VIP_VI_STATE *State,    OUT VIP_VI_ATTRIBUTES *Attributes,    OUT VIP_BOOLEAN *SendQueueEmpty,    OUT VIP_BOOLEAN *RecvQueueEmpty);
VIP_RETURN (VI_CALL *VipSetViAttributes)(    IN VIP_VI_HANDLE ViHandle,    IN VIP_VI_ATTRIBUTES *Attributes);
VIP_RETURN (VI_CALL *VipPostSend)(    IN VIP_VI_HANDLE ViHandle,    IN VIP_DESCRIPTOR *DescriptorPtr,    IN VIP_MEM_HANDLE MemoryHandle);
VIP_RETURN (VI_CALL *VipSendDone)(    IN VIP_VI_HANDLE ViHandle,    OUT VIP_DESCRIPTOR **DescriptorPtr);
VIP_RETURN (VI_CALL *VipSendWait)(    IN VIP_VI_HANDLE ViHandle,    IN VIP_ULONG TimeOut,    OUT VIP_DESCRIPTOR **DescriptorPtr);
VIP_RETURN (VI_CALL *VipSendNotify)(    IN VIP_VI_HANDLE ViHandle,    IN VIP_PVOID Context,    IN VIP_VI_CALLBACK Callback);
VIP_RETURN (VI_CALL *VipPostRecv)(    IN VIP_VI_HANDLE ViHandle,    IN VIP_DESCRIPTOR *DescriptorPtr,    IN VIP_MEM_HANDLE MemoryHandle);
VIP_RETURN (VI_CALL *VipRecvDone)(    IN VIP_VI_HANDLE ViHandle,    OUT VIP_DESCRIPTOR **DescriptorPtr);
VIP_RETURN (VI_CALL *VipRecvWait)(    IN VIP_VI_HANDLE ViHandle,    IN VIP_ULONG TimeOut,    OUT VIP_DESCRIPTOR **DescriptorPtr);
VIP_RETURN (VI_CALL *VipRecvNotify)(    IN VIP_VI_HANDLE ViHandle,    IN VIP_PVOID Context,    IN VIP_VI_CALLBACK Callback);
VIP_RETURN (VI_CALL *VipConnectWait)(    IN VIP_NIC_HANDLE NicHandle,    IN VIP_NET_ADDRESS *LocalAddr,    IN VIP_ULONG Timeout,    OUT VIP_NET_ADDRESS *RemoteAddr,    OUT VIP_VI_ATTRIBUTES *RemoteViAttributes,    OUT VIP_CONN_HANDLE *ConnHandle);
VIP_RETURN (VI_CALL *VipConnectAccept)(    IN VIP_CONN_HANDLE ConnHandle,    IN VIP_VI_HANDLE ViHandle);
VIP_RETURN (VI_CALL *VipConnectReject)(    IN VIP_CONN_HANDLE ConnHandle);
VIP_RETURN (VI_CALL *VipConnectRequest)(    IN VIP_VI_HANDLE ViHandle,    IN VIP_NET_ADDRESS *LocalAddr,    IN VIP_NET_ADDRESS *RemoteAddr,    IN VIP_ULONG Timeout,    OUT VIP_VI_ATTRIBUTES *RemoteViAttributes);
VIP_RETURN (VI_CALL *VipDisconnect)(    IN VIP_VI_HANDLE ViHandle);
VIP_RETURN (VI_CALL *VipCreateCQ)(    IN VIP_NIC_HANDLE NicHandle,    IN VIP_ULONG EntryCount,    OUT VIP_CQ_HANDLE *CQHandle);
VIP_RETURN (VI_CALL *VipDestroyCQ)(    IN VIP_CQ_HANDLE CQHandle);
VIP_RETURN (VI_CALL *VipResizeCQ)(    IN VIP_CQ_HANDLE CQHandle,    IN VIP_ULONG EntryCount);
VIP_RETURN (VI_CALL *VipCQDone)(    IN VIP_CQ_HANDLE CQHandle,    OUT VIP_VI_HANDLE *ViHandle,    OUT VIP_BOOLEAN *RecvQueue);
VIP_RETURN (VI_CALL *VipCQWait)(    IN VIP_CQ_HANDLE CQHandle,    IN VIP_ULONG Timeout,    OUT VIP_VI_HANDLE *ViHandle,    OUT VIP_BOOLEAN *RecvQueue);
VIP_RETURN (VI_CALL *VipCQNotify)(    IN VIP_CQ_HANDLE CqHandle,    IN VIP_PVOID Context,    IN VIP_CQ_CALLBACK Callback);
VIP_RETURN (VI_CALL *VipNSInit)(    IN VIP_NIC_HANDLE NicHandle,    IN VIP_PVOID NSInitInfo);
VIP_RETURN (VI_CALL *VipNSGetHostByName)(    IN VIP_NIC_HANDLE NicHandle,    IN VIP_CHAR *Name,    OUT VIP_NET_ADDRESS *Address,    IN VIP_ULONG NameIndex);
VIP_RETURN (VI_CALL *VipNSGetHostByAddr)(    IN VIP_NIC_HANDLE NicHandle,    IN VIP_NET_ADDRESS *Address,    OUT VIP_CHAR *Name,    IN OUT VIP_ULONG *NameLen);
VIP_RETURN (VI_CALL *VipNSShutdown)(    IN VIP_NIC_HANDLE NicHandle);
VIP_RETURN (VI_CALL *VipConnectPeerRequest)(    IN VIP_VI_HANDLE ViHandle,    IN VIP_NET_ADDRESS *LocalAddr,    IN VIP_NET_ADDRESS *RemoteAddr,    IN VIP_ULONG Timeout);
VIP_RETURN (VI_CALL *VipConnectPeerDone)(    IN VIP_VI_HANDLE ViHandle,    OUT VIP_VI_ATTRIBUTES *RemoteAttributes);
VIP_RETURN (VI_CALL *VipConnectPeerWait)(    IN VIP_VI_HANDLE ViHandle,    OUT VIP_VI_ATTRIBUTES *RemoteViAttributes);
VIP_RETURN (VI_CALL *VipAddTagCQ)(	IN VIP_CQ_HANDLE CQHandle,	IN OUT VIP_EVENT_HANDLE *Event,	IN VIP_ULONG Tag,	IN VIP_ULONG Priority);
VIP_RETURN (VI_CALL *VipRemoveTagCQ)(	IN VIP_CQ_HANDLE CQHandle,	IN VIP_EVENT_HANDLE Event,	IN VIP_ULONG Tag);
VIP_RETURN (VI_CALL *VipPostDeferredSends)(	IN VIP_VI_HANDLE vihandle, 	IN VIP_BOOLEAN enableinterrupt,	IN OUT VIP_BOOLEAN *sendsdeferred);
// Non-standard functions
VIP_PVOID (VI_CALL *VipGetUserData)(	IN VIP_VI_HANDLE ViHandle);
void (VI_CALL *VipSetUserData)(	IN VIP_VI_HANDLE vih, 	IN VIP_PVOID data);

//////////////////////////////////////////////////////////////////////
///////////////////////// Vi helper functions ////////////////////////
//////////////////////////////////////////////////////////////////////
static VIP_VI_ATTRIBUTES default_vi_attribs = {
	VIP_SERVICE_RELIABLE_DELIVERY,
	32768,						// MTU
	0,							// QOS is unused
	0,							// use the default protection tag
	0,							// no RDMA Write
	0							// no RDMA Read
};

static char * codeTab[] = {
	"Error posting descriptor",       
	"Connection lost",
	"Receive on empty queue",         
	"VI over-run",
	"RDMA write protection error",    
	"RDMA Write data error",
	"RDMA write abort",               
	"*invalid* - RDMA read",
	"Protection error on completion", 
	"RDMA transport error",
	"Catastrophic error"
};
void ErrorCallbackFunction(void *ctx, VIP_ERROR_DESCRIPTOR *d)
{
	char buf[256], *p = buf;
	ClosedVINode *n;

	if (g_bViClosing)
		return;

	switch (d->ResourceCode) 
	{
	case VIP_RESOURCE_NIC:
		sprintf(p, "callback on NIC handle %x", d->NicHandle);
		break;
    case VIP_RESOURCE_VI:
		n = g_pClosedViList;
		while (n != NULL)
		{
			if (d->ViHandle == n->hVi)
				return;
			n = n->pNext;
		}
		sprintf(p, "callback on VI handle %x", d->ViHandle);
		break;
    case VIP_RESOURCE_CQ:
		sprintf(p, "callback on CQ handle %x", d->CQHandle);
		break;
	case VIP_RESOURCE_DESCRIPTOR:
		sprintf(p, "callback on descriptor %x", d->DescriptorPtr);
		break;
	}
	p += strlen(p);
	sprintf(p, ": %s", codeTab[d->ErrorCode]);

	printf("Error callback - %s\n", buf);
	// call your_log_function(buf)
}

/*
  function:    (char*)msg = DescriptorError(VIP_RETURN code,
                                            VIP_DESCRIPTOR *desc);

  arguments:   code - return value from VipRecvDone/VipSendDone
               desc - descriptor returned from Vip****Done

  returns:     also returns pointer to printed string
  
  description: prints out a text description of the returned error
               information. 

  usage:       call as follows:

                VIP_VI_HANDLE vi;
				VIP_RETURN status;
				 ...
				status = VipRecvDone(vi, &d);
				if (status != VIP_SUCCESS) {
				    DescriptorError(status, d);
					...
				} 
*/
static char *errTab[] = {
	"VIP_SUCCESS", 
	"VIP_NOT_DONE", 
	"VIP_INVALID_PARAMETER",
	"VIP_ERROR_RESOURCE", 
	"VIP_TIMEOUT", 
	"VIP_REJECT",
	"VIP_INVALID_RELIABILITY_LEVEL", 
	"VIP_INVALID_MTU", 
	"VIP_INVALID_QOS",
	"VIP_INVALID_PTAG", 
	"VIP_INVALID_RDMAREAD", 
	"VIP_DESCRIPTOR_ERROR",
	"VIP_INVALID_STATE", 
	"VIP_ERROR_NAMESERVICE", 
	"VIP_NO_MATCH",
	"VIP_NOT_REACHABLE", 
	"VIP_ERROR_NOT_SUPPORTED"
};
static struct {
	int mask;
	char *msg1, *msg2;
} statusTab[] = {
	{VIP_STATUS_FORMAT_ERROR,			"Format Error", 0},
	{VIP_STATUS_PROTECTION_ERROR,		"Protection Error", 0},
	{VIP_STATUS_LENGTH_ERROR,			"Length Error", 0},
	{VIP_STATUS_PARTIAL_ERROR,			"Partial Frame Error", 0},
	{VIP_STATUS_DESC_FLUSHED_ERROR, 	"Descriptor Flushed", 0},
	{VIP_STATUS_TRANSPORT_ERROR,		"Transport Error", 0},
	{VIP_STATUS_RDMA_PROT_ERROR,		"RDMA Protection Error", 0},
	{VIP_STATUS_REMOTE_DESC_ERROR, 		"Remote Descriptor Error", 0},
	{VIP_STATUS_OP_RECEIVE,				"Direction = RECV", "Direction = SEND"},
	{VIP_STATUS_OP_RDMA_WRITE,			"Type = RDMA Write sent"},
	{VIP_STATUS_OP_REMOTE_RDMA_WRITE,	"Type = RDMA Write received"},
	{VIP_STATUS_IMMEDIATE,				"Immediate Data Valid"},
	{0, 0, 0}
};
char * DescriptorError(int r, VIP_DESCRIPTOR *d)
{
	int dd = (int)d, s, i;
	static char buf[1000], *p = buf;
	
	sprintf(p, "Return     = %d (%s)\nDescriptor = 0x%x %s\n",
		r, (r <= VIP_ERROR_NOT_SUPPORTED) ? errTab[r] : "*INVALID*", 
		d, (dd & 63) ? "*ERROR - not 64-byte aligned*" : "");
	if (!d) 
	{
		printf("%s", buf);
		return buf;
	}
	
	p += strlen(p);
	sprintf(p, " Control.Length = %d\n Control.Status = 0x%05x\n", 
		d->Control.Length, d->Control.Status);
	
	p += strlen(p);
	s = d->Control.Status;
	
	if ((s & VIP_STATUS_DONE) == 0)
		sprintf(p, " *ERROR* - descriptor not marked done\n");
	
	for (i = 0; ; i++) 
	{
		p += strlen(p);
		if (statusTab[i].msg1 == 0)
			break;
		if ((s & statusTab[i].mask) == statusTab[i].mask)
			sprintf(p, "                  0x%05x - %s\n",
			statusTab[i].mask, statusTab[i].msg1);
		else if (statusTab[i].msg2)
			sprintf(p, "                            %s\n",
			statusTab[i].msg2);
	}
	
	printf("%s", buf);
	return buf;
}

// Function name	: AssertSuccess
// Description	    : 
// Return type		: bool 
// Argument         : int status
// Argument         : char *msg
// Argument         : VIP_DESCRIPTOR *desc
bool AssertSuccess(int status, char *msg, VIP_DESCRIPTOR *desc)
{
	if (status == VIP_SUCCESS)
		return true;
	if (g_bViClosing)
		return false;
	if (msg != NULL)
		printf("%s\n", msg);
	if (desc != NULL)
		DescriptorError(status, desc);
	else
		printf("Return status: %d\n", status);
	fflush(stdout);
	return false;
}

/*
  function:    get_descriptors
  arguments:   nic - nic handle
               num - number of descriptors
			   buflen - data length
  returns:     list of descriptors
               *mh - memory handle  (for VipPost*(), VipDeregisterMem())
			   *ptr - pointer to allocated memory (for free())
  description: Allocates and formats a list of descriptors.
*/
VIP_DESCRIPTOR * get_descriptors(VIP_NIC_HANDLE nic, int num, int buflen, VIP_MEM_HANDLE *mh, void **ptr)
{
	int status, i, len, buflen_aligned;
	char *p;
	VIP_DESCRIPTOR *free, *d;

	/* descriptors and buffers are allocated contiguously, with the data
	   buffer for a descriptor immediately following the descriptor
	   itself.  When allocating memory we have to allow for alignment
	   losses on the first descriptor (up to 64 bytes), plus alignment
	   losses on each additional descriptor if 'buflen' is not a
	   multiple of 64 bytes. 
	 */
	buflen_aligned = ((buflen+63) & ~63);
	len = 64 + num*(buflen_aligned+64);
	// switched to new so delete can be used to on ptr later
	//p = calloc(len, 1);
	p = new char[len];
	memset(p, 0, len);
	*ptr = p;
	
	status = VipRegisterMem(nic, p, len, 0, mh);
	if (status != VIP_SUCCESS)
	{
		printf("can't register memory\n");
		delete p;
		return NULL;
	}

	// Align the start pointer, and start carving out descriptors and
	//   buffers.  Link them through the Next field.
	p = (char*)((((int)p) + 63) & ~63); // 64-byte aligned
	free = 0;
	for (i = 0; i < num; i++) {
		d = (VIP_DESCRIPTOR*)p;
		d->Control.Next.Address = free;
		free = d;
		d->Control.SegCount = 1;
		d->Control.Control = 0;
		d->Control.Length = buflen;

		p += 64;
		d->Data[0].Handle = *mh;
		d->Data[0].Length = buflen;
		d->Data[0].Data.Address = p;
		p += buflen_aligned;
	}

	// return the list of allocated descriptors
	return free;
}
//////////////////////////////////////////////////////////////////////
//////////////// End of Vi helper functions //////////////////////////
//////////////////////////////////////////////////////////////////////

// Function name	: CloseVi
// Description	    : 
// Return type		: int 
// Argument         : VI_Info *vinfo
int CloseVi(VI_Info *vinfo)
{
	VIP_DESCRIPTOR *d;
	if (InterlockedExchange(&vinfo->valid, 0))
	{
		if (vinfo->hVi != NULL)
		{
			ClosedVINode *n = new ClosedVINode;
			n->pNext = g_pClosedViList;
			n->hVi = vinfo->hVi;
			g_pClosedViList = n;
			VipDisconnect(vinfo->hVi);
			do 
			{
				VipRecvDone(vinfo->hVi, &d);
			} while (d != 0);
			
			VipDestroyVi(vinfo->hVi);
			
			if (vinfo->pReceiveDescriptorBuffer != NULL)
			{
				VipDeregisterMem(g_hViNic, vinfo->pReceiveDescriptorBuffer, vinfo->mhReceive);
				delete vinfo->pReceiveDescriptorBuffer;
			}
			if (vinfo->pSendDescriptorBuffer != NULL)
			{
				VipDeregisterMem(g_hViNic, vinfo->pSendDescriptorBuffer, vinfo->mhSend);
				delete vinfo->pSendDescriptorBuffer;
			}
			
			if (vinfo->pSendDesc)
				delete vinfo->pSendDesc;
		}
		vinfo->pSendDesc = NULL;
		vinfo->hVi = NULL;
		vinfo->hNic = NULL;
		vinfo->pReceiveDescriptorBuffer = NULL;
		vinfo->pSendDescriptorBuffer = NULL;
	}

	return 0;
}

// Function name	: ConnectViTo
// Description	    : 
// Return type		: int (TRUE=1 or FALSE=0)
// Argument         : int nRemoteRank
int ConnectViTo(int nRemoteRank)
{
	VIP_RETURN			dwStatus;
	VIP_VI_ATTRIBUTES	Vi_RemoteAttribs;
	VIP_DESCRIPTOR		*pRecvDesc, **pSendDesc, *pDesc;
	VIP_MEM_HANDLE		mhSend, mhReceive;
	void				*pSendDescriptorBuffer, *pReceiveDescriptorBuffer;//, *pDataBuffer;
	char				localbuf[40], remotebuf[40];
	VIP_NET_ADDRESS		*pLocalAddress;
	VIP_NET_ADDRESS		*pRemoteAddress;
	int					nNumRecvDescriptors=32, nNumSendDescriptors=30;
	void				*vp;
	int					nSendsPerAck, nReceivesPerAck;
	unsigned char		ViDescriminator[16];
	int					nViDescriminator_len;

	//printf("ConnectViTo: Connecting to %d\n", nRemoteRank);fflush(stdout);
	// Create a VI
	//dwStatus = VipCreateVi(g_hViNic, &default_vi_attribs, g_hViCQ, g_hViCQ, &g_hConnectToVi);
	// Create a VI with only the receive queue associated with the completion queue
	dwStatus = VipCreateVi(g_hViNic, &default_vi_attribs, NULL, g_hViCQ, &g_hConnectToVi);
	if (!AssertSuccess(dwStatus, "can't create VI"))
		return 0;
	// Check and insert it in g_pProcTable
	//LPVOID pNull = NULL;
#ifdef USE_VC6_HEADERS
	if (InterlockedCompareExchange((void**)&g_pProcTable[nRemoteRank].vinfo.hVi, (void*)g_hConnectToVi, (void*)NULL) != NULL)
#else
	if (InterlockedCompareExchange((LONG volatile *)&g_pProcTable[nRemoteRank].vinfo.hVi, (LONG)g_hConnectToVi, (LONG)NULL) != NULL)
#endif
	{
		//printf("ConnectViTo: waiting for vinfo to be validated\n");fflush(stdout);
		// Connection has already been made by another thread.  Destroy this one and wait for the other one to be valid
		VipDestroyVi(g_hConnectToVi);
		while (g_pProcTable[nRemoteRank].vinfo.valid == 0)
			Sleep(200);
		return 1;
	}

	// Each node will establish VI connections using the 'JobID + rank' as the descriminator
	sprintf((char*)ViDescriminator, "%s%d", g_pszJobID, nRemoteRank);
	nViDescriminator_len = strlen((char*)ViDescriminator);

	pLocalAddress  = (VIP_NET_ADDRESS*)localbuf;
	pRemoteAddress = (VIP_NET_ADDRESS*)remotebuf;
	pLocalAddress->HostAddressLen = ADDR_LEN;
	vp = pLocalAddress->HostAddress + ADDR_LEN;
	pLocalAddress->DiscriminatorLen = nViDescriminator_len;
	memcpy(vp, ViDescriminator, nViDescriminator_len);

	// Save the descriminator for automatic reconnect
	//descriminator = (unsigned char*)vp;
	//descriminator_len = descriminator_length;
	
	if (nNumSendDescriptors & 0x1)
		nNumSendDescriptors++; // must be even
	
	pSendDesc = new VIP_DESCRIPTOR*[nNumSendDescriptors];

	if (g_bUseBNR)
	{
		char pszKey[100];
		sprintf(pszKey, "ListenHost%d", nRemoteRank);
		BNR_Get(g_myBNRgroup, pszKey, g_pProcTable[nRemoteRank].host);
	}
	else if (g_bUseDatabase)
	{
		char pszKey[100];
		int length = NT_HOSTNAME_LEN;
		sprintf(pszKey, "ListenHost%d", nRemoteRank);
		g_Database.Get(pszKey, g_pProcTable[nRemoteRank].host, &length);
	}
	else
		GetProcessConnectInfo(nRemoteRank);

	// Get the remote host information
	VipNSInit(g_hViNic, 0);
	dwStatus = VipNSGetHostByName(g_hViNic, g_pProcTable[nRemoteRank].host, pRemoteAddress, 0);
	if (!AssertSuccess(dwStatus, "can't find remote address"))
		return 0;

	// Append the discriminator
	vp = pRemoteAddress->HostAddress + ADDR_LEN;
	pRemoteAddress->DiscriminatorLen = nViDescriminator_len;
	memcpy(vp, ViDescriminator, nViDescriminator_len);

	// Reserve memory for descriptors, for sending and receiving data
	// I should allow for smaller sizes than g_viMTU
	pRecvDesc = get_descriptors(g_hViNic, nNumRecvDescriptors, g_viMTU, &mhReceive, &pReceiveDescriptorBuffer);
	pSendDesc[0] = get_descriptors(g_hViNic, nNumSendDescriptors, g_viMTU, &mhSend, &pSendDescriptorBuffer);
	pDesc = (VIP_DESCRIPTOR*)(pSendDesc[0]->Control.Next.Address);
	
	for (int i=1; i<nNumSendDescriptors; i++)
	{
		pSendDesc[i] = pDesc;
		pDesc = (VIP_DESCRIPTOR*)(pDesc->Control.Next.Address);
	}
	
	// Post the receive immediately
	VIP_DESCRIPTOR *pTemp2, *pTemp = pRecvDesc;
	while (pTemp)
	{
		pTemp2 = pTemp;
		// Advance to the next descriptor before calling PostRecv because PostRecv modifies the Address field
		pTemp = (VIP_DESCRIPTOR*)(pTemp->Control.Next.Address);
		dwStatus = VipPostRecv(g_hConnectToVi, pTemp2, mhReceive);
		if (!AssertSuccess(dwStatus, "can't post receive", pTemp2))
			return 0;
	}

	// Request a connection
	dwStatus = VipConnectRequest(g_hConnectToVi, pLocalAddress, pRemoteAddress, VITIMEOUT, &Vi_RemoteAttribs);
	if (!AssertSuccess(dwStatus, "connect request failed"))
	{
		MakeErrMsg(1, "VI Connection request to process %d failed\n", nRemoteRank);
		return 0;
	}

	// Set the user data for this connection to be the rank of the remote process
	VipSetUserData(g_hConnectToVi, (VIP_PVOID)nRemoteRank);

	// Send my rank and nSendsPerAck
	nSendsPerAck = nNumSendDescriptors / 2;
	pSendDesc[0]->Control.Control = VIP_CONTROL_OP_SENDRECV | VIP_CONTROL_IMMEDIATE;
	pSendDesc[0]->Control.Length = 2 * sizeof(int);
	pSendDesc[0]->Control.SegCount = 1;
	pSendDesc[0]->Control.Reserved = 0;
	pSendDesc[0]->Control.ImmediateData = 0;
	pSendDesc[0]->Data[0].Length = 2 * sizeof(int);
	pSendDesc[0]->Data[0].Handle = mhSend;
	((int*)pSendDesc[0]->Data[0].Data.Address)[0] = g_nIproc;
	((int*)pSendDesc[0]->Data[0].Data.Address)[1] = nSendsPerAck;

	dwStatus = VipPostSend(g_hConnectToVi, pSendDesc[0], mhSend);
	if (!AssertSuccess(dwStatus, "ConnectViTo:VipPostSend failed", pSendDesc[0]))
		return 0;
	dwStatus = VipSendWait(g_hConnectToVi, VITIMEOUT, &pDesc);
	if (!AssertSuccess(dwStatus, "ConnectViTo:VipSendWait failed", pDesc))
		return 0;

	// Receive ack
	if (g_bViSingleThreaded)
	{
		while (g_nConnectGate == 0)
			ViWorkerThread(0);
	}
	else
	{
		// Wait for the worker thread to signal that the packet is ready to be taken out of the queue
		// by setting the gate to 1
		while (g_nConnectGate == 0)
			Sleep(0);
	}
	// Remove the packet
	while ( (dwStatus = VipRecvDone(g_hConnectToVi, &pRecvDesc)) == VIP_NOT_DONE)
		Sleep(1);
	if (!AssertSuccess(dwStatus, "ConnectViTo:VipRecvDone failed", pRecvDesc))
	{
		MakeErrMsg(1, "Unable to receive connect packet from process %d\n", nRemoteRank);
		return 0;
	}
	if (pRecvDesc->Control.ImmediateData == 0)
	{
		// Close the VI due to loss in race condition
		VIP_DESCRIPTOR *d;
		VipDisconnect(g_hConnectToVi);
		do 
		{
			VipRecvDone(g_hConnectToVi, &d);
		} while (d != 0);

		VipDestroyVi(g_hConnectToVi);

		if (pReceiveDescriptorBuffer != NULL)
		{
			VipDeregisterMem(g_hViNic, pReceiveDescriptorBuffer, mhReceive);
			delete pReceiveDescriptorBuffer;
		}
		if (pSendDescriptorBuffer != NULL)
		{
			VipDeregisterMem(g_hViNic, pSendDescriptorBuffer, mhSend);
			delete pSendDescriptorBuffer;
		}
		
		if (pSendDesc)
			delete pSendDesc;
	}
	else
	{
		nReceivesPerAck = ((int*)pRecvDesc->Data[0].Data.Address)[0];

		// Re-post the receive descriptor immediately
		pRecvDesc->Control.Control = VIP_CONTROL_OP_SENDRECV;
		pRecvDesc->Control.Length = g_viMTU;
		pRecvDesc->Control.SegCount = 1;
		pRecvDesc->Control.Reserved = 0;
		pRecvDesc->Data[0].Length = g_viMTU;
		pRecvDesc->Data[0].Handle = mhReceive;
		dwStatus = VipPostRecv(g_hConnectToVi, pRecvDesc, mhReceive);
		if (!AssertSuccess(dwStatus, "ConnectViTo:VipPostRecv failed", pRecvDesc))
			return 0;
		
		// Store VI connection information in the proctable
		VI_Info *vinfo = &g_pProcTable[nRemoteRank].vinfo;
		memcpy(&vinfo->descriminator, ViDescriminator, nViDescriminator_len);
		vinfo->descriminator_len = nViDescriminator_len;
		vinfo->hNic = g_hViNic;
		vinfo->hVi = g_hConnectToVi;
		//vinfo->localbuf;
		vinfo->mhReceive = mhReceive;
		vinfo->mhSend = mhSend;
		vinfo->nCurSendIndex = 0;
		vinfo->nNumReceived = 0;
		vinfo->nNumRecvDescriptors = nNumRecvDescriptors;
		vinfo->nNumSendDescriptors = nNumSendDescriptors;
		vinfo->nNumSent = 0;
		vinfo->nPostedSends = 0;
		vinfo->nReceivesPerAck = nReceivesPerAck;
		vinfo->nSendsPerAck = nSendsPerAck;
		vinfo->nSendAcked = 0;
		vinfo->nSequenceNumberReceive = 0;
		vinfo->nSequenceNumberSend = 0;
		vinfo->pDesc = pDesc;
		//vinfo->pLocalAddress = (VIP_NET_ADDRESS*)vinfo->localbuf;
		vinfo->pRecvDesc = pRecvDesc;
		vinfo->pRemoteAddress = (VIP_NET_ADDRESS*)vinfo->remotebuf;
		vinfo->pSendDesc = pSendDesc;
		vinfo->pSendDescriptorBuffer = pSendDescriptorBuffer;
		vinfo->pReceiveDescriptorBuffer = pReceiveDescriptorBuffer;
		//vinfo->remotebuf;
		//vinfo->Vi_LocalAttribs = Vi_LocalAttribs;
		vinfo->Vi_RemoteAttribs = Vi_RemoteAttribs;
		initlock(&vinfo->lock);
		// Setting the data to valid must be last and the compiler or chip must execute this instruction last too.
		vinfo->valid = 1;

		// Increase the completion queue size every time a new connection is made
		g_nNumCQEntries += CQ_ENTRIES_INCREMENT;
		dwStatus = VipResizeCQ(g_hViCQ, g_nNumCQEntries);
	}

	// Reset g_hConnectToVi before setting the worker gate so the worker thread will not accidentally match it again
	g_hConnectToVi = NULL;
	g_nConnectGate = 0;
	if (!g_bViSingleThreaded)
		g_nWorkerGate = 1;

	return 1;
}

// Function name	: ViListenThread
// Description	    : 
// Return type		: void 
void ViListenThread()
{
	VIP_RETURN			dwStatus;
	VIP_VI_ATTRIBUTES	Vi_RemoteAttribs;
	VIP_DESCRIPTOR		*pRecvDesc, **pSendDesc, *pDesc;
	VIP_MEM_HANDLE		mhSend, mhReceive;
	void				*pSendDescriptorBuffer, *pReceiveDescriptorBuffer;
	char				localbuf[40], remotebuf[40];
	VIP_NET_ADDRESS		*pLocalAddress;
	VIP_NET_ADDRESS		*pRemoteAddress;
	int					nNumRecvDescriptors=32, nNumSendDescriptors=30;
	void				*vp;
	int					nRemoteRank;
	int					nSendsPerAck, nReceivesPerAck;

	while (true)
	{
		/////////////////////
		// Setup a VI
		pLocalAddress  = (VIP_NET_ADDRESS*)localbuf;
		pRemoteAddress = (VIP_NET_ADDRESS*)remotebuf;
		pLocalAddress->HostAddressLen = ADDR_LEN;
		vp = pLocalAddress->HostAddress + ADDR_LEN;
		pLocalAddress->DiscriminatorLen = g_nViDescriminator_len;
		memcpy(vp, g_ViDescriminator, g_nViDescriminator_len);
	
		// Save the descriminator for automatic reconnect
		//descriminator = (unsigned char*)vp;
		//descriminator_len = descriminator_length;

		if (nNumSendDescriptors & 0x1)
			nNumSendDescriptors++; // must be even

		pSendDesc = new VIP_DESCRIPTOR*[nNumSendDescriptors];

		//dwStatus = VipCreateVi(g_hViNic, &default_vi_attribs, g_hViCQ, g_hViCQ, &g_hListenThreadVi);
		dwStatus = VipCreateVi(g_hViNic, &default_vi_attribs, NULL, g_hViCQ, &g_hListenThreadVi);
		if (!AssertSuccess(dwStatus, "can't create VI"))
		{
			nt_error("Error", 1);
			return;
		}

		// Reserve memory for descriptors, for sending and receiving data
		pRecvDesc = get_descriptors(g_hViNic, nNumRecvDescriptors, g_viMTU, &mhReceive, &pReceiveDescriptorBuffer);
		pSendDesc[0] = get_descriptors(g_hViNic, nNumSendDescriptors, g_viMTU, &mhSend, &pSendDescriptorBuffer);
		pDesc = (VIP_DESCRIPTOR*)(pSendDesc[0]->Control.Next.Address);

		for (int i=1; i<nNumSendDescriptors; i++)
		{
			pSendDesc[i] = pDesc;
			pDesc = (VIP_DESCRIPTOR*)(pDesc->Control.Next.Address);
		}

		// Post the receives immediately
		VIP_DESCRIPTOR *pTemp2, *pTemp = pRecvDesc;
		while (pTemp)
		{
			pTemp2 = pTemp;
			// Advance to the next descriptor before calling PostRecv because PostRecv modifies the Address field
			pTemp = (VIP_DESCRIPTOR*)(pTemp->Control.Next.Address);
			dwStatus = VipPostRecv(g_hListenThreadVi, pTemp2, mhReceive);
			if (!AssertSuccess(dwStatus, "ViListenThread:can't post receive", pTemp2))
			{
				nt_error("Error", 1);
				return;
			}
		}

		////////////////////////////
		// Wait for a connection
		VIP_CONN_HANDLE conn;

		dwStatus = VipConnectWait(g_hViNic, pLocalAddress, VIP_INFINITE, pRemoteAddress, &Vi_RemoteAttribs, &conn);
		if (!AssertSuccess(dwStatus, "ViListenThread:failed waiting for connection"))
		{
			if (g_bViClosing)
			{
				// Clean up local VI structures
				// ...
				return;
			}
			nt_error("Error", 1);
			return;
		}

		dwStatus = VipConnectAccept(conn, g_hListenThreadVi);
		if (!AssertSuccess(dwStatus, "can't accept connection"))
		{
			nt_error("Error", 1);
			return;
		}

		// Receive nRemoteRank and nReceivesPerAck
		if (g_bViSingleThreaded)
		{
			while (g_nListenGate == 0)
				ViWorkerThread(0);
		}
		else
		{
			while (g_nListenGate == 0)
				Sleep(0);
		}
		while ( (dwStatus = VipRecvDone(g_hListenThreadVi, &pRecvDesc)) == VIP_NOT_DONE)
			Sleep(1);
		if (!AssertSuccess(dwStatus, "ViListenThread:VipRecvDone failed", pRecvDesc))
		{
			nt_error("Error", 1);
			return;
		}
		nRemoteRank = ((int*)pRecvDesc->Data[0].Data.Address)[0];
		nReceivesPerAck = ((int*)pRecvDesc->Data[0].Data.Address)[1];
		if (nRemoteRank < 0 || nRemoteRank >= g_nNproc)
			MakeErrMsg(1, "Invalid rank received on new VI: %d", nRemoteRank);

		// Re-post the receive descriptor
		pRecvDesc->Control.Control = VIP_CONTROL_OP_SENDRECV;
		pRecvDesc->Control.Length = g_viMTU;
		pRecvDesc->Control.SegCount = 1;
		pRecvDesc->Control.Reserved = 0;
		pRecvDesc->Data[0].Length = g_viMTU;
		pRecvDesc->Data[0].Handle = mhReceive;
		dwStatus = VipPostRecv(g_hListenThreadVi, pRecvDesc, mhReceive);
		if (!AssertSuccess(dwStatus, "ViListenThread:VipPostRecv failed", pRecvDesc))
		{
			nt_error("Error", 1);
			return;
		}

		// Set the user data for this connection to be the rank of the remote process
		VipSetUserData(g_hListenThreadVi, (VIP_PVOID)nRemoteRank);

		// Insert VI into proc table
		bool bSetupConnection;
#ifdef USE_VC6_HEADERS
		if (InterlockedCompareExchange((void**)&g_pProcTable[nRemoteRank].vinfo.hVi, (void*)g_hListenThreadVi, (void*)NULL) == NULL)
#else
		if (InterlockedCompareExchange((LONG volatile *)&g_pProcTable[nRemoteRank].vinfo.hVi, (LONG)g_hListenThreadVi, (LONG)NULL) == NULL)
#endif
			bSetupConnection = true;
		else
		{
			// Two connections have been made simultaneously
			// One must be left up and the other must be disconnected
			if (nRemoteRank > g_nIproc)
			{
				// If the remote rank is higer, reject the new connection and keep the existing
				bSetupConnection = false;
				// Send ack=0
				pSendDesc[0]->Control.Control = VIP_CONTROL_OP_SENDRECV | VIP_CONTROL_IMMEDIATE;
				pSendDesc[0]->Control.Length = 0;
				pSendDesc[0]->Control.SegCount = 0;
				pSendDesc[0]->Control.Reserved = 0;
				pSendDesc[0]->Control.ImmediateData = 0; // Ack stored in immediate data
				dwStatus = VipPostSend(g_hListenThreadVi, pSendDesc[0], mhSend);
				if (!AssertSuccess(dwStatus, "ViListenThread:VipPostSend failed", pSendDesc[0]))
				{
					nt_error("Error", 1);
					return;
				}
				dwStatus = VipSendWait(g_hListenThreadVi, VITIMEOUT, &pDesc);
				if (!AssertSuccess(dwStatus, "ViListenThread:VipSendWait failed", pDesc))
				{
					nt_error("Error", 1);
					return;
				}
			}
			else
			{
				// If the remote rank is lower, destroy the existing connection and accept the new
				CloseVi(&g_pProcTable[nRemoteRank].vinfo);
				bSetupConnection = true;
			}
		}
		if (bSetupConnection)
		{
			nSendsPerAck = nNumSendDescriptors / 2;
			// Send ack=1
			pSendDesc[0]->Control.Control = VIP_CONTROL_OP_SENDRECV | VIP_CONTROL_IMMEDIATE;
			pSendDesc[0]->Control.Length = sizeof(int);
			pSendDesc[0]->Control.SegCount = 1;
			pSendDesc[0]->Control.Reserved = 0;
			pSendDesc[0]->Control.ImmediateData = 1; // Ack stored in immediate data
			pSendDesc[0]->Data[0].Length = sizeof(int);
			pSendDesc[0]->Data[0].Handle = mhSend;
			((int*)pSendDesc[0]->Data[0].Data.Address)[0] = nSendsPerAck;
			dwStatus = VipPostSend(g_hListenThreadVi, pSendDesc[0], mhSend);
			if (!AssertSuccess(dwStatus, "ViListenThread:VipPostSend failed", pSendDesc[0]))
			{
				nt_error("Error", 1);
				return;
			}
			dwStatus = VipSendWait(g_hListenThreadVi, VITIMEOUT, &pDesc);
			if (!AssertSuccess(dwStatus, "ViListenThread:VipSendWait failed", pDesc))
			{
				nt_error("Error", 1);
				return;
			}

			// Store VI connection information in the proctable
			VI_Info *vinfo = &g_pProcTable[nRemoteRank].vinfo;
			memcpy(&vinfo->descriminator, g_ViDescriminator, g_nViDescriminator_len);
			vinfo->descriminator_len = g_nViDescriminator_len;
			vinfo->hNic = g_hViNic;
			vinfo->hVi = g_hListenThreadVi;
			//vinfo->localbuf;
			vinfo->mhReceive = mhReceive;
			vinfo->mhSend = mhSend;
			vinfo->nCurSendIndex = 0;
			vinfo->nNumReceived = 0;
			vinfo->nNumRecvDescriptors = nNumRecvDescriptors;
			vinfo->nNumSendDescriptors = nNumSendDescriptors;
			vinfo->nNumSent = 0;
			vinfo->nPostedSends = 0;
			vinfo->nReceivesPerAck = nReceivesPerAck;
			vinfo->nSendsPerAck = nSendsPerAck;
			vinfo->nSendAcked = 0;
			vinfo->nSequenceNumberReceive = 0;
			vinfo->nSequenceNumberSend = 0;
			//vinfo->pDataBuffer = pDataBuffer;
			vinfo->pDesc = pDesc;
			//vinfo->pLocalAddress = (VIP_NET_ADDRESS*)vinfo->localbuf;
			vinfo->pRecvDesc = pRecvDesc;
			vinfo->pRemoteAddress = (VIP_NET_ADDRESS*)vinfo->remotebuf;
			vinfo->pSendDesc = pSendDesc;
			vinfo->pSendDescriptorBuffer = pSendDescriptorBuffer;
			vinfo->pReceiveDescriptorBuffer = pReceiveDescriptorBuffer;
			//vinfo->remotebuf;
			//vinfo->Vi_LocalAttribs = Vi_LocalAttribs;
			vinfo->Vi_RemoteAttribs = Vi_RemoteAttribs;
			initlock(&vinfo->lock);
			// Setting the data to valid must be last
			vinfo->valid = 1;

			// Increase the completion queue size every time a new connection is made
			g_nNumCQEntries += CQ_ENTRIES_INCREMENT;
			dwStatus = VipResizeCQ(g_hViCQ, g_nNumCQEntries);
		}
		else
		{
			//printf("VI already in proctable %d\n", nRemoteRank);fflush(stdout);
		}

		g_hListenThreadVi = NULL;
		g_nListenGate = 0;
		if (!g_bViSingleThreaded)
			g_nWorkerGate = 1;
	}
}

// Function name	: HashViPointer
// Description	    : 
// Return type		: int 
// Argument         : VIP_VI_HANDLE p
int HashViPointer(VIP_VI_HANDLE p)
{
	int index;
	if (p == NULL)
		nt_error("Hashing NULL VI handle", 1);
	
	if (VipGetUserData != NULL)
	{
		index = (int)VipGetUserData(p);
		if (g_pProcTable[index].vinfo.hVi == p)
			return index;
	}
	else
	{
		// For now, just search for the handle
		for (int i=0; i<g_nNproc; i++)
		{
			if ( g_pProcTable[i].via && (g_pProcTable[i].vinfo.hVi == p) )
				return i;
		}
	}

	if (p == g_hListenThreadVi)
	{
		g_nListenGate = 1;
		if (g_bViSingleThreaded)
			return -1;
		while (g_nWorkerGate == 0)
			Sleep(0);
		g_nWorkerGate = 0;
	}
	else
	if (p == g_hConnectToVi)
	{
		g_nConnectGate = 1;
		if (g_bViSingleThreaded)
			return -1;
		while (g_nWorkerGate == 0)
			Sleep(0);
		g_nWorkerGate = 0;
	}
	else
		MakeErrMsg(1, "HashViPointer: VI_HANDLE(%x) not found in g_pProcTable", p);
	return -1;
}

// Function name	: ViWorkerThread
// Description	    : 
// Return type		: void 
int ViWorkerThread(int bRepeating)
{
	VIP_VI_HANDLE hVi;
	VIP_BOOLEAN bRecvQ;
	VIP_RETURN dwStatus;
	int index;
	VI_Info *vinfo;
	NT_Message *message;

	do
	{
		if (!bRepeating)
		{
			// Poll once and return if no packet is available
			if ((dwStatus = VipCQDone(g_hViCQ, &hVi, &bRecvQ)) == VIP_NOT_DONE)
				return 0;
			if (!AssertSuccess(dwStatus, "ViWorkerThread:VipCQDone failed"))
			{
				if (g_bViClosing)
					return 0;
				nt_error("Error", 1);
				return 0;
			}
		}
		else
		{
			// Wait for a packet by either polling or a wait function
			if (g_bViUsePolling)
			{
				while ((dwStatus = VipCQDone(g_hViCQ, &hVi, &bRecvQ)) == VIP_NOT_DONE)
					Sleep(0);
				if (!AssertSuccess(dwStatus, "ViWorkerThread:VipCQDone failed"))
				{
					if (g_bViClosing)
						return 0;
					nt_error("Error", 1);
					return 0;
				}
			}
			else
			{
				dwStatus = VipCQWait(g_hViCQ, VIP_INFINITE, &hVi, &bRecvQ);
				if (!AssertSuccess(dwStatus, "ViWorkerThread:VipCQWait failed"))
				{
					if (g_bViClosing)
						return 0;
					nt_error("Error", 1);
					return 0;
				}
			}
		}

		index = HashViPointer(hVi);
		if (index == -1)
		{
			//printf("HashViPointer returned -1\n");fflush(stdout);
			continue;
		}
		vinfo = &g_pProcTable[index].vinfo;

		if (bRecvQ)
		{
			// Packet ready in the receive queue
			while ( (dwStatus = VipRecvDone(vinfo->hVi, &vinfo->pRecvDesc)) == VIP_NOT_DONE )
				Sleep(0);
			if (!AssertSuccess(dwStatus, "ViWorkerThread:VipRecvDone failed", vinfo->pRecvDesc))
			{
				if (g_bViClosing)
					return 0;
				nt_error("Error", 1);
				return 0;
			}
			// Zero length messages are assumed to be ack packets.
			// In the future, I will probably check the immediate data to determine the packet type
			if (vinfo->pRecvDesc->Control.Length == 0)
			{
				// Ack packet received
				InterlockedIncrement(&vinfo->nSendAcked);
				vinfo->nSequenceNumberReceive = vinfo->pRecvDesc->Control.ImmediateData;
			}
			else
			{
				// Data packet received
				int datalen;
				message = &g_pProcTable[index].msg;
				if (message->state == NT_MSG_READING_TAG)
				{
					// This is the first packet in a message.
					// Peel off the tag, length, and as much of the data as is available
					message->tag = ((int*)(vinfo->pRecvDesc->Data[0].Data.Address))[0];
					message->length = ((int*)(vinfo->pRecvDesc->Data[0].Data.Address))[1];
					message->buffer = g_MsgQueue.GetBufferToFill(message->tag, message->length, index, &message->pElement);
					datalen = vinfo->pRecvDesc->Control.Length - (2 * sizeof(int));
					if (datalen > 0)
					{
						memcpy(
							message->buffer, 
							&((int*)(vinfo->pRecvDesc->Data[0].Data.Address))[2], 
							datalen);
						message->nRemaining = message->length - datalen;
					}
					if (message->nRemaining)
						message->state = NT_MSG_READING_BUFFER;
					else
					{
						message->state = NT_MSG_READING_TAG;
						g_MsgQueue.SetElementEvent(message->pElement);
					}
				}
				else
				{
					// This is next packet containing only data for the current message
					datalen = vinfo->pRecvDesc->Control.Length;
					memcpy(
						&(((char*)message->buffer)[message->length - message->nRemaining]),
						vinfo->pRecvDesc->Data[0].Data.Address,
						datalen);
					message->nRemaining -= datalen;
					if (message->nRemaining == 0)
					{
						message->state = NT_MSG_READING_TAG;
						g_MsgQueue.SetElementEvent(message->pElement);
					}
				}
			}

			// Re-post the receive
			vinfo->pRecvDesc->Control.Control = VIP_CONTROL_OP_SENDRECV;
			vinfo->pRecvDesc->Control.Length = g_viMTU;
			vinfo->pRecvDesc->Control.SegCount = 1;
			vinfo->pRecvDesc->Control.Reserved = 0;
			vinfo->pRecvDesc->Data[0].Length = g_viMTU;
			vinfo->pRecvDesc->Data[0].Handle = vinfo->mhReceive;
			dwStatus = VipPostRecv(vinfo->hVi, vinfo->pRecvDesc, vinfo->mhReceive);
			if (!AssertSuccess(dwStatus, "ViWorkerThread:VipPostRecv failed", vinfo->pRecvDesc))
			{
				nt_error("Error", 1);
				return 0;
			}
			
			// Send ack if necessary
			vinfo->nNumReceived++;
			if (vinfo->nNumReceived % vinfo->nReceivesPerAck == 0)
				ViSendAck(vinfo);
		}
		else
		{
			// Packet ready in the send queue
			printf("There shouldn't be any send completion messages\n");fflush(stdout);
		}
	}while (bRepeating);
	return 1;
}

// Function name	: PollViQueue
// Description	    : 
// Return type		: void 
void PollViQueue()
{
	//*
	if (!ViWorkerThread(0))
		Sleep(0);
	/*/
	// My trials show that polling more than once before sleeping only 
	// decreases performance.The lock function shows the exact opposite.  
	// Maybe I need to slim down the ViWorkerThread to return faster.
	for (int i=0; i<10; i++)
	{
		if (ViWorkerThread(0))
			return;
	}
	Sleep(0);
	//*/
}

// Function name	: LoadViFunctions
// Description	    : 
// Return type		: bool 
bool LoadViFunctions()
{
	HMODULE hViLib;
	char pszLibrary[1024];
	
	if (!GetEnvironmentVariable("MPICH_VI_LIB", pszLibrary, 1024))
		strcpy(pszLibrary, "vipl.dll");

	// First initialize everythting to NULL
	VipOpenNic = NULL;
	VipCloseNic = NULL;
	VipQueryNic = NULL;
	VipRegisterMem = NULL;
	VipDeregisterMem = NULL;
	VipQueryMem = NULL;
	VipSetMemAttributes = NULL;
	VipErrorCallback = NULL;
	VipQuerySystemManagementInfo = NULL;
	VipCreatePtag = NULL;
	VipDestroyPtag = NULL;
	VipCreateVi = NULL;
	VipDestroyVi = NULL;
	VipQueryVi = NULL;
	VipSetViAttributes = NULL;
	VipPostSend = NULL;
	VipSendDone = NULL;
	VipSendWait = NULL;
	VipSendNotify = NULL;
	VipPostRecv = NULL;
	VipRecvDone = NULL;
	VipRecvWait = NULL;
	VipRecvNotify = NULL;
	VipConnectWait = NULL;
	VipConnectAccept = NULL;
	VipConnectReject = NULL;
	VipConnectRequest = NULL;
	VipDisconnect = NULL;
	VipCreateCQ = NULL;
	VipDestroyCQ = NULL;
	VipResizeCQ = NULL;
	VipCQDone = NULL;
	VipCQWait = NULL;
	VipCQNotify = NULL;
	VipNSInit = NULL;
	VipNSGetHostByName = NULL;
	VipNSGetHostByAddr = NULL;
	VipNSShutdown = NULL;
	VipConnectPeerRequest = NULL;
	VipConnectPeerDone = NULL;
	VipConnectPeerWait = NULL;
	VipAddTagCQ = NULL;
	VipRemoveTagCQ = NULL;
	VipPostDeferredSends = NULL;

	VipGetUserData = NULL;
	VipSetUserData = NULL;

	hViLib = LoadLibrary(pszLibrary);

	if (hViLib == NULL)
		return false;

	// Add code to check if the return values are NULL ...
	VipOpenNic = (VIP_RETURN (VI_CALL *)(const char *, void **))GetProcAddress(hViLib, "VipOpenNic");
	if (VipOpenNic == NULL) DPRINTF(("VipOpenNic == NULL\n"));
	VipCloseNic = (VIP_RETURN (VI_CALL *)(void *))GetProcAddress(hViLib, "VipCloseNic");
	if (VipCloseNic == NULL) DPRINTF(("VipCloseNic == NULL\n"));
	VipQueryNic = (VIP_RETURN (VI_CALL *)(void *, struct _VIP_NIC_ATTRIBUTES *))GetProcAddress(hViLib, "VipQueryNic");
	if (VipQueryNic == NULL) DPRINTF(("VipQueryNic == NULL\n"));
	VipRegisterMem = (VIP_RETURN (VI_CALL *)(void *, void *, unsigned long, struct _VIP_MEM_ATTRIBUTES *, unsigned __int32 *))GetProcAddress(hViLib, "VipRegisterMem");
	if (VipRegisterMem == NULL) DPRINTF(("VipRegisterMem == NULL\n"));
	VipDeregisterMem = (VIP_RETURN (VI_CALL *)(void *, void *, unsigned __int32))GetProcAddress(hViLib, "VipDeregisterMem");
	if (VipDeregisterMem == NULL) DPRINTF(("VipDeregisterMem == NULL\n"));
	VipQueryMem = (VIP_RETURN (VI_CALL *)(void *, void *, unsigned __int32, struct _VIP_MEM_ATTRIBUTES *))GetProcAddress(hViLib, "VipQueryMem");
	if (VipQueryMem == NULL) DPRINTF(("VipQueryMem == NULL\n"));
	VipSetMemAttributes = (VIP_RETURN (VI_CALL *)(void *, void *, unsigned __int32, struct _VIP_MEM_ATTRIBUTES *))GetProcAddress(hViLib, "VipSetMemAttributes");
	if (VipSetMemAttributes == NULL) DPRINTF(("VipSetMemAttributes == NULL\n"));
	VipErrorCallback = (VIP_RETURN (VI_CALL *)(void *, void *, void (VI_CALL *)(void *, struct _VIP_ERROR_DESCRIPTOR *)))GetProcAddress(hViLib, "VipErrorCallback");
	if (VipErrorCallback == NULL) DPRINTF(("VipErrorCallback == NULL\n"));
	VipQuerySystemManagementInfo = (VIP_RETURN (VI_CALL *)(void *, unsigned long, void *))GetProcAddress(hViLib, "VipQuerySystemManagementInfo");
	if (VipQuerySystemManagementInfo == NULL) DPRINTF(("VipQuerySystemManagementInfo == NULL\n"));
	VipCreatePtag = (VIP_RETURN (VI_CALL *)(void *, void **))GetProcAddress(hViLib, "VipCreatePtag");
	if (VipCreatePtag == NULL) DPRINTF(("VipCreatePtag == NULL\n"));
	VipDestroyPtag = (VIP_RETURN (VI_CALL *)(void *, void *))GetProcAddress(hViLib, "VipDestroyPtag");
	if (VipDestroyPtag == NULL) DPRINTF(("VipDestroyPtag == NULL\n"));
	VipCreateVi = (VIP_RETURN (VI_CALL *)(void *, struct _VIP_VI_ATTRIBUTES *, void *, void *, void **))GetProcAddress(hViLib, "VipCreateVi");
	if (VipCreateVi == NULL) DPRINTF(("VipCreateVi == NULL\n"));
	VipDestroyVi = (VIP_RETURN (VI_CALL *)(void *))GetProcAddress(hViLib, "VipDestroyVi");
	if (VipDestroyVi == NULL) DPRINTF(("VipDestroyVi == NULL\n"));
	VipQueryVi = (VIP_RETURN (VI_CALL *)(void *, VIP_VI_STATE *, struct _VIP_VI_ATTRIBUTES *, int *, int *))GetProcAddress(hViLib, "VipQueryVi");
	if (VipQueryVi == NULL) DPRINTF(("VipQueryVi == NULL\n"));
	VipSetViAttributes = (VIP_RETURN (VI_CALL *)(void *, struct _VIP_VI_ATTRIBUTES *))GetProcAddress(hViLib, "VipSetViAttributes");
	if (VipSetViAttributes == NULL) DPRINTF(("VipSetViAttributes == NULL\n"));
	VipPostSend = (VIP_RETURN (VI_CALL *)(void *, struct _VIP_DESCRIPTOR *, unsigned __int32))GetProcAddress(hViLib, "VipPostSend");
	if (VipPostSend == NULL) DPRINTF(("VipPostSend == NULL\n"));
	VipSendDone = (VIP_RETURN (VI_CALL *)(void *, struct _VIP_DESCRIPTOR **))GetProcAddress(hViLib, "VipSendDone");
	if (VipSendDone == NULL) DPRINTF(("VipSendDone == NULL\n"));
	VipSendWait = (VIP_RETURN (VI_CALL *)(void *, unsigned long, struct _VIP_DESCRIPTOR **))GetProcAddress(hViLib, "VipSendWait");
	if (VipSendWait == NULL) DPRINTF(("VipSendWait == NULL\n"));
	VipSendNotify = (VIP_RETURN (VI_CALL *)(void *, void *, void (VI_CALL *)(void *, void *, void *, struct _VIP_DESCRIPTOR *)))GetProcAddress(hViLib, "VipSendNotify");
	if (VipSendNotify == NULL) DPRINTF(("VipSendNotify == NULL\n"));
	VipPostRecv = (VIP_RETURN (VI_CALL *)(void *, struct _VIP_DESCRIPTOR *, unsigned __int32))GetProcAddress(hViLib, "VipPostRecv");
	if (VipPostRecv == NULL) DPRINTF(("VipPostRecv == NULL\n"));
	VipRecvDone = (VIP_RETURN (VI_CALL *)(void *, struct _VIP_DESCRIPTOR **))GetProcAddress(hViLib, "VipRecvDone");
	if (VipRecvDone == NULL) DPRINTF(("VipRecvDone == NULL\n"));
	VipRecvWait = (VIP_RETURN (VI_CALL *)(void *, unsigned long, struct _VIP_DESCRIPTOR **))GetProcAddress(hViLib, "VipRecvWait");
	if (VipRecvWait == NULL) DPRINTF(("VipRecvWait == NULL\n"));
	VipRecvNotify = (VIP_RETURN (VI_CALL *)(void *, void *, void (_cdecl *)(void *, void *, void *, struct _VIP_DESCRIPTOR *)))GetProcAddress(hViLib, "VipRecvNotify");
	if (VipRecvNotify == NULL) DPRINTF(("VipRecvNotify == NULL\n"));
	VipConnectWait = (VIP_RETURN (VI_CALL *)(void *, struct _VIP_NET_ADDRESS *, unsigned long, struct _VIP_NET_ADDRESS *, struct _VIP_VI_ATTRIBUTES *, void **))GetProcAddress(hViLib, "VipConnectWait");
	if (VipConnectWait == NULL) DPRINTF(("VipConnectWait == NULL\n"));
	VipConnectAccept = (VIP_RETURN (VI_CALL *)(void *, void *))GetProcAddress(hViLib, "VipConnectAccept");
	if (VipConnectAccept == NULL) DPRINTF(("VipConnectAccept == NULL\n"));
	VipConnectReject = (VIP_RETURN (VI_CALL *)(void *))GetProcAddress(hViLib, "VipConnectReject");
	if (VipConnectReject == NULL) DPRINTF(("VipConnectReject == NULL\n"));
	VipConnectRequest = (VIP_RETURN (VI_CALL *)(void *, struct _VIP_NET_ADDRESS *, struct _VIP_NET_ADDRESS *, unsigned long, struct _VIP_VI_ATTRIBUTES *))GetProcAddress(hViLib, "VipConnectRequest");
	if (VipConnectRequest == NULL) DPRINTF(("VipConnectRequest == NULL\n"));
	VipDisconnect = (VIP_RETURN (VI_CALL *)(void *))GetProcAddress(hViLib, "VipDisconnect");
	if (VipDisconnect == NULL) DPRINTF(("VipDisconnect == NULL\n"));
	VipCreateCQ = (VIP_RETURN (VI_CALL *)(void *, unsigned long, void **))GetProcAddress(hViLib, "VipCreateCQ");
	if (VipCreateCQ == NULL) DPRINTF(("VipCreateCQ == NULL\n"));
	VipDestroyCQ = (VIP_RETURN (VI_CALL *)(void *))GetProcAddress(hViLib, "VipDestroyCQ");
	if (VipDestroyCQ == NULL) DPRINTF(("VipDestroyCQ == NULL\n"));
	VipResizeCQ = (VIP_RETURN (VI_CALL *)(void *, unsigned long))GetProcAddress(hViLib, "VipResizeCQ");
	if (VipResizeCQ == NULL) DPRINTF(("VipResizeCQ == NULL\n"));
	VipCQDone = (VIP_RETURN (VI_CALL *)(void *, void **, int *))GetProcAddress(hViLib, "VipCQDone");
	if (VipCQDone == NULL) DPRINTF(("VipCQDone == NULL\n"));
	VipCQWait = (VIP_RETURN (VI_CALL *)(void *, unsigned long, void **, int *))GetProcAddress(hViLib, "VipCQWait");
	if (VipCQWait == NULL) DPRINTF(("VipCQWait == NULL\n"));
	VipCQNotify = (VIP_RETURN (VI_CALL *)(void *, void *, void (VI_CALL *)(void *, void *, void *, int)))GetProcAddress(hViLib, "VipCQNotify");
	if (VipCQNotify == NULL) DPRINTF(("VipCQNotify == NULL\n"));
	VipNSInit = (VIP_RETURN (VI_CALL *)(void *, void *))GetProcAddress(hViLib, "VipNSInit");
	if (VipNSInit == NULL) DPRINTF(("VipNSInit == NULL\n"));
	VipNSGetHostByName = (VIP_RETURN (VI_CALL *)(void *, char *, struct _VIP_NET_ADDRESS *, unsigned long))GetProcAddress(hViLib, "VipNSGetHostByName");
	if (VipNSGetHostByName == NULL) DPRINTF(("VipNSGetHostByName == NULL\n"));
	VipNSGetHostByAddr = (VIP_RETURN (VI_CALL *)(void *, struct _VIP_NET_ADDRESS *, char *, unsigned long *))GetProcAddress(hViLib, "VipNSGetHostByAddr");
	if (VipNSGetHostByAddr == NULL) DPRINTF(("VipNSGetHostByAddr == NULL\n"));
	VipNSShutdown = (VIP_RETURN (VI_CALL *)(void *))GetProcAddress(hViLib, "VipNSShutdown");
	if (VipNSShutdown == NULL) DPRINTF(("VipNSShutdown == NULL\n"));
	VipConnectPeerRequest = (VIP_RETURN (VI_CALL *)(void *, struct _VIP_NET_ADDRESS *, struct _VIP_NET_ADDRESS *, unsigned long))GetProcAddress(hViLib, "VipConnectPeerRequest");
	if (VipConnectPeerRequest == NULL) DPRINTF(("VipConnectPeerRequest == NULL\n"));
	VipConnectPeerDone = (VIP_RETURN (VI_CALL *)(void *, struct _VIP_VI_ATTRIBUTES *))GetProcAddress(hViLib, "VipConnectPeerDone");
	if (VipConnectPeerDone == NULL) DPRINTF(("VipConnectPeerDone == NULL\n"));
	VipConnectPeerWait = (VIP_RETURN (VI_CALL *)(void *, struct _VIP_VI_ATTRIBUTES *))GetProcAddress(hViLib, "VipConnectPeerWait");
	if (VipConnectPeerWait == NULL) DPRINTF(("VipConnectPeerWait == NULL\n"));
	VipAddTagCQ = (VIP_RETURN (VI_CALL *)(void *, void **, unsigned long, unsigned long))GetProcAddress(hViLib, "VipAddTagCQ");
	if (VipAddTagCQ == NULL) DPRINTF(("VipAddTagCQ == NULL\n"));
	VipRemoveTagCQ = (VIP_RETURN (VI_CALL *)(void *, void *, unsigned long))GetProcAddress(hViLib, "VipRemoveTagCQ");
	if (VipRemoveTagCQ == NULL) DPRINTF(("VipRemoveTagCQ == NULL\n"));
	VipPostDeferredSends = (VIP_RETURN (VI_CALL *)(void *, int, int *))GetProcAddress(hViLib, "VipPostDeferredSends");
	if (VipPostDeferredSends == NULL) DPRINTF(("VipPostDeferredSends == NULL\n"));

	// Non-standard VIA calls
	// Giganet
	VipGetUserData = (void * (VI_CALL *)(void *))GetProcAddress(hViLib, "VipGetUserData");
	if (VipGetUserData == NULL) DPRINTF(("VipGetUserData == NULL\n"));
	VipSetUserData = (void (VI_CALL *)(void *, void *))GetProcAddress(hViLib, "VipSetUserData");
	if (VipSetUserData == NULL) DPRINTF(("VipSetUserData == NULL\n"));
	// Servernet
	// GWizz

	return true;
}

// Function name	: InitVI
// Description	    : 
// Return type		: void 
bool InitVI()
{
	DWORD dwThreadID;
	VIP_RETURN dwStatus;
	char pszTemp[1024];
	VIP_NIC_ATTRIBUTES nicAttribs;
	int nCount=0, *pMembers = NULL;

	try{
	if (!LoadViFunctions())
		return false;
	}catch(...)
	{
		nt_error("Exception thrown in LoadViFunctions caught in InitVi", 1);
		return false;
	}

	// Determine whether to use polling methods or not
	if (GetEnvironmentVariable("MPICH_VI_USE_POLLING", pszTemp, 100))
		g_bViUsePolling = true;

	// Determine which processes this process can reach by VI connections
	if (GetEnvironmentVariable("MPICH_VI_CLICKS", pszTemp, 100) == 0 && GetEnvironmentVariable("MPICH_VI_CLIQUES", pszTemp, 100) == 0)
		return false; // If none, then there is no need to continue
	if (ParseCliques(pszTemp, g_nIproc, g_nNproc, &nCount, &pMembers))
	{
		nt_error("Unable to parse the VI cliques", 1);
		return false;
	}
	for (int i=0; i<nCount; i++)
	{
		if ( (pMembers[i] >= 0) && (pMembers[i] < g_nNproc) )
		{
			g_pProcTable[pMembers[i]].via = 1;
			g_pProcTable[pMembers[i]].vinfo.hVi = NULL;
			g_pProcTable[pMembers[i]].vinfo.valid = 0;
		}
	}
	if (pMembers != NULL)
		delete pMembers;

	// Open the network interface card and save the handle
	// TODO: What if there are multiple nics?
	char pszNic[100];
	sprintf(pszNic, "%s0", g_pszNicBaseName);
	dwStatus = VipOpenNic(pszNic, &g_hViNic);
	if (!AssertSuccess(dwStatus, "InitVI:can't open nic"))
	{
		printf("VipOpenNic failed\n");fflush(stdout);
	}

	// Set the global descriminator used to accept VI connections
	sprintf((char*)g_ViDescriminator, "%s%d", g_pszJobID, g_nIproc);
	g_nViDescriminator_len = strlen((char*)g_ViDescriminator);

	// Determine and save the maximum transmission unit
	if (VipQueryNic(g_hViNic, &nicAttribs) == VIP_SUCCESS)
	{
		if (nicAttribs.MaxTransferSize < DESIRED_PACKET_LENGTH)
		{
			g_viMTU = nicAttribs.MaxTransferSize;
			default_vi_attribs.MaxTransferSize = nicAttribs.MaxTransferSize;
		}
		else
		{
			g_viMTU = DESIRED_PACKET_LENGTH;
			default_vi_attribs.MaxTransferSize = DESIRED_PACKET_LENGTH;
		}
	}

	//*
	// The code will work without the callback function but it is necessary
	// to detect catastrophic network closures, ie. a remote process dies.
	// Set the error callback function
	dwStatus = VipErrorCallback(g_hViNic, NULL, ErrorCallbackFunction);
	if (!AssertSuccess(dwStatus, "InitVI:VipErrorCallback failed"))
	{
		printf("VipErrorCallback failed\n");fflush(stdout);
	}
	//*/

	// Create a global completion queue for all VI connections to share
	dwStatus = VipCreateCQ(g_hViNic, INITIAL_NUM_CQ_ENTRIES, &g_hViCQ);
	if (!AssertSuccess(dwStatus, "InitVI:VipCreateCQ failed"))
	{
		printf("VipCreateCQ failed\n");fflush(stdout);
	}

	// Create a thread to wait for VI connections
	for (i=0; i<NT_CREATE_THREAD_RETRIES; i++)
	{
		g_hViListenThread = CreateThread(NULL, 0, (LPTHREAD_START_ROUTINE)ViListenThread, NULL, NT_THREAD_STACK_SIZE, &dwThreadID);
		if (g_hViListenThread != NULL)
			break;
		Sleep(NT_CREATE_THREAD_SLEEP_TIME);
	}
	if (g_hViListenThread == NULL)
	{
		printf("CreateThread(ViListenThread) failed: %d\n", GetLastError());fflush(stdout);
	}

	pszTemp[0] = '\0';
	GetEnvironmentVariable("MPICH_VI_SINGLETHREAD", pszTemp, 100);

	if (pszTemp[0] == '1')
	{
		// Set the poll function so the via device will run single threaded.
		g_MsgQueue.SetProgressFunction(PollViQueue);
		g_bViSingleThreaded = true;
	}
	else
	{
		// Create a worker thread to eagerly drain messages from all open VI connections
		for (i=0; i<NT_CREATE_THREAD_RETRIES; i++)
		{
			g_hViWorkerThread = CreateThread(NULL, 0, (LPTHREAD_START_ROUTINE)ViWorkerThread, (LPVOID)1, NT_THREAD_STACK_SIZE, &dwThreadID);
			if (g_hViWorkerThread != NULL)
				break;
			Sleep(NT_CREATE_THREAD_SLEEP_TIME);
		}
		if (g_hViWorkerThread == NULL)
		{
			printf("CreateThread(ViWorkerThread) failed: %d\n", GetLastError());fflush(stdout);
		}
	}
	return true;
}

// Function name	: EndVI
// Description	    : 
// Return type		: void 
void EndVI()
{
	VIP_RETURN dwStatus;

	// Remove the error callback function
	if (g_hViNic != NULL)
		VipErrorCallback(g_hViNic, 0, NULL);
	
	// Close all VI connections
	for (int i=0; i<g_nNproc; i++)
	{
		if (g_pProcTable[i].via)
			CloseVi(&g_pProcTable[i].vinfo);
	}
	
	// Destroy the completion queue
	if (g_hViCQ != NULL)
	{
		dwStatus = VipDestroyCQ(g_hViCQ);
		AssertSuccess(dwStatus, "EndFI:VipDestroyCQ failed in EndVI");
	}

	// Terminate the threads
	if (g_hViListenThread)
		TerminateThread(g_hViListenThread, 0);
	WaitForSingleObject(g_hViListenThread, 1000);
	CloseHandle(g_hViListenThread);
	if (g_hViWorkerThread)
		TerminateThread(g_hViWorkerThread, 0);
	WaitForSingleObject(g_hViWorkerThread, 1000);
	CloseHandle(g_hViWorkerThread);

	ClosedVINode *n = g_pClosedViList;
	while (n != NULL)
	{
		g_pClosedViList = n;
		n = n->pNext;
		delete g_pClosedViList;
	}
	g_pClosedViList = NULL;
}

// Function name	: NT_ViSend
// Description	    : 
// Return type		: void 
// Argument         : int type
// Argument         : void *buffer
// Argument         : unsigned int length
// Argument         : int to
void NT_ViSend(int type, void *buffer, unsigned int length, int to)
{
	if (g_pProcTable[to].vinfo.hVi == NULL)
		ConnectViTo(to);

	if (!ViSendFirstPacket(&g_pProcTable[to].vinfo, buffer, length, type))
		nt_error("ViSendFirstPacket failed", 1);
	if (length)
	{
		if (!ViSendMsg(&g_pProcTable[to].vinfo, buffer, length))
			nt_error("ViSendMsg failed", 1);
	}
	// Uncomment this if you want to guarantee that messages are out of the local buffers before send returns.
	// All this really does is slow down performance.
	//ViFlushPackets(&g_pProcTable[to].vinfo); 
}

// These numbers are experimentally generated.
// I don't know how to generate then dynamically.
#define VI_STREAM_MIN	0x1000
#define VI_STREAM_MIN_N	12
#define VI_STREAM_MAX	0x400000
#define VI_STREAM_MAX_N	22
/*
#define VI_BANDWIDTH	600.0*1048576.0
#define VI_LATENCY		0.000022
#define VI_MULTIPLIER	10.0
/*/
#define VI_BANDWIDTH	800.0*1048576.0
#define VI_LATENCY		0.000002
#define VI_MULTIPLIER	2.75
//*/
//*
// Function name	: ViSendMsg
// Description	    : 
// Return type		: bool 
// Argument         : VI_Info *vinfo
// Argument         : void *pBuffer
// Argument         : unsigned int length
//#include <math.h>
bool ViSendMsg(VI_Info *vinfo, void *pBuffer, unsigned int length)
{
	unsigned int size;

	if (length < VI_STREAM_MIN || length > VI_STREAM_MAX)
	{
		// Use a do loop so that messages of length zero are sent
		do
		{
			if (g_viMTU > length)
				size = length;
			else
				size = g_viMTU;
			
			if (!ViSendPacket(vinfo, pBuffer, size))
				return false;
			
			length = length - size;
			pBuffer = (char*)pBuffer + size;
		} while (length != 0);
	}
	else
	{
		// I can't use the math library because the fortran libraries conflict with the C libraries.
		//unsigned int max = (unsigned int)ceil((double)length / (VI_MULTIPLIER * sqrt((double)length / (VI_BANDWIDTH * VI_LATENCY))));
		// So I approximate the sqrt function with Newton's method.
		double doriginal = (double)length / (VI_BANDWIDTH * VI_LATENCY);
		double d = doriginal;
		for (int i=0; i<10; i++)
			d = (d*d + doriginal) / (2.0*d);
		unsigned int max = (unsigned int) ( (double)length / (VI_MULTIPLIER * d) );

		if (max > g_viMTU)
			max = g_viMTU;
		do
		{
			if (max > length)
				size = length;
			else
				size = max;
			
			if (!ViSendPacket(vinfo, pBuffer, size))
				return false;
			
			length = length - size;
			pBuffer = (char*)pBuffer + size;
		} while (length != 0);
	}

	return true;
}

// Function name	: ViSendFirstPacket
// Description	    : 
// Return type		: bool 
// Argument         : VI_Info *vinfo
// Argument         : void *&pBuffer
// Argument         : unsigned int &length
// Argument         : int tag
bool ViSendFirstPacket(VI_Info *vinfo, void *&pBuffer, unsigned int &length, int tag)
{
	VIP_DESCRIPTOR *pDesc;
	VIP_RETURN dwStatus;
	unsigned int size;

	// These functions must be locked because the receive thread can send an ack while the
	// main thread is sending a message.
	lock(&vinfo->lock);

	// Send tag, length, buffer in a contiguous chunk

	// When nPostedSend equals nNumSendDescriptors, there are no free descriptors available.
	// So I clean up half the posted sends every time nNumSendDescriptors have been used and
	// then wait for an ack
	if (vinfo->nPostedSends == vinfo->nNumSendDescriptors)
	{
		while (true)
		{
			if (g_bViUsePolling)
			{
				while ( (dwStatus = VipSendDone(vinfo->hVi, &pDesc)) == VIP_NOT_DONE )
					Sleep(0);
				if (!AssertSuccess(dwStatus, "ViSendFirstPacket:VipSendDone failed", pDesc))
				{
					nt_error("Error", 1);
					//if ( (!m_bConnectionLost) || (!ReEstablishConnection(VI_RECON_STATE_SEND_WAIT)) )
						return false;
				}
			}
			else
			{
				dwStatus = VipSendWait(vinfo->hVi, VITIMEOUT, &pDesc);
				if (!AssertSuccess(dwStatus, "ViSendFirstPacket:VipSendWait failed", pDesc))
				{
					nt_error("Error", 1);
					//if ( (!m_bConnectionLost) || (!ReEstablishConnection(VI_RECON_STATE_SEND_WAIT)) )
						return false;
				}
			}
			vinfo->nPostedSends--;
			vinfo->nNumSent++;
			if (vinfo->nNumSent % vinfo->nSendsPerAck == 0)
			{
				unlock(&vinfo->lock);
				ViRecvAck(vinfo);
				lock(&vinfo->lock);
				if (vinfo->nPostedSends == vinfo->nNumSendDescriptors)
				{
					if (g_bViUsePolling)
					{
						while ( (dwStatus = VipSendDone(vinfo->hVi, &pDesc)) == VIP_NOT_DONE )
							Sleep(0);
						if (!AssertSuccess(dwStatus, "ViSendPacket:VipSendDone failed", pDesc))
						{
							nt_error("Error", 1);
							//if ( (!m_bConnectionLost) || (!ReEstablishConnection(VI_RECON_STATE_SEND_WAIT)) )
							return false;
						}
					}
					else
					{
						dwStatus = VipSendWait(vinfo->hVi, VITIMEOUT, &pDesc);
						if (!AssertSuccess(dwStatus, "ViSendPacket:VipSendWait failed", pDesc))
						{
							nt_error("Error", 1);
							//if ( (!m_bConnectionLost) || (!ReEstablishConnection(VI_RECON_STATE_SEND_WAIT)) )
							return false;
						}
					}
					vinfo->nPostedSends--;
					vinfo->nNumSent++;
				}
				break;
			}
		}
	}

	// Put the tag, length and buffer in the packet
	pDesc = vinfo->pSendDesc[vinfo->nCurSendIndex];
	((unsigned int*)(pDesc->Data[0].Data.Address))[0] = tag;
	((unsigned int*)(pDesc->Data[0].Data.Address))[1] = length;
	size = min(length, VI_STREAM_MIN-(2*sizeof(int)));
	if (size > 0)
		memcpy(&((unsigned int*)(pDesc->Data[0].Data.Address))[2], pBuffer, size);
	pDesc->Control.Control = VIP_CONTROL_OP_SENDRECV;
	pDesc->Control.Length = size + 2*sizeof(int);
	pDesc->Control.SegCount = 1;
	pDesc->Control.Reserved = 0;
	pDesc->Data[0].Length = size + 2*sizeof(int);
	pDesc->Data[0].Handle = vinfo->mhSend;
	
	dwStatus = VipPostSend(vinfo->hVi, pDesc, vinfo->mhSend);
	if (!AssertSuccess(dwStatus, "ViSendFirstPacket:VipPostSend failed", pDesc))
	{
		nt_error("Error", 1);
		//if ( (!m_bConnectionLost) || (!ReEstablishConnection(VI_RECON_STATE_POST_SEND)) )
			return false;
	}

	vinfo->nPostedSends++;
	if (vinfo->nPostedSends > vinfo->nNumSendDescriptors)
	{
		printf("ViSendFirstPacket incremented nPostedSends past the maximum\n");fflush(stdout);
	}
	vinfo->nCurSendIndex = (vinfo->nCurSendIndex + 1) % vinfo->nNumSendDescriptors;

	// Adjust the length and buffer pointers
	pBuffer = (unsigned char *)pBuffer + size;
	length = length - size;

	unlock(&vinfo->lock);
	return true;
}

// Function name	: ViSendPacket
// Description	    : 
// Return type		: bool 
// Argument         : VI_Info *vinfo
// Argument         : void *pBuffer
// Argument         : unsigned int length
bool ViSendPacket(VI_Info *vinfo, void *pBuffer, unsigned int length)
{
	VIP_DESCRIPTOR *pDesc;
	VIP_RETURN dwStatus;
	
	// These functions must be locked because the receive thread can send an ack while the
	// main thread is sending a message.
	lock(&vinfo->lock);

	// When nPostedSend equals nNumSendDescriptors, there are no free descriptors available.
	// So I clean up half the posted sends every time nNumSendDescriptors have been used and
	// then wait for an ack
	if (vinfo->nPostedSends == vinfo->nNumSendDescriptors)
	{
		while (true)
		{
			if (g_bViUsePolling)
			{
				while ( (dwStatus = VipSendDone(vinfo->hVi, &pDesc)) == VIP_NOT_DONE )
					Sleep(0);
				if (!AssertSuccess(dwStatus, "ViSendPacket:VipSendDone failed", pDesc))
				{
					nt_error("Error", 1);
					//if ( (!m_bConnectionLost) || (!ReEstablishConnection(VI_RECON_STATE_SEND_WAIT)) )
						return false;
				}
			}
			else
			{
				dwStatus = VipSendWait(vinfo->hVi, VITIMEOUT, &pDesc);
				if (!AssertSuccess(dwStatus, "ViSendPacket:VipSendWait failed", pDesc))
				{
					nt_error("Error", 1);
					//if ( (!m_bConnectionLost) || (!ReEstablishConnection(VI_RECON_STATE_SEND_WAIT)) )
						return false;
				}
			}
			vinfo->nPostedSends--;
			vinfo->nNumSent++;
			if (vinfo->nNumSent % vinfo->nSendsPerAck == 0)
			{
				unlock(&vinfo->lock);
				ViRecvAck(vinfo);
				lock(&vinfo->lock);
				if (vinfo->nPostedSends == vinfo->nNumSendDescriptors)
				{
					if (g_bViUsePolling)
					{
						while ( (dwStatus = VipSendDone(vinfo->hVi, &pDesc)) == VIP_NOT_DONE )
							Sleep(0);
						if (!AssertSuccess(dwStatus, "ViSendPacket:VipSendDone failed", pDesc))
						{
							nt_error("Error", 1);
							//if ( (!m_bConnectionLost) || (!ReEstablishConnection(VI_RECON_STATE_SEND_WAIT)) )
							return false;
						}
					}
					else
					{
						dwStatus = VipSendWait(vinfo->hVi, VITIMEOUT, &pDesc);
						if (!AssertSuccess(dwStatus, "ViSendPacket:VipSendWait failed", pDesc))
						{
							nt_error("Error", 1);
							//if ( (!m_bConnectionLost) || (!ReEstablishConnection(VI_RECON_STATE_SEND_WAIT)) )
							return false;
						}
					}
					vinfo->nPostedSends--;
					vinfo->nNumSent++;
				}
				break;
			}
		}
	}

	// Copy the buffer and setup the packet
	pDesc = vinfo->pSendDesc[vinfo->nCurSendIndex];
	memcpy(pDesc->Data[0].Data.Address, pBuffer, length);
	pDesc->Control.Control = VIP_CONTROL_OP_SENDRECV;
	pDesc->Control.Length = length;
	pDesc->Control.SegCount = 1;
	pDesc->Control.Reserved = 0;
	pDesc->Data[0].Length = length;
	pDesc->Data[0].Handle = vinfo->mhSend;
	
	dwStatus = VipPostSend(vinfo->hVi, pDesc, vinfo->mhSend);
	if (!AssertSuccess(dwStatus, "ViSendPacket:VipPostSend failed", pDesc))
	{
		nt_error("Error", 1);
		//if ( (!m_bConnectionLost) || (!ReEstablishConnection(VI_RECON_STATE_POST_SEND)) )
			return false;
	}

	vinfo->nPostedSends++;
	if (vinfo->nPostedSends > vinfo->nNumSendDescriptors)
	{
		printf("ViSendPacket incremented nPostedSends past the maximum\n");fflush(stdout);
		while(true)
			Sleep(250);
	}
	vinfo->nCurSendIndex = (vinfo->nCurSendIndex + 1) % vinfo->nNumSendDescriptors;

	unlock(&vinfo->lock);
	return true;
}

// Function name	: ViFlushPackets
// Description	    : 
// Return type		: bool 
// Argument         : VI_Info *vinfo
bool ViFlushPackets(VI_Info *vinfo)
{
	VIP_DESCRIPTOR *pDesc;
	VIP_RETURN dwStatus;

	// These functions must be locked because the receive thread can send an ack while the
	// main thread is sending a message and any send can cause a flush.
	lock(&vinfo->lock);

	// Complete all the posted sends
	while (vinfo->nPostedSends > 0)
	{
		if (g_bViUsePolling)
		{
			while ( (dwStatus = VipSendDone(vinfo->hVi, &pDesc)) == VIP_NOT_DONE )
				Sleep(0);
			if (!AssertSuccess(dwStatus, "ViFlushPackets:VipSendDone failed", pDesc))
			{
				nt_error("Error", 1);
				//if ( (!m_bConnectionLost) || (!ReEstablishConnection(VI_RECON_STATE_SEND_WAIT)) )
					return false;
			}
		}
		else
		{
			dwStatus = VipSendWait(vinfo->hVi, VITIMEOUT, &pDesc);
			if (!AssertSuccess(dwStatus, "ViFlushPackets:VipSendWait failed", pDesc))
			{
				nt_error("Error", 1);
				//if ( (!m_bConnectionLost) || (!ReEstablishConnection(VI_RECON_STATE_SEND_WAIT)) )
					return false;
			}
		}
		vinfo->nPostedSends--;
		vinfo->nNumSent++;
		if (vinfo->nNumSent % vinfo->nSendsPerAck == 0)
		{
			unlock(&vinfo->lock);
			ViRecvAck(vinfo);
			lock(&vinfo->lock);
		}
	}
	unlock(&vinfo->lock);
	return true;
}

// Function name	: ViSendAck
// Description	    : 
// Return type		: bool 
// Argument         : VI_Info *vinfo
bool ViSendAck(VI_Info *vinfo)
{
	VIP_DESCRIPTOR *pDesc;
	VIP_RETURN dwStatus;

	// These functions must be locked because the receive thread can send an ack while the
	// main thread is sending a message.
	lock(&vinfo->lock);

	// When nPostedSend equals nNumSendDescriptors, there are no free descriptors available.
	// So I clear up one packet.
	if (vinfo->nPostedSends == vinfo->nNumSendDescriptors)
	{
		if (g_bViUsePolling)
		{
			while ( (dwStatus = VipSendDone(vinfo->hVi, &pDesc)) == VIP_NOT_DONE )
				Sleep(0);
			if (!AssertSuccess(dwStatus, "ViSendAck:VipSendDone failed", pDesc))
			{
				nt_error("Error", 1);
				//if ( (!m_bConnectionLost) || (!ReEstablishConnection(VI_RECON_STATE_SEND_WAIT)) )
				return false;
			}
		}
		else
		{
			dwStatus = VipSendWait(vinfo->hVi, VITIMEOUT, &pDesc);
			if (!AssertSuccess(dwStatus, "ViSendAck:VipSendWait failed", pDesc))
			{
				nt_error("Error", 1);
				//if ( (!m_bConnectionLost) || (!ReEstablishConnection(VI_RECON_STATE_SEND_WAIT)) )
				return false;
			}
		}
		vinfo->nPostedSends--;
		vinfo->nNumSent++;
		if (vinfo->nNumSent % vinfo->nSendsPerAck == 0)
		{
			// BAD CODE BLOCK
			// It is possible for a receive to cause an ack to be sent.  If there are'nt any send descriptors
			// available this ack can't be sent until the posted sends are flushed.  After flushing the sends
			// the code waits for an ack. The second ack will never be received because we are waiting for the
			// first and we won't get back to check the queue until the second ack is received.
			// In other words, the ack receiving code can get in a state where it blocks waiting for a future ack
			// and the receiving thread deadlocks itself.  I have not been able to get a program to enter this
			// block so I don't know if it is a problem.
			printf("Entering code which can fail if called from the WorkerThread\n");fflush(stdout);
			unlock(&vinfo->lock);
			ViRecvAck(vinfo);
			lock(&vinfo->lock);
			if (vinfo->nPostedSends == vinfo->nNumSendDescriptors)
			{
				if (g_bViUsePolling)
				{
					while ( (dwStatus = VipSendDone(vinfo->hVi, &pDesc)) == VIP_NOT_DONE )
						Sleep(0);
					if (!AssertSuccess(dwStatus, "ViSendPacket:VipSendDone failed", pDesc))
					{
						nt_error("Error", 1);
						//if ( (!m_bConnectionLost) || (!ReEstablishConnection(VI_RECON_STATE_SEND_WAIT)) )
						return false;
					}
				}
				else
				{
					dwStatus = VipSendWait(vinfo->hVi, VITIMEOUT, &pDesc);
					if (!AssertSuccess(dwStatus, "ViSendPacket:VipSendWait failed", pDesc))
					{
						nt_error("Error", 1);
						//if ( (!m_bConnectionLost) || (!ReEstablishConnection(VI_RECON_STATE_SEND_WAIT)) )
						return false;
					}
				}
				vinfo->nPostedSends--;
				vinfo->nNumSent++;
			}
			// END OF BAD CODE BLOCK
		}
	}

	pDesc = vinfo->pSendDesc[vinfo->nCurSendIndex];
	pDesc->Control.Control = VIP_CONTROL_OP_SENDRECV | VIP_CONTROL_IMMEDIATE;
	pDesc->Control.Length = 0;
	pDesc->Control.SegCount = 0;
	pDesc->Control.Reserved = 0;
	pDesc->Control.ImmediateData = vinfo->nSequenceNumberSend;
		
	dwStatus = VipPostSend(vinfo->hVi, pDesc, vinfo->mhSend);
	if (!AssertSuccess(dwStatus, "ViSendAck:VipPostSend failed", pDesc))
	{
		nt_error("Error", 1);
		//if ( (!m_bConnectionLost) || (!ReEstablishConnection(VI_RECON_STATE_POST_SEND)) )
		return false;
	}

	vinfo->nPostedSends++;
	if (vinfo->nPostedSends > vinfo->nNumSendDescriptors)
	{
		printf("ViSendAck incremented nPostedSends past the maximum\n");fflush(stdout);
	}
	vinfo->nSequenceNumberSend++;
	vinfo->nCurSendIndex = (vinfo->nCurSendIndex + 1) % vinfo->nNumSendDescriptors;

	unlock(&vinfo->lock);
	return true;
}

// Function name	: ViRecvAck
// Description	    : 
// Return type		: bool 
// Argument         : VI_Info *vinfo
bool ViRecvAck(VI_Info *vinfo)
{
	//*
	// This is not thread safe
	// Wait for the worker thread to signal that an ack packet has been received
	if (g_bViSingleThreaded)
	{
		while (!vinfo->nSendAcked)
			g_MsgQueue.m_pProgressPollFunction();
	}
	else
	{
		while (!vinfo->nSendAcked)
			Sleep(0);
	}
	// Reset the variable to false
	InterlockedDecrement(&vinfo->nSendAcked);
	return true;
	/*/
	// Thread safe version
	LONG nValue;
	if (g_bViSingleThreaded)
	{
		while ( (nValue = InterlockedExchange(&vinfo->nSendAcked, 0)) == 0)
			g_MsgQueue.m_pProgressPollFunction();
	}
	else
	{
		while ( (nValue = InterlockedExchange(&vinfo->nSendAcked, 0)) == 0)
			Sleep(0);
	}

	nValue--;
	InterlockedExchange(&vinfo->nSendAcked, nValue);
	return true;
	//*/
}
