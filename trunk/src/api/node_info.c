/* 
 * node_info.c - Get the node records of SLURM
 * See slurm.h for documentation on external functions and data structures
 *
 * Author: Moe Jette, jette@llnl.gov
 */

#define DEBUG_SYSTEM 1

#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif 

#include <errno.h>
#include <stdio.h>
#include <syslog.h>

#include "slurm.h"

char 	*Node_API_Buffer = NULL;
int  	Node_API_Buffer_Size = 0;

#if DEBUG_MODULE
/* main is used here for testing purposes only */
main(int argc, char * argv[]) {
    int Error_Code, size;
    struct Node_Record *Node_Ptr;
    char *Format, *Partition;
    char Req_Name[MAX_NAME_LEN];	/* Name of the partition */
    char Next_Name[MAX_NAME_LEN];	/* Name of the next partition */
    int CPUs, RealMemory, TmpDisk, Weight;
    char *Features, *Nodes, *Node_State;
    char *Dump;
    int Dump_Size;
    time_t Update_Time;
    unsigned *NodeBitMap;	/* Bitmap of nodes in partition */
    int BitMapSize;		/* Bytes in NodeBitMap */

    Error_Code = Load_Node(Dump, Dump_Size);
    if (Error_Code) printf("Load_Node error %d\n", Error_Code);

    Error_Code =  Load_Nodes_Up(&NodeBitMap, &BitMapSize);
    if (Error_Code) printf("Load_Nodes_Up error %d\n", Error_Code);
    if (BitMapSize > 0) printf("Load_Nodes_Up  BitMap[0]=0x%x, BitMapSize=%d\n", 
			NodeBitMap[0], BitMapSize);

    Error_Code =  Load_Nodes_Idle(&NodeBitMap, &BitMapSize);
    if (Error_Code) printf("Load_Nodes_Idle error %d\n", Error_Code);
    if (BitMapSize > 0) printf("Load_Nodes_Idle  BitMap[0]=0x%x, BitMapSize=%d\n", 
			NodeBitMap[0], BitMapSize);

    strcpy(Req_Name, "");	/* Start at beginning of partition list */
    while (1) {
	Error_Code = Load_Node_Config(Req_Name, Next_Name, &CPUs, &RealMemory, &TmpDisk, &Weight, 
	    &Features, &Partition, &Node_State);
	if (Error_Code != 0)  {
	    printf("Load_Node_Config error %d on %s\n", Error_Code, Req_Name);
	    break;
	} /* if */

	printf("Found node Name=%s, CPUs=%d, RealMemory=%d, TmpDisk=%d, ", 
	    Req_Name, CPUs, RealMemory, TmpDisk);
	printf("State=%s Weight=%d, Features=%s, Partition=%s\n", 
	    Node_State, Weight, Features, Partition);

	if (strlen(Next_Name) == 0) break;
	strcpy(Req_Name, Next_Name);
    } /* while */
    free(Dump);

    exit(0);
} /* main */
#endif


/*
 * Load_Node - Load the supplied node information buffer for use by info gathering APIs
 * Input: Buffer - Pointer to node information buffer
 *        Buffer_Size - size of Buffer
 * Output: Returns 0 if no error, EINVAL if the buffer is invalid
 */
int Load_Node(char *Buffer, int Buffer_Size) {
    int Buffer_Offset, Error_Code, Version;

    Buffer_Offset = 0;
    Error_Code = Read_Value(Buffer, &Buffer_Offset, Buffer_Size, "NodeVersion", &Version);
    if (Error_Code) {
#if DEBUG_SYSTEM
	fprintf(stderr, "Load_Node: Node buffer lacks valid header\n");
#else
	syslog(LOG_ERR, "Load_Node: Node buffer lacks valid header\n");
#endif
	return EINVAL;
    } /* if */
    if (Version != NODE_STRUCT_VERSION) {
#if DEBUG_SYSTEM
	fprintf(stderr, "Load_Part: expect version %d, read %d\n", NODE_STRUCT_VERSION, Version);
#else
	syslog(LOG_ERR, "Load_Part: expect version %d, read %d\n", NODE_STRUCT_VERSION, Version);
#endif
	return EINVAL;
    } /* if */

    Node_API_Buffer = Buffer;
    Node_API_Buffer_Size = Buffer_Size;
    return 0;
} /* Load_Node */


