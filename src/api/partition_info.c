/* 
 * partition_info.c - Get the partition information of SLURM
 * See slurm.h for documentation on external functions and data structures
 *
 * Author: Moe Jette, jette@llnl.gov
 */

#define PROTOTYPE_API 1

#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <syslog.h>
#include <sys/socket.h>
#include <netinet/in.h>

#include "slurm.h"
#include "slurmlib.h"

char *Part_API_Buffer = NULL;
int  Part_API_Buffer_Size = 0;

#if DEBUG_MODULE
/* main is used here for module testing purposes only */
main(int argc, char * argv[]) {
    static time_t Last_Update_Time = (time_t)NULL;
    int Error_Code;
    char Req_Name[MAX_NAME_LEN];	/* Name of the partition */
    char Next_Name[MAX_NAME_LEN];	/* Name of the next partition */
    int MaxTime;		/* -1 if unlimited */
    int MaxNodes;		/* -1 if unlimited */
    int TotalNodes;		/* Total number of nodes in the partition */
    int TotalCPUs;		/* Total number of CPUs in the partition */
    char *Nodes;		/* Names of nodes in partition */
    char *AllowGroups;		/* NULL indicates ALL */
    int Key;    	 	/* 1 if SLURM distributed key is required for use of partition */
    int StateUp;		/* 1 if state is UP */
    int Shared;			/* 1 if partition can be shared */
    unsigned *NodeBitMap;	/* Bitmap of nodes in partition */
    int BitMapSize;		/* Bytes in NodeBitMap */

    Error_Code = Load_Part(&Last_Update_Time);
    if (Error_Code) {
	printf("Load_Part error %d\n", Error_Code);
	exit(1);
    } /* if */
    strcpy(Req_Name, "");	/* Start at beginning of partition list */
    while (Error_Code == 0) {
	Error_Code = Load_Part_Name(Req_Name, Next_Name, &MaxTime, &MaxNodes, 
	    &TotalNodes, &TotalCPUs, &Key, &StateUp, &Shared, &Nodes, &AllowGroups);
	if (Error_Code != 0)  {
	    printf("Load_Part_Name error %d finding %s\n", Error_Code, Req_Name);
	    break;
	} /* if */

	printf("Found partition Name=%s, TotalNodes=%d, Nodes=%s, MaxTime=%d, MaxNodes=%d\n", 
	    Req_Name, TotalNodes, Nodes, MaxTime, MaxNodes);
	printf("  TotalNodes=%d, TotalCPUs=%d, Key=%d StateUp=%d, Shared=%d, AllowGroups=%s\n", 
	    TotalNodes, TotalCPUs, Key, StateUp, Shared, AllowGroups);
	if (strlen(Next_Name) == 0) break;
	strcpy(Req_Name, Next_Name);
    } /* while */
    Free_Part_Info();
    exit(0);
} /* main */
#endif


/*
 * Free_Part_Info - Free the partition information buffer (if allocated)
 */
void Free_Part_Info(void) {
    if (Part_API_Buffer) free(Part_API_Buffer);
} /* Free_Part_Info */


/*
 * Load_Part - Update the partition information buffer for use by info gathering APIs if 
 *	partition records have changed since the time specified. 
 * Input: Last_Update_Time - Pointer to time of last buffer
 * Output: Last_Update_Time - Time reset if buffer is updated
 *         Returns 0 if no error, EINVAL if the buffer is invalid, ENOMEM if malloc failure
 */
int Load_Part(time_t *Last_Update_Time) {
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
    sprintf(Request_Msg, "DumpPart LastUpdate=%lu", (long)(*Last_Update_Time));
    if (send(sockfd, Request_Msg, strlen(Request_Msg)+1, 0) < strlen(Request_Msg)) {
	close(sockfd); 
	return EINVAL;
    } /* if */
    Buffer = NULL;
    Buffer_Offset = 0;
    Buffer_Size = 1024;
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
    Buffer = realloc(Buffer, Buffer_Size);
    if (Buffer == NULL) return ENOMEM;

    Buffer_Offset = 0;
    Error_Code = Read_Value(Buffer, &Buffer_Offset, Buffer_Size, "PartVersion", &Version);
    if (Error_Code) {
#if DEBUG_SYSTEM
	fprintf(stderr, "Load_Part: Partition buffer lacks valid header\n");
#else
	syslog(LOG_ERR, "Load_Part: Partition buffer lacks valid header\n");
#endif
	free(Buffer);
	return EINVAL;
    } /* if */
    if (Version != PART_STRUCT_VERSION) {
#if DEBUG_SYSTEM
	fprintf(stderr, "Load_Part: expect version %d, read %d\n", PART_STRUCT_VERSION, Version);
#else
	syslog(LOG_ERR, "Load_Part: expect version %d, read %d\n", PART_STRUCT_VERSION, Version);
#endif
	free(Buffer);
	return EINVAL;
    } /* if */

    Part_API_Buffer = Buffer;
    Part_API_Buffer_Size = Buffer_Size;
    return 0;
} /* Load_Part */


