//
// Copyright (C) 1998 Giganet Incorporated
// All rights reserved
//
// This file represents information contained the Virtual Interface Specification Revision 1.0
// as ported to Windows NT 4.0
//

// Modified by David Ashton
// Argonne National Lab
// July 2000

#ifndef _VIPL_H
#define _VIPL_H

#define VI_CALL __cdecl

#ifndef IN
#define IN
#endif
#ifndef OUT
#define OUT
#endif

//
// VIA types
//
typedef unsigned __int64 VIP_UINT64;
typedef unsigned __int32 VIP_UINT32;
typedef unsigned __int16 VIP_UINT16;
typedef unsigned __int8 VIP_UINT8;

typedef unsigned char VIP_UCHAR;
typedef char VIP_CHAR;

typedef unsigned short VIP_USHORT;
typedef short VIP_SHORT;

typedef unsigned long VIP_ULONG;
typedef long VIP_LONG;

typedef int VIP_BOOLEAN;
typedef void *VIP_PVOID;
typedef void *VIP_EVENT_HANDLE;

#define VIP_TRUE	1
#define VIP_FALSE 0

//
// handle types
//
typedef VIP_PVOID VIP_QOS;
typedef VIP_PVOID VIP_NIC_HANDLE;
typedef VIP_PVOID VIP_VI_HANDLE;
typedef VIP_PVOID VIP_CQ_HANDLE;
typedef VIP_PVOID VIP_PROTECTION_HANDLE;
typedef VIP_UINT32 VIP_MEM_HANDLE;
typedef VIP_PVOID VIP_CONN_HANDLE;

//
// infinite timeout
//
#define VIP_INFINITE INFINITE

#ifdef USE_FALCON_DESCRIPTOR_FORMAT

typedef FALCON_DESCRIPTOR VIP_DESCRIPTOR;

#else
 
//
// VIA descriptors
//
struct _VIP_DESCRIPTOR;

//
// VIA 64 bit address format
//
typedef volatile union {
    VIP_UINT64 AddressBits;
    VIP_PVOID Address;
    struct _VIP_DESCRIPTOR *Descriptor;
} VIP_PVOID64;

//
// control segment format
//
typedef volatile struct {
    VIP_PVOID64 Next;
    VIP_MEM_HANDLE NextHandle;
    VIP_UINT16 SegCount;
    VIP_UINT16 Control;

    VIP_UINT32 Reserved;
    VIP_UINT32 ImmediateData;
    VIP_UINT32 Length;
    VIP_UINT32 Status;
} VIP_CONTROL_SEGMENT;

//
// control field
//
#define VIP_CONTROL_OP_SENDRECV         0x0
#define VIP_CONTROL_OP_RDMAWRITE        0x1
#define VIP_CONTROL_OP_RDMAREAD         0x2
#define VIP_CONTROL_OP_RESERVED         0x3
#define VIP_CONTROL_OP_MASK             0x3
#define VIP_CONTROL_IMMEDIATE           0x4
#define VIP_CONTROL_QFENCE              0x8
#define VIP_CONTROL_RESERVED            0xfff0

//
// status field
//
#define VIP_STATUS_DONE                 0x1
#define VIP_STATUS_FORMAT_ERROR         0x2
#define VIP_STATUS_PROTECTION_ERROR     0x4
#define VIP_STATUS_LENGTH_ERROR         0x8
#define VIP_STATUS_PARTIAL_ERROR        0x10
#define VIP_STATUS_DESC_FLUSHED_ERROR   0x20
#define VIP_STATUS_TRANSPORT_ERROR      0x40
#define VIP_STATUS_RDMA_PROT_ERROR      0x80
#define VIP_STATUS_REMOTE_DESC_ERROR    0x100
#define VIP_STATUS_ERROR_MASK           0x1fe

#define VIP_STATUS_OP_SEND              0x00000
#define VIP_STATUS_OP_RECEIVE           0x10000
#define VIP_STATUS_OP_RDMA_WRITE        0x20000
#define VIP_STATUS_OP_REMOTE_RDMA_WRITE 0x30000
#define VIP_STATUS_OP_RDMA_READ         0x40000
#define VIP_STATUS_OP_MASK              0x70000
#define VIP_STATUS_IMMEDIATE            0x80000

#define VIP_STATUS_RESERVED             0xFFF0FE00

//
// address segment format
//
typedef volatile struct {
    VIP_PVOID64 Data;
    VIP_MEM_HANDLE Handle;
    VIP_UINT32 Reserved;
} VIP_ADDRESS_SEGMENT;

