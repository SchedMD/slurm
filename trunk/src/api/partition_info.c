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
    int MaxTime;			/* -1 if unlimited */
    int MaxNodes;			/* -1 if unlimited */
    int TotalNodes;			/* Total number of nodes in the partition */
    int TotalCPUs;			/* Total number of CPUs in the partition */
    char Nodes[FEATURE_SIZE];		/* Names of nodes in partition */
    char AllowGroups[FEATURE_SIZE];	/* NULL indicates ALL */
    int Key;    	 		/* 1 if SLURM distributed key is required for use of partition */
    int StateUp;			/* 1 if state is UP */
    int Shared;				/* 1 if partition can be shared */
    int Default;			/* 1 if default partition */

    Error_Code = Load_Part(&Last_Update_Time);
    if (Error_Code) {
	printf("Load_Part error %d\n", Error_Code);
	exit(1);
    } /* if */
    strcpy(Req_Name, "");	/* Start at beginning of partition list */
    while (Error_Code == 0) {
	Error_Code = Load_Part_Name(Req_Name, Next_Name, &MaxTime, &MaxNodes, 
	    &TotalNodes, &TotalCPUs, &Key, &StateUp, &Shared, &Default, 
	    Nodes, AllowGroups);
	if (Error_Code != 0)  {
	    printf("Load_Part_Name error %d finding %s\n", Error_Code, Req_Name);
	    break;
	} /* if */

	printf("Found partition Name=%s Nodes=%s MaxTime=%d MaxNodes=%d Default=%d \n", 
	    Req_Name, Nodes, MaxTime, MaxNodes, Default);
	printf("  TotalNodes=%d TotalCPUs=%d Key=%d StateUp=%d Shared=%d AllowGroups=%s\n", 
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
    char Request_Msg[64], *Buffer, *My_Line;
    int sockfd;
    struct sockaddr_in serv_addr;
    unsigned long My_Time;

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
    if (strcmp(Buffer, "NOCHANGE") == 0) {
	free(Buffer);
	return 0;
    } /* if */

    Buffer_Offset = 0;
    Error_Code = Read_Buffer(Buffer, &Buffer_Offset, Buffer_Size, &My_Line);
    if ((Error_Code) || (strlen(My_Line) < strlen(HEAD_FORMAT))) {
#if DEBUG_SYSTEM
	fprintf(stderr, "Load_Part: Node buffer lacks valid header\n");
#else
	syslog(LOG_ERR, "Load_Part: Node buffer lacks valid header\n");
#endif
	free(Buffer);
	return EINVAL;
    } /* if */
    sscanf(My_Line, HEAD_FORMAT, &My_Time, &Version);
    if (Version != PART_STRUCT_VERSION) {
#if DEBUG_SYSTEM
	fprintf(stderr, "Load_Part: expect version %d, read %d\n", PART_STRUCT_VERSION, Version);
#else
	syslog(LOG_ERR, "Load_Part: expect version %d, read %d\n", PART_STRUCT_VERSION, Version);
#endif
	free(Buffer);
	return EINVAL;
    } /* if */

    *Last_Update_Time = (time_t)My_Time;
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
 * NOTE:  Req_Name and Next_Name must be declared by caller with have length MAX_NAME_LEN or larger
 *        Nodes and AllowGroups must be declared by caller with length of FEATURE_SIZE or larger
 */
int Load_Part_Name(char *Req_Name, char *Next_Name, int *MaxTime, int *MaxNodes, 
	int *TotalNodes, int *TotalCPUs, int *Key, int *StateUp, int *Shared, int *Default,
	char *Nodes, char *AllowGroups) {
    int i, Error_Code, Version, Buffer_Offset;
    static time_t Last_Update_Time, Update_Time;
    struct Part_Record My_Part;
    int My_BitMap_Size;
    static char Next_Name_Value[MAX_NAME_LEN];
    char My_Part_Name[MAX_NAME_LEN], My_Key[MAX_NAME_LEN], My_Default[MAX_NAME_LEN];
    char My_Shared[MAX_NAME_LEN], My_State[MAX_NAME_LEN], *My_Line;
    static Last_Buffer_Offset;
    unsigned long My_Time;

    /* Load buffer's header (data structure version and time) */
    Buffer_Offset = 0;
    Error_Code = Read_Buffer(Part_API_Buffer, &Buffer_Offset, Part_API_Buffer_Size, &My_Line);
    if (Error_Code) return Error_Code;
    sscanf(My_Line, HEAD_FORMAT, &My_Time, &Version);
    Update_Time = (time_t) My_Time;

    if ((Update_Time == Last_Update_Time) && (strcmp(Req_Name,Next_Name_Value) == 0) && 
         (strlen(Req_Name) != 0)) Buffer_Offset=Last_Buffer_Offset;
    Last_Update_Time = Update_Time;

    while (1) {	
	/* Load all info for next partition */
	Error_Code = Read_Buffer(Part_API_Buffer, &Buffer_Offset, Part_API_Buffer_Size, &My_Line);
	if (Error_Code == EFAULT) break; /* End of buffer */
	if (Error_Code) return Error_Code;

	sscanf(My_Line, PART_STRUCT_FORMAT, 
		My_Part_Name, 
		&My_Part.MaxNodes, 
		&My_Part.MaxTime, 
		Nodes, 
		My_Key,
		My_Default,
		AllowGroups,
		My_Shared,
		My_State,
		&My_Part.TotalNodes, 
		&My_Part.TotalCPUs);

	if (strlen(Req_Name) == 0)  strcpy(Req_Name, My_Part_Name);

	/* Check if this is requested partition */ 
	if (strcmp(Req_Name, My_Part_Name) != 0) continue;

	/*Load values to be returned */
	*MaxTime 	= My_Part.MaxTime;
	*MaxNodes 	= My_Part.MaxNodes;
	*TotalNodes	= My_Part.TotalNodes;
	*TotalCPUs	= My_Part.TotalCPUs;
	if (strcmp(My_Key, "YES") == 0)
	    *Key = 1;
	else
	    *Key = 0;
	if (strcmp(My_Default, "YES") == 0)
	    *Default = 1;
	else
	    *Default = 0;
	if (strcmp(My_State, "UP") == 0) 
	    *StateUp = 1;
	else
	    *StateUp = 0;
	if (strcmp(My_Shared, "YES") == 0) 
	    *Shared = 1;
	else
	    *Shared = 0;

	Last_Buffer_Offset = Buffer_Offset;
	Error_Code = Read_Buffer(Part_API_Buffer, &Buffer_Offset, Part_API_Buffer_Size, &My_Line);
	if (Error_Code) {	/* No more records */
	    strcpy(Next_Name_Value, "");
	    strcpy(Next_Name, "");
	} else {
	    sscanf(My_Line, "PartitionName=%s", My_Part_Name);
	    strncpy(Next_Name_Value, My_Part_Name, MAX_NAME_LEN);
	    strncpy(Next_Name, My_Part_Name, MAX_NAME_LEN);
	} /* else */
	return 0;
    } /* while */
    return ENOENT;
} /* Load_Part_Name */