/* 
 * Load_Part_Name - Load the state information about the named partition
 * Input: Req_Name - Name of the partition for which information is requested
 *		     if "", then get info for the first partition in list
 *        Next_Name - Location into which the name of the next partition is 
 *                   stored, "" if no more
 *        MaxTime, etc. - Pointers into which the information is to be stored
 * Output: Req_Name - The partition's name is stored here
 *         Next_Name - The name of the next partition in the list is stored here
 *         MaxTime, etc. - The partition's state information
 *         Returns 0 on success, ENOENT if not found, or EINVAL if buffer is bad
 * NOTE:  Req_Name and Next_Name must have length MAX_NAME_LEN
 */
int Load_Part_Name(char *Req_Name, char *Next_Name, int *MaxTime, int *MaxNodes, 
	int *TotalNodes, int *TotalCPUs, int *Key, int *StateUp, int *Shared,
	char **Nodes, char **AllowGroups) {
    int i, Error_Code, Version, Buffer_Offset;
    static time_t Last_Update_Time, Update_Time;
    struct Part_Record My_Part;
    int My_BitMap_Size;
    static char Next_Name_Value[MAX_NAME_LEN];
    static Last_Buffer_Offset;

    /* Load buffer's header (data structure version and time) */
    Buffer_Offset = 0;
    if (Error_Code = Read_Value(Part_API_Buffer, &Buffer_Offset, Part_API_Buffer_Size, 
		"PartVersion", &Version)) return Error_Code;
    if (Error_Code = Read_Value(Part_API_Buffer, &Buffer_Offset, Part_API_Buffer_Size,
		"UpdateTime", &Update_Time)) return Error_Code;

    if ((Update_Time == Last_Update_Time) && (strcmp(Req_Name,Next_Name_Value) == 0) && 
         (strlen(Req_Name) != 0)) Buffer_Offset=Last_Buffer_Offset;
    Last_Update_Time = Update_Time;

    while (1) {	
	/* Load all info for next partition */
	Error_Code = Read_Value(Part_API_Buffer, &Buffer_Offset, Part_API_Buffer_Size, 
		"PartName", &My_Part.Name);
	if (Error_Code == EFAULT) break; /* End of buffer */
	if (Error_Code) return Error_Code;
	if (strlen(Req_Name) == 0)  strcpy(Req_Name,My_Part.Name);

	if (Error_Code = Read_Value(Part_API_Buffer, &Buffer_Offset, Part_API_Buffer_Size, 
		"MaxTime", &My_Part.MaxTime)) return Error_Code;

	if (Error_Code = Read_Value(Part_API_Buffer, &Buffer_Offset, Part_API_Buffer_Size, 
		"MaxNodes", &My_Part.MaxNodes)) return Error_Code;

	if (Error_Code = Read_Value(Part_API_Buffer, &Buffer_Offset, Part_API_Buffer_Size, 
		"TotalNodes", &My_Part.TotalNodes)) return Error_Code;

	if (Error_Code = Read_Value(Part_API_Buffer, &Buffer_Offset, Part_API_Buffer_Size, 
		"TotalCPUs", &My_Part.TotalCPUs)) return Error_Code;

	if (Error_Code = Read_Value(Part_API_Buffer, &Buffer_Offset, Part_API_Buffer_Size, 
		"Key", &i)) return Error_Code;
	My_Part.Key = i;

	if (Error_Code = Read_Value(Part_API_Buffer, &Buffer_Offset, Part_API_Buffer_Size, 
		"StateUp", &i)) return Error_Code;
	My_Part.StateUp = i;

	if (Error_Code = Read_Value(Part_API_Buffer, &Buffer_Offset, Part_API_Buffer_Size, 
		"Shared", &i)) return Error_Code;
	My_Part.Shared = i;

	if (Error_Code = Read_Array(Part_API_Buffer, &Buffer_Offset, Part_API_Buffer_Size, 
		"NodeList", (void **)&My_Part.Nodes, (int *)NULL)) return Error_Code;

	if (Error_Code = Read_Array(Part_API_Buffer, &Buffer_Offset, Part_API_Buffer_Size, 
		"AllowGroups", (void **)&My_Part.AllowGroups, (int *)NULL)) return Error_Code;

	if (Error_Code = Read_Tag(Part_API_Buffer, &Buffer_Offset, Part_API_Buffer_Size, 
		"EndPart")) return Error_Code;

	/* Check if this is requested partition */ 
	if (strcmp(Req_Name, My_Part.Name) != 0) continue;

	/*Load values to be returned */
	*MaxTime 	= My_Part.MaxTime;
	*MaxNodes 	= My_Part.MaxNodes;
	*TotalNodes	= My_Part.TotalNodes;
	*TotalCPUs	= My_Part.TotalCPUs;
	*Key    	= (int)My_Part.Key;
	*StateUp 	= (int)My_Part.StateUp;
	*Shared 	= (int)My_Part.Shared;
	Nodes[0]	= My_Part.Nodes;
	AllowGroups[0]	= My_Part.AllowGroups;

	Last_Buffer_Offset = Buffer_Offset;
	Error_Code = Read_Value(Part_API_Buffer, &Buffer_Offset, Part_API_Buffer_Size, 
		"PartName", &Next_Name_Value);
	if (Error_Code)		/* No more records or bad tag */
	    strcpy(Next_Name, "");
	else
	    strcpy(Next_Name, Next_Name_Value);
	return 0;
    } /* while */
    return ENOENT;
} /* Load_Part_Name */