//
// data segment format
//
typedef volatile struct {
    VIP_PVOID64 Data;
    VIP_MEM_HANDLE Handle;
    VIP_UINT32 Length;
} VIP_DATA_SEGMENT;

#ifdef VIPL095

typedef union {
    VIP_ADDRESS_SEGMENT Remote;
    VIP_DATA_SEGMENT Local;
} VIP_DESCRIPTOR_SEGMENT;

//
// VIA descriptor format
//
typedef struct _VIP_DESCRIPTOR {
    VIP_CONTROL_SEGMENT CS;
    VIP_DESCRIPTOR_SEGMENT DS[2];
} VIP_DESCRIPTOR;

#else

//
// VIA descriptor format
//
typedef struct _VIP_DESCRIPTOR {
    VIP_CONTROL_SEGMENT Control;
    VIP_DATA_SEGMENT Data[1];
} VIP_DESCRIPTOR;

#endif
#endif

//
// descriptor alignment
//
#define VIP_DESCRIPTOR_ALIGNMENT 64

//
// API return codes
//
typedef enum {
    VIP_SUCCESS,
    VIP_NOT_DONE,
    VIP_INVALID_PARAMETER,
    VIP_ERROR_RESOURCE,

    VIP_TIMEOUT,
    VIP_REJECT,
    VIP_INVALID_RELIABILITY_LEVEL,
    VIP_INVALID_MTU,

    VIP_INVALID_QOS,
    VIP_INVALID_PTAG,
    VIP_INVALID_RDMAREAD,
    VIP_DESCRIPTOR_ERROR,

    VIP_INVALID_STATE,
    VIP_ERROR_NAMESERVICE,
    VIP_NO_MATCH,
    VIP_NOT_REACHABLE,

    VIP_ERROR_NOT_SUPPORTED,

    VIP_ERROR = VIP_DESCRIPTOR_ERROR,
} VIP_RETURN;

typedef VIP_USHORT VIP_RELIABILITY_LEVEL;

//
// VI reliability levels
//
#define VIP_SERVICE_UNRELIABLE 1
#define VIP_SERVICE_RELIABLE_DELIVERY 2
#define VIP_SERVICE_RELIABLE_RECEPTION 4
#define VIP_BASE_SERVICES (VIP_SERVICE_UNRELIABLE+VIP_SERVICE_RELIABLE_DELIVERY+VIP_SERVICE_RELIABLE_RECEPTION)
#define VIP_SERVICE_ACK	(1<<14)
#define VIP_SERVICE_DFC (1<<15)

//
// Network address formats
//
typedef struct _VIP_NET_ADDRESS {
    VIP_UINT16 HostAddressLen;
    VIP_UINT16 DiscriminatorLen;
    VIP_UINT8 HostAddress[1];
} VIP_NET_ADDRESS;

//
// NIC attributes
//
typedef struct _VIP_NIC_ATTRIBUTES {
    VIP_CHAR Name[64];
    VIP_ULONG HardwareVersion;
    VIP_ULONG ProviderVersion;
    VIP_UINT16 NicAddressLen;
    const VIP_UINT8 *LocalNicAddress;
    VIP_BOOLEAN ThreadSafe;
    VIP_UINT16 MaxDiscriminatorLen;
    VIP_ULONG MaxRegisterBytes;
    VIP_ULONG MaxRegisterRegions;
    VIP_ULONG MaxRegisterBlockBytes;
    VIP_ULONG MaxVI;
    VIP_ULONG MaxDescriptorsPerQueue;
    VIP_ULONG MaxSegmentsPerDesc;
    VIP_ULONG MaxCQ;
    VIP_ULONG MaxCQEntries;
    VIP_ULONG MaxTransferSize;
    VIP_ULONG NativeMTU;
    VIP_ULONG MaxPtags;
    VIP_RELIABILITY_LEVEL ReliabilityLevelSupport;
    VIP_RELIABILITY_LEVEL RDMAReadSupport;
} VIP_NIC_ATTRIBUTES;

//
// Memory attributes
//
typedef struct _VIP_MEM_ATTRIBUTES {
    VIP_PROTECTION_HANDLE Ptag;
    VIP_BOOLEAN EnableRdmaWrite;
    VIP_BOOLEAN EnableRdmaRead;
} VIP_MEM_ATTRIBUTES;

typedef enum _VIP_RESOURCE_CODE {
    VIP_RESOURCE_NIC,
    VIP_RESOURCE_VI,
    VIP_RESOURCE_CQ,
    VIP_RESOURCE_DESCRIPTOR,
} VIP_RESOURCE_CODE;

