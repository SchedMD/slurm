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
#include <sys/socket.h>
#include <netinet/in.h>

#include "slurm.h"
#include "slurmlib.h"

char 	*Node_API_Buffer = NULL;
int  	Node_API_Buffer_Size = 0;

#if DEBUG_MODULE
/* main is used here for testing purposes only */
main(int argc, char * argv[]) {
    static time_t Last_Update_Time = (time_t)NULL;
    int Error_Code, size, i;
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

    Error_Code = Load_Node(&Last_Update_Time);
    if (Error_Code) printf("Load_Node error %d\n", Error_Code);

    strcpy(Req_Name, "");	/* Start at beginning of partition list */
    for (i=1; ;i++) {
	Error_Code = Load_Node_Config(Req_Name, Next_Name, &CPUs, &RealMemory, &TmpDisk, &Weight, 
	    &Features, &Partition, &Node_State);
	if (Error_Code != 0)  {
	    printf("Load_Node_Config error %d on %s\n", Error_Code, Req_Name);
	    break;
	} /* if */
	if ((i<10) || (i%100 == 0)) {
	    printf("Found node Name=%s, CPUs=%d, RealMemory=%d, TmpDisk=%d, ", 
		Req_Name, CPUs, RealMemory, TmpDisk);
	    printf("State=%s Weight=%d, Features=%s, Partition=%s\n", 
	  	Node_State, Weight, Features, Partition);
	} else if (i%100 == 1) 
	    printf("Skipping...\n");

	if (strlen(Next_Name) == 0) break;
	strcpy(Req_Name, Next_Name);
    } /* while */
    Free_Node_Info();
    exit(0);
} /* main */
#endif


/*
 * Free_Node_Info - Free the node information buffer (if allocated)
 */
void Free_Node_Info(void) {
    if (Node_API_Buffer) free(Node_API_Buffer);
} /* Free_Node_Info */


/*
 * Load_Node - Load the supplied node information buffer for use by info gathering APIs if
 *	node records have changed since the time specified. 
 * Input: Buffer - Pointer to node information buffer
 *        Buffer_Size - size of Buffer
 * Output: Returns 0 if no error, EINVAL if the buffer is invalid, ENOMEM if malloc failure
 */
int Load_Node(time_t *Last_Update_Time) {
    int Buffer_Offset, Buffer_Size, Error_Code, In_Size, Version;
    char Request_Msg[64], *Buffer;
    int sockfd;
    struct sockaddr_in serv_addr;

    if ((sockfd = socket(AF_INET, SOCK_STREAM, 0)) < 0) return EINVAL;
    serv_addr.sin_family	= PF_INET;
    serv_addr.sin_addr.s_addr	= inet_addr(SLURMCTLD_HOST);
    serv_addr.sin_port  	= htons(SLURMCTLD_PORT);
    if (connect(sockfd, (struct sockaddr *) &serv_addr, sizeof(serv_addr)) < 0) {
	close(sockfd); 
	return EINVAL;
    } /* if */
    sprintf(Request_Msg, "DumpNode LastUpdate=%lu", (long)(*Last_Update_Time));
    if (send(sockfd, Request_Msg, strlen(Request_Msg)+1, 0) < strlen(Request_Msg)) {
	close(sockfd); 
	return EINVAL;
    } /* if */
    Buffer = NULL;
    Buffer_Offset = 0;
    Buffer_Size = 8 * 1024;
    while (1) {
    	Buffer = realloc(Buffer, Buffer_Size);
	if (Buffer == NULL) {
	    close(sockfd); 
	    return ENOMEM;
	} /* if */
	In_Size = recv(sockfd, &Buffer[Buffer_Offset], (Buffer_Size-Buffer_Offset), 0);
	if (In_Size <= 0) {	/* End if input */
	    In_Size = 0; 
	    break; 
	} /* if */
	Buffer_Offset +=  In_Size;
	Buffer_Size += In_Size;
    } /* while */
    close(sockfd); 
    Buffer_Size = Buffer_Offset + In_Size;
printf("size=%d\n",Buffer_Size);
    Buffer = realloc(Buffer, Buffer_Size);
    if (Buffer == NULL) return ENOMEM;

    Buffer_Offset = 0;
    Error_Code = Read_Value(Buffer, &Buffer_Offset, Buffer_Size, "NodeVersion", &Version);
    if (Error_Code) {
#if DEBUG_SYSTEM
	fprintf(stderr, "Load_Node: Node buffer lacks valid header\n");
#else
	syslog(LOG_ERR, "Load_Node: Node buffer lacks valid header\n");
#endif
	free(Buffer);
	return EINVAL;
    } /* if */
    if (Version != NODE_STRUCT_VERSION) {
#if DEBUG_SYSTEM
	fprintf(stderr, "Load_Part: expect version %d, read %d\n", NODE_STRUCT_VERSION, Version);
#else
	syslog(LOG_ERR, "Load_Part: expect version %d, read %d\n", NODE_STRUCT_VERSION, Version);
#endif
	free(Buffer);
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
    static time_t Last_Update_Time, Update_Time;
    struct Node_Record My_Node;
    static char Next_Name_Value[MAX_NAME_LEN], My_Partition_Name[MAX_NAME_LEN];
    static Last_Buffer_Offset;
    char *My_Feature, *My_State;
    unsigned *Up_NodeBitMap, *Idle_NodeBitMap;

    /* Load buffer's header (data structure version and time) */
    Buffer_Offset = 0;
    if (Error_Code = Read_Value(Node_API_Buffer, &Buffer_Offset, Node_API_Buffer_Size, 
		"NodeVersion", &Version)) return Error_Code;
    if (Error_Code = Read_Value(Node_API_Buffer, &Buffer_Offset, Node_API_Buffer_Size,
		"UpdateTime", &Update_Time)) return Error_Code;

    if ((Update_Time == Last_Update_Time) && (strcmp(Req_Name,Next_Name_Value) == 0) && 
         (strlen(Req_Name) != 0)) Buffer_Offset=Last_Buffer_Offset;
    Last_Update_Time = Update_Time;
	
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

	Last_Buffer_Offset = Buffer_Offset;
	Error_Code = Read_Value(Node_API_Buffer, &Buffer_Offset, Node_API_Buffer_Size, 
		"NodeName", &Next_Name_Value);
	if (Error_Code) 	/* No more records or bad tag */
	    strcpy(Next_Name, "");
	else
	    strcpy(Next_Name, Next_Name_Value);
	return 0;
    } /* while */

} /* Load_Node_Config */