/* 
 * Load_Node_Config - Load the state information about the named node
 * Input: Req_Name - Name of the node for which information is requested
 *		     if "", then get info for the first node in list
 *        Next_Name - Location into which the name of the next node is 
 *                   stored, "" if no more
 *        CPUs, etc. - Pointers into which the information is to be stored
 * Output: Next_Name - Name of the next node in the list
 *         CPUs, etc. - The node's state information
 *         Returns 0 on success, ENOENT if not found, or EINVAL if buffer is bad
 * NOTE:  Req_Name and Next_Name must have length MAX_NAME_LEN
 */
int Load_Node_Config(char *Req_Name, char *Next_Name, int *CPUs, 
	int *RealMemory, int *TmpDisk, int *Weight, char **Features,
	char **Partition, char **NodeState) {
    int i, Error_Code, Version, Buffer_Offset, My_Weight;
    time_t Update_Time;
    struct Node_Record My_Node;
    char Next_Name_Value[MAX_NAME_LEN], My_Partition_Name[MAX_NAME_LEN];
    char *My_Feature, *My_State;
    unsigned *Up_NodeBitMap, *Idle_NodeBitMap;

    /* Load buffer's header (data structure version and time) */
    Buffer_Offset = 0;
    if (Error_Code = Read_Value(Node_API_Buffer, &Buffer_Offset, Node_API_Buffer_Size, 
		"NodeVersion", &Version)) return Error_Code;
    if (Error_Code = Read_Value(Node_API_Buffer, &Buffer_Offset, Node_API_Buffer_Size,
		"UpdateTime", &Update_Time)) return Error_Code;

    /* Read up and idle node bitmaps */
    if (Error_Code = Read_Array(Node_API_Buffer, &Buffer_Offset, Node_API_Buffer_Size, 
	"UpBitMap", (void **)&Up_NodeBitMap, (int*)NULL)) return Error_Code;
    if (Error_Code = Read_Array(Node_API_Buffer, &Buffer_Offset, Node_API_Buffer_Size, 
	"IdleBitMap", (void **)&Idle_NodeBitMap, (int*)NULL)) return Error_Code;

    while (1) {
	 /* Load all information for next node */
	Error_Code = Read_Value(Node_API_Buffer, &Buffer_Offset, Node_API_Buffer_Size, 
		"NodeName", &My_Node.Name);
	if (Error_Code == EFAULT) break; /* End of buffer */
	if (Error_Code) return Error_Code;
	if (strlen(Req_Name) == 0)  strcpy(Req_Name,My_Node.Name);

	if (Error_Code = Read_Array(Node_API_Buffer, &Buffer_Offset, Node_API_Buffer_Size, 
		"NodeState", (void **)&My_State, (int*)NULL)) return Error_Code;

	if (Error_Code = Read_Value(Node_API_Buffer, &Buffer_Offset, Node_API_Buffer_Size, 
		"CPUs", &My_Node.CPUs)) return Error_Code;

	if (Error_Code = Read_Value(Node_API_Buffer, &Buffer_Offset, Node_API_Buffer_Size, 
		"RealMemory", &My_Node.RealMemory)) return Error_Code;

	if (Error_Code = Read_Value(Node_API_Buffer, &Buffer_Offset, Node_API_Buffer_Size, 
		"TmpDisk", &My_Node.TmpDisk)) return Error_Code;

	if (Error_Code = Read_Value(Node_API_Buffer, &Buffer_Offset, Node_API_Buffer_Size, 
		"Partition", &My_Partition_Name)) return Error_Code;

	if (Error_Code = Read_Value(Node_API_Buffer, &Buffer_Offset, Node_API_Buffer_Size, 
		"Weight", &My_Weight)) return Error_Code;

	if (Error_Code = Read_Array(Node_API_Buffer, &Buffer_Offset, Node_API_Buffer_Size, 
		"Feature", (void **)&My_Feature, (int*)NULL)) return Error_Code;

	if (Error_Code = Read_Tag(Node_API_Buffer, &Buffer_Offset, Node_API_Buffer_Size, 
		"EndNode")) return Error_Code;

	/* Check if this is requested partition */ 
	if (strcmp(Req_Name, My_Node.Name) != 0) continue;

	/*Load values to be returned */
	*CPUs = My_Node.CPUs;
	*RealMemory = My_Node.RealMemory;
	*TmpDisk = My_Node.TmpDisk;
	NodeState[0] = My_State;
	Partition[0] = My_Partition_Name;
	*Weight = My_Weight;
	Features[0] = My_Feature;

	Error_Code = Read_Value(Node_API_Buffer, &Buffer_Offset, Node_API_Buffer_Size, 
		"NodeName", &Next_Name_Value);
	if (Error_Code) 	/* No more records or bad tag */
	    strcpy(Next_Name, "");
	else
	    strcpy(Next_Name, Next_Name_Value);
	return 0;
    } /* while */

} /* Load_Node_Config */


