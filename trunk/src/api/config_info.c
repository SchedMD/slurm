/* 
 * partition_info.c - Get the partition information of SLURM
 * See slurm.h for documentation on external functions and data structures
 *
 * Author: Moe Jette, jette@llnl.gov
 */

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

char *Build_API_Buffer = NULL;
int  Build_API_Buffer_Size = 0;

#if DEBUG_MODULE
/* main is used here for module testing purposes only */
main(int argc, char * argv[]) {
    char Req_Name[BUILD_SIZE], Next_Name[BUILD_SIZE], Value[BUILD_SIZE];
    int Error_Code;

    Error_Code = Load_Build();
    if (Error_Code) {
	printf("Load_Build error %d\n", Error_Code);
	exit(1);
    } /* if */
    strcpy(Req_Name, "");	/* Start at beginning of build configuration list */
    while (Error_Code == 0) {
	Error_Code = Load_Build_Name(Req_Name, Next_Name, Value);
	if (Error_Code != 0)  {
	    printf("Load_Build_Name error %d finding %s\n", Error_Code, Req_Name);
	    break;
	} /* if */

	printf("%s=%s\n", Req_Name, Value);
	if (strlen(Next_Name) == 0) break;
	strcpy(Req_Name, Next_Name);
    } /* while */
    Free_Build_Info();
    exit(0);
} /* main */
#endif


/*
 * Free_Build_Info - Free the build information buffer (if allocated)
 */
void Free_Build_Info(void) {
    if (Build_API_Buffer) free(Build_API_Buffer);
} /* Free_Build_Info */


/*
 * Load_Build - Update the build information buffer for use by info gathering APIs
 * Output: Returns 0 if no error, EINVAL if the buffer is invalid, ENOMEM if malloc failure
 */
int Load_Build() {
    int Buffer_Offset, Buffer_Size, Error_Code, In_Size, Version;
    char *Buffer, *My_Line;
    int sockfd;
    struct sockaddr_in serv_addr;
    unsigned long My_Time;

    if (Build_API_Buffer) return 0;	/* Already loaded */

    if ((sockfd = socket(AF_INET, SOCK_STREAM, 0)) < 0) return EINVAL;
    serv_addr.sin_family	= PF_INET;
    serv_addr.sin_addr.s_addr	= inet_addr(SLURMCTLD_HOST);
    serv_addr.sin_port  	= htons(SLURMCTLD_PORT);
    if (connect(sockfd, (struct sockaddr *) &serv_addr, sizeof(serv_addr)) < 0) {
	close(sockfd); 
	return EINVAL;
    } /* if */
    if (send(sockfd, "DumpBuild", 10, 0) < 10) {
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
    Error_Code = Read_Buffer(Buffer, &Buffer_Offset, Buffer_Size, &My_Line);
    if ((Error_Code) || (strlen(My_Line) < strlen(HEAD_FORMAT))) {
#if DEBUG_SYSTEM
	fprintf(stderr, "Load_Build: Node buffer lacks valid header\n");
#else
	syslog(LOG_ERR, "Load_Build: Node buffer lacks valid header\n");
#endif
	free(Buffer);
	return EINVAL;
    } /* if */
    sscanf(My_Line, HEAD_FORMAT, &My_Time, &Version);
    if (Version != BUILD_STRUCT_VERSION) {
#if DEBUG_SYSTEM
	fprintf(stderr, "Load_Build: expect version %d, read %d\n", BUILD_STRUCT_VERSION, Version);
#else
	syslog(LOG_ERR, "Load_Build: expect version %d, read %d\n", BUILD_STRUCT_VERSION, Version);
#endif
	free(Buffer);
	return EINVAL;
    } /* if */

    Build_API_Buffer = Buffer;
    Build_API_Buffer_Size = Buffer_Size;
    return 0;
} /* Load_Build */


/* 
 * Load_Build_Name - Load the state information about the named build parameter
 * Input: Req_Name - Name of the parameter for which information is requested
 *		     if "", then get info for the first parameter in list
 *        Next_Name - Location into which the name of the next parameter is 
 *                   stored, "" if no more
 *        Value - Pointer to location into which the information is to be stored
 * Output: Req_Name - The parameter's name is stored here
 *         Next_Name - The name of the next parameter in the list is stored here
 *         Value - The parameter's state information
 *         Returns 0 on success, ENOENT if not found, or EINVAL if buffer is bad
 * NOTE:  Req_Name, Next_Name, and Value must be declared by caller with have 
 *        length BUILD_SIZE or larger
 */
int Load_Build_Name(char *Req_Name, char *Next_Name, char *Value) {
    int i, Error_Code, Version, Buffer_Offset;
    static char Next_Build_Name[BUILD_SIZE] = "";
    static Last_Buffer_Offset;
    unsigned long My_Time;
    char *My_Line;
    char My_Build_Name[BUILD_SIZE], My_Build_Value[BUILD_SIZE];

    /* Load buffer's header (data structure version and time) */
    Buffer_Offset = 0;
    Error_Code = Read_Buffer(Build_API_Buffer, &Buffer_Offset, Build_API_Buffer_Size, &My_Line);
    if (Error_Code) return Error_Code;
    sscanf(My_Line, HEAD_FORMAT, &My_Time, &Version);

    if ((strcmp(Req_Name, Next_Build_Name) == 0) && 
         (strlen(Req_Name) != 0)) Buffer_Offset=Last_Buffer_Offset;

    while (1) {	
	/* Load all info for next parameter */
	Error_Code = Read_Buffer(Build_API_Buffer, &Buffer_Offset, Build_API_Buffer_Size, &My_Line);
	if (Error_Code == EFAULT) break; /* End of buffer */
	if (Error_Code) return Error_Code;

	i=sscanf(My_Line, BUILD_STRUCT_FORMAT, My_Build_Name, My_Build_Value);
	if (i == 1) strcpy(My_Build_Value, "");		/* empty string passed */
	if (strlen(Req_Name) == 0)  strncpy(Req_Name, My_Build_Name, BUILD_SIZE);

	/* Check if this is requested parameter */ 
	if (strcmp(Req_Name, My_Build_Name) != 0) continue;

	/*Load values to be returned */
	strncpy(Value, My_Build_Value, BUILD_SIZE);

	Last_Buffer_Offset = Buffer_Offset;
	Error_Code = Read_Buffer(Build_API_Buffer, &Buffer_Offset, Build_API_Buffer_Size, &My_Line);
	if (Error_Code) {	/* No more records */
	    strcpy(Next_Build_Name, "");
	    strcpy(Next_Name, "");
	} else {
	    sscanf(My_Line, BUILD_STRUCT_FORMAT, My_Build_Name, My_Build_Value);
	    strncpy(Next_Build_Name, My_Build_Name, BUILD_SIZE);
	    strncpy(Next_Name, My_Build_Name, BUILD_SIZE);
	} /* else */
	return 0;
    } /* while */
    return ENOENT;
} /* Load_Build_Name */