typedef enum _VIP_ERROR_CODE {
    VIP_ERROR_POST_DESC,
    VIP_ERROR_CONN_LOST,
    VIP_ERROR_RECVQ_EMPTY,
    VIP_ERROR_VI_OVERRUN,
    VIP_ERROR_RDMAW_PROT,
    VIP_ERROR_RDMAW_DATA,
    VIP_ERROR_RDMAW_ABORT,
    VIP_ERROR_RDMAR_PROT,
    VIP_ERROR_COMP_PROT,
    VIP_ERROR_RDMA_TRANSPORT,
    VIP_ERROR_CATASTROPHIC,
} VIP_ERROR_CODE;

typedef struct _VIP_ERROR_DESCRIPTOR {
    VIP_NIC_HANDLE NicHandle;
    VIP_VI_HANDLE ViHandle;
    VIP_CQ_HANDLE CQHandle;
    VIP_DESCRIPTOR *DescriptorPtr;
    VIP_ULONG OpCode;
    VIP_RESOURCE_CODE ResourceCode;
    VIP_ERROR_CODE ErrorCode;
} VIP_ERROR_DESCRIPTOR;

//
// VI states
//
typedef enum {
    VIP_STATE_IDLE,
    VIP_STATE_CONNECTED,
    VIP_STATE_CONNECT_PENDING,
    VIP_STATE_ERROR,
} VIP_VI_STATE;

//
// VI attributes
//
typedef struct _VIP_VI_ATTRIBUTES {
    VIP_RELIABILITY_LEVEL ReliabilityLevel;
    VIP_ULONG MaxTransferSize;
    VIP_QOS QoS;
    VIP_PROTECTION_HANDLE Ptag;
    VIP_BOOLEAN EnableRdmaWrite;
    VIP_BOOLEAN EnableRdmaRead;
} VIP_VI_ATTRIBUTES;

#define VIP_SMI_AUTODISCOVERY ((VIP_ULONG) 1)

typedef struct {
    VIP_ULONG NumberOfHops;
    VIP_NET_ADDRESS *ADAddrArray;
    VIP_ULONG NumAdAddrs;
} VIP_AUTODISCOVERY_LIST;