/* 
 * Load_Nodes_Idle - Load the bitmap of idle nodes
 * Input: NodeBitMap - Location to put bitmap pointer
 *        BitMap_Size - Location into which the byte size of NodeBitMap is to be stored
 * Output: NodeBitMap - Pointer to bitmap
 *         BitMap_Size - Byte size of NodeBitMap
 *         Returns 0 on success or EINVAL if buffer is bad
 */
int Load_Nodes_Idle(unsigned **NodeBitMap, int *BitMap_Size) {
    int Error_Code, Version, Buffer_Offset;
    time_t Update_Time;
    unsigned *Up_NodeBitMap, *Idle_NodeBitMap;

    /* Load buffer's header (data structure version and time) */
    Buffer_Offset = 0;
    if (Error_Code = Read_Value(Node_API_Buffer, &Buffer_Offset, Node_API_Buffer_Size, 
		"NodeVersion", &Version)) return Error_Code;
    if (Error_Code = Read_Value(Node_API_Buffer, &Buffer_Offset, Node_API_Buffer_Size,
		"UpdateTime", &Update_Time)) return Error_Code;

    /* Read up and idle node bitmaps */
    if (Error_Code = Read_Array(Node_API_Buffer, &Buffer_Offset, Node_API_Buffer_Size, 
	"UpBitMap", (void **)&Up_NodeBitMap, BitMap_Size)) return Error_Code;
    if (Error_Code = Read_Array(Node_API_Buffer, &Buffer_Offset, Node_API_Buffer_Size, 
	"IdleBitMap", (void **)&Idle_NodeBitMap, BitMap_Size)) return Error_Code;

    NodeBitMap[0] = Idle_NodeBitMap;
    return 0;
} /* Load_Nodes_Idle */


/* 
 * Load_Nodes_Up - Load the bitmap of up nodes
 * Input: NodeBitMap - Location to put bitmap pointer
 *        BitMap_Size - Location into which the byte size of NodeBitMap is to be stored
 * Output: NodeBitMap - Pointer to bitmap
 *         BitMap_Size - Byte size of NodeBitMap
 *         Returns 0 on success or EINVAL if buffer is bad
 */
int Load_Nodes_Up(unsigned **NodeBitMap, int *BitMap_Size) {
    int Error_Code, Version, Buffer_Offset;
    time_t Update_Time;
    unsigned *Up_NodeBitMap, *Idle_NodeBitMap;

    /* Load buffer's header (data structure version and time) */
    Buffer_Offset = 0;
    if (Error_Code = Read_Value(Node_API_Buffer, &Buffer_Offset, Node_API_Buffer_Size, 
		"NodeVersion", &Version)) return Error_Code;
    if (Error_Code = Read_Value(Node_API_Buffer, &Buffer_Offset, Node_API_Buffer_Size,
		"UpdateTime", &Update_Time)) return Error_Code;

    /* Read up and idle node bitmaps */
    if (Error_Code = Read_Array(Node_API_Buffer, &Buffer_Offset, Node_API_Buffer_Size, 
	"UpBitMap", (void **)&Up_NodeBitMap, BitMap_Size)) return Error_Code;

    NodeBitMap[0] = Up_NodeBitMap;
    return 0;
} /* Load_Nodes_Up */