#if __cplusplus
extern "C" {
#endif

//
// NIC primitives
//
extern
VIP_RETURN (VI_CALL *VipOpenNic)(
    IN const VIP_CHAR *DeviceName,
    OUT VIP_NIC_HANDLE *NicHandle);

extern
VIP_RETURN (VI_CALL *VipCloseNic)(
    IN VIP_NIC_HANDLE NicHandle);

extern
VIP_RETURN (VI_CALL *VipQueryNic)(
    IN VIP_NIC_HANDLE NicHandle,
    OUT VIP_NIC_ATTRIBUTES *Attributes);

extern
VIP_RETURN (VI_CALL *VipRegisterMem)(
    IN VIP_NIC_HANDLE NicHandle,
    IN VIP_PVOID VirtualAddress,
    IN VIP_ULONG Length,
    IN VIP_MEM_ATTRIBUTES *MemAttributes,
    OUT VIP_MEM_HANDLE *MemHandle);

extern
VIP_RETURN (VI_CALL *VipDeregisterMem)(
    IN VIP_NIC_HANDLE NicHandle,
    IN VIP_PVOID VirtualAddress,
    IN VIP_MEM_HANDLE MemHandle);

extern
VIP_RETURN (VI_CALL *VipQueryMem)(
    IN VIP_NIC_HANDLE NicHandle,
    IN VIP_PVOID VirtualAddress,
    IN VIP_MEM_HANDLE MemHandle,
    OUT VIP_MEM_ATTRIBUTES *MemAttributes);

extern
VIP_RETURN (VI_CALL *VipSetMemAttributes)(
    IN VIP_NIC_HANDLE NicHandle,
    IN VIP_PVOID VirtualAddress,
    IN VIP_MEM_HANDLE MemHandle,
    IN VIP_MEM_ATTRIBUTES *MemAttributes);

typedef void (*VIP_ERROR_HANDLER)(VIP_PVOID, VIP_ERROR_DESCRIPTOR *);

extern
VIP_RETURN (VI_CALL *VipErrorCallback)(
    IN VIP_NIC_HANDLE NicHandle,
    IN VIP_PVOID Context,
    IN VIP_ERROR_HANDLER ErrorHandler);

//
// management
//
extern
VIP_RETURN (VI_CALL *VipQuerySystemManagementInfo)(
    IN VIP_NIC_HANDLE NicHandle,
    IN VIP_ULONG InfoType,
    OUT VIP_PVOID SysManInfo);

//
// Protection tags
//
extern
VIP_RETURN (VI_CALL *VipCreatePtag)(
    IN VIP_NIC_HANDLE NicHandle,
    OUT VIP_PROTECTION_HANDLE *ProtectionTag);

extern
VIP_RETURN (VI_CALL *VipDestroyPtag)(
    IN VIP_NIC_HANDLE NicHandle,
    IN VIP_PROTECTION_HANDLE ProtectionTag);

//
// VI primitives
//
extern
VIP_RETURN (VI_CALL *VipCreateVi)(
    IN VIP_NIC_HANDLE NicHandle,
    IN VIP_VI_ATTRIBUTES *ViAttributes,
    IN VIP_CQ_HANDLE SendCQHandle,
    IN VIP_CQ_HANDLE RecvCQHandle,
    OUT VIP_VI_HANDLE *ViHandle);

extern
VIP_RETURN (VI_CALL *VipDestroyVi)(
    IN VIP_VI_HANDLE ViHandle);

extern
VIP_RETURN (VI_CALL *VipQueryVi)(
    IN VIP_VI_HANDLE ViHandle,
    OUT VIP_VI_STATE *State,
    OUT VIP_VI_ATTRIBUTES *Attributes,
    OUT VIP_BOOLEAN *SendQueueEmpty,
    OUT VIP_BOOLEAN *RecvQueueEmpty);

extern
VIP_RETURN (VI_CALL *VipSetViAttributes)(
    IN VIP_VI_HANDLE ViHandle,
    IN VIP_VI_ATTRIBUTES *Attributes);

extern
VIP_RETURN (VI_CALL *VipPostSend)(
    IN VIP_VI_HANDLE ViHandle,
    IN VIP_DESCRIPTOR *DescriptorPtr,
    IN VIP_MEM_HANDLE MemoryHandle);

extern
VIP_RETURN (VI_CALL *VipSendDone)(
    IN VIP_VI_HANDLE ViHandle,
    OUT VIP_DESCRIPTOR **DescriptorPtr);

extern
VIP_RETURN (VI_CALL *VipSendWait)(
    IN VIP_VI_HANDLE ViHandle,
    IN VIP_ULONG TimeOut,
    OUT VIP_DESCRIPTOR **DescriptorPtr);

typedef void (*VIP_VI_CALLBACK)(
    VIP_PVOID Context, VIP_NIC_HANDLE NicHandle, VIP_VI_HANDLE ViHandle, VIP_DESCRIPTOR *Descriptor);

extern
VIP_RETURN (VI_CALL *VipSendNotify)(
    IN VIP_VI_HANDLE ViHandle,
    IN VIP_PVOID Context,
    IN VIP_VI_CALLBACK Callback);

extern
VIP_RETURN (VI_CALL *VipPostRecv)(
    IN VIP_VI_HANDLE ViHandle,
    IN VIP_DESCRIPTOR *DescriptorPtr,
    IN VIP_MEM_HANDLE MemoryHandle);

extern
VIP_RETURN (VI_CALL *VipRecvDone)(
    IN VIP_VI_HANDLE ViHandle,
    OUT VIP_DESCRIPTOR **DescriptorPtr);

extern
VIP_RETURN (VI_CALL *VipRecvWait)(
    IN VIP_VI_HANDLE ViHandle,
    IN VIP_ULONG TimeOut,
    OUT VIP_DESCRIPTOR **DescriptorPtr);

extern
VIP_RETURN (VI_CALL *VipRecvNotify)(
    IN VIP_VI_HANDLE ViHandle,
    IN VIP_PVOID Context,
    IN VIP_VI_CALLBACK Callback);

extern
VIP_RETURN (VI_CALL *VipConnectWait)(
    IN VIP_NIC_HANDLE NicHandle,
    IN VIP_NET_ADDRESS *LocalAddr,
    IN VIP_ULONG Timeout,
    OUT VIP_NET_ADDRESS *RemoteAddr,
    OUT VIP_VI_ATTRIBUTES *RemoteViAttributes,
    OUT VIP_CONN_HANDLE *ConnHandle);

extern
VIP_RETURN (VI_CALL *VipConnectAccept)(
    IN VIP_CONN_HANDLE ConnHandle,
    IN VIP_VI_HANDLE ViHandle);

extern
VIP_RETURN (VI_CALL *VipConnectReject)(
    IN VIP_CONN_HANDLE ConnHandle);

extern
VIP_RETURN (VI_CALL *VipConnectRequest)(
    IN VIP_VI_HANDLE ViHandle,
    IN VIP_NET_ADDRESS *LocalAddr,
    IN VIP_NET_ADDRESS *RemoteAddr,
    IN VIP_ULONG Timeout,
    OUT VIP_VI_ATTRIBUTES *RemoteViAttributes);

extern
VIP_RETURN (VI_CALL *VipDisconnect)(
    IN VIP_VI_HANDLE ViHandle);

extern
VIP_PVOID (VI_CALL *VipGetUserData)(
	IN VIP_VI_HANDLE ViHandle);

extern 
void (VI_CALL *VipSetUserData)(
	IN VIP_VI_HANDLE vih, 
	IN VIP_PVOID data);

// 
// Completion Queue primitives
//
extern
VIP_RETURN (VI_CALL *VipCreateCQ)(
    IN VIP_NIC_HANDLE NicHandle,
    IN VIP_ULONG EntryCount,
    OUT VIP_CQ_HANDLE *CQHandle);

extern
VIP_RETURN (VI_CALL *VipDestroyCQ)(
    IN VIP_CQ_HANDLE CQHandle);

extern
VIP_RETURN (VI_CALL *VipResizeCQ)(
    IN VIP_CQ_HANDLE CQHandle,
    IN VIP_ULONG EntryCount);

extern
VIP_RETURN (VI_CALL *VipCQDone)(
    IN VIP_CQ_HANDLE CQHandle,
    OUT VIP_VI_HANDLE *ViHandle,
    OUT VIP_BOOLEAN *RecvQueue);

extern
VIP_RETURN (VI_CALL *VipCQWait)(
    IN VIP_CQ_HANDLE CQHandle,
    IN VIP_ULONG Timeout,
    OUT VIP_VI_HANDLE *ViHandle,
    OUT VIP_BOOLEAN *RecvQueue);

typedef void (*VIP_CQ_CALLBACK)(
    VIP_PVOID Context, VIP_NIC_HANDLE NicHandle, VIP_VI_HANDLE ViHandle, VIP_BOOLEAN RecvQueue);

extern
VIP_RETURN (VI_CALL *VipCQNotify)(
    IN VIP_CQ_HANDLE CqHandle,
    IN VIP_PVOID Context,
    IN VIP_CQ_CALLBACK Callback);

//
// name service API
//
extern
VIP_RETURN (VI_CALL *VipNSInit)(
    IN VIP_NIC_HANDLE NicHandle,
    IN VIP_PVOID NSInitInfo);

extern
VIP_RETURN (VI_CALL *VipNSGetHostByName)(
    IN VIP_NIC_HANDLE NicHandle,
    IN VIP_CHAR *Name,
    OUT VIP_NET_ADDRESS *Address,
    IN VIP_ULONG NameIndex);

extern
VIP_RETURN (VI_CALL *VipNSGetHostByAddr)(
    IN VIP_NIC_HANDLE NicHandle,
    IN VIP_NET_ADDRESS *Address,
    OUT VIP_CHAR *Name,
    IN OUT VIP_ULONG *NameLen);

extern
VIP_RETURN (VI_CALL *VipNSShutdown)(
    IN VIP_NIC_HANDLE NicHandle);

//
// peer connection API
//
extern
VIP_RETURN (VI_CALL *VipConnectPeerRequest)(
    IN VIP_VI_HANDLE ViHandle,
    IN VIP_NET_ADDRESS *LocalAddr,
    IN VIP_NET_ADDRESS *RemoteAddr,
    IN VIP_ULONG Timeout);

extern
VIP_RETURN (VI_CALL *VipConnectPeerDone)(
    IN VIP_VI_HANDLE ViHandle,
    OUT VIP_VI_ATTRIBUTES *RemoteAttributes);

extern
VIP_RETURN (VI_CALL *VipConnectPeerWait)(
    IN VIP_VI_HANDLE ViHandle,
    OUT VIP_VI_ATTRIBUTES *RemoteViAttributes);

//
// Tag demultiplexing
//
extern
VIP_RETURN (VI_CALL *VipAddTagCQ)(
	IN VIP_CQ_HANDLE CQHandle,
	IN OUT VIP_EVENT_HANDLE *Event,
	IN VIP_ULONG Tag,
	IN VIP_ULONG Priority);

extern
VIP_RETURN (VI_CALL *VipRemoveTagCQ)(
	IN VIP_CQ_HANDLE CQHandle,
	IN VIP_EVENT_HANDLE Event,
	IN VIP_ULONG Tag);

//
// DFC
//
extern
VIP_RETURN (VI_CALL *VipPostDeferredSends)(
	IN VIP_VI_HANDLE vihandle, 
	IN VIP_BOOLEAN enableinterrupt,
	IN OUT VIP_BOOLEAN *sendsdeferred);


#if __cplusplus
};
#endif

#endif
