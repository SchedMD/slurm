/* 
 * controller.c - Main control machine daemon for SLURM
 * See slurm.h for documentation on external functions and data structures
 *
 * NOTE: DEBUG_MODULE of read_config requires that it be loaded with 
 *       bits_bytes, partition_mgr, read_config, and node_mgr
 *
 * Author: Moe Jette, jette@llnl.gov
 */

#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <syslog.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>

#include "slurm.h"
#include "slurmlib.h"

#define BUF_SIZE 1024

int Msg_From_Root(void);
void Slurmctld_Req(int sockfd);

main(int argc, char * argv[]) {
    int Error_Code;
    int child_pid, cli_len, newsockfd, sockfd;
    struct sockaddr_in cli_addr, serv_addr;
    char Node_Name[MAX_NAME_LEN];

    Error_Code = Init_SLURM_Conf();
    if (Error_Code) {
#if DEBUG_SYSTEM
	fprintf(stderr, "slurmctld: Init_SLURM_Conf error %d\n", Error_Code);
#else
	syslog(LOG_ALERT, "slurmctld: Init_SLURM_Conf error %d\n", Error_Code);
#endif
	abort();
    } /* if */

    Error_Code = Read_SLURM_Conf(SLURM_CONF);
    if (Error_Code) {
#if DEBUG_SYSTEM
	fprintf(stderr, "slurmctld: Error %d from Read_SLURM_Conf reading %s\n", 
		Error_Code, SLURM_CONF);
#else
	syslog(LOG_ALERT, "slurmctld: Error %d from Read_SLURM_Conf reading %s\n", 
		Error_Code, SLURM_CONF);
#endif
	abort();
    } /* if */

    Error_Code = gethostname(Node_Name, MAX_NAME_LEN);
    if (Error_Code != 0) {
#if DEBUG_SYSTEM
	fprintf(stderr, "slurmctld: Error %d from gethostname\n", Error_Code);
#else
	syslog(LOG_ALERT, "slurmctld: Error %d from gethostname\n", Error_Code);
#endif
	abort();
    } /* if */
    if (strcmp(Node_Name, ControlMachine) != 0) {
#if DEBUG_SYSTEM
	fprintf(stderr, "slurmctld: This machine (%s) is not the primary control machine (%s)\n", 
		Node_Name, ControlMachine);
#else
	syslog(LOG_ERR, "slurmctld: This machine (%s) is not the primary control machine (%s)\n", 
		Node_Name, ControlMachine);
#endif
	exit(1);
    } /* if */

    if ((sockfd = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
#if DEBUG_SYSTEM
	fprintf(stderr, "slurmctld: Error %d from socket\n", errno);
#else
	syslog(LOG_ALERT, "slurmctld: Error %d from socket\n", errno);
#endif
	abort();
    } /* if */
    memset(&serv_addr, 0, sizeof(serv_addr));
    serv_addr.sin_family	= PF_INET;
    serv_addr.sin_addr.s_addr	= htonl(INADDR_ANY);
    serv_addr.sin_port  	= htons(SLURMCTLD_PORT);
    if (bind(sockfd, (struct sockaddr *) &serv_addr, sizeof(serv_addr)) < 0) {
#if DEBUG_SYSTEM
	fprintf(stderr, "slurmctld: Error %d from bind\n", errno);
#else
	syslog(LOG_ALERT, "slurmctld: Error %d from bind\n", errno);
#endif
	abort();
    } /* if */
    listen(sockfd, 5);
    while (1) {
	cli_len = sizeof(cli_addr);
	if ((newsockfd = accept(sockfd, (struct sockaddr *) &cli_addr, &cli_len)) < 0) {
#if DEBUG_SYSTEM
	    fprintf(stderr, "slurmctld: Error %d from accept\n", errno);
#else
	    syslog(LOG_ALERT, "slurmctld: Error %d from accept\n", errno);
#endif
	    abort();
	} /* if */

/* Convert to pthread, TBD */
Slurmctld_Req(newsockfd);	/* Process the request */
close(newsockfd);		/* close the new socket */

    } /* while */
} /* main */

/* 
 * Dump_Build - Dump all build parameters to a buffer
 * Input: Buffer_Ptr - Location into which a pointer to the data is to be stored.
 *                     The data buffer is actually allocated by Dump_Part and the 
 *                     calling function must free the storage.
 *         Buffer_Size - Location into which the size of the created buffer is in bytes
 * Output: Buffer_Ptr - The pointer is set to the allocated buffer.
 *         Buffer_Size - Set to size of the buffer in bytes
 *         Returns 0 if no error, errno otherwise
 * NOTE: The buffer at *Buffer_Ptr must be freed by the caller
 * NOTE: IF YOU MAKE ANY CHANGES HERE be sure to increment the value of BUILD_STRUCT_VERSION
 *       and make the corresponding changes to Load_Build_Name in api/build_info.c
 */
int Dump_Build(char **Buffer_Ptr, int *Buffer_Size) {
    char *Buffer;
    int Buffer_Offset, Buffer_Allocated, i, Record_Size;
    char Out_Line[BUILD_SIZE*2];

    Buffer_Ptr[0] = NULL;
    *Buffer_Size = 0;
    Buffer = NULL;
    Buffer_Offset = 0;
    Buffer_Allocated = 0;

    /* Write haeader, version and time */
    sprintf(Out_Line, HEAD_FORMAT, (unsigned long)time(NULL), BUILD_STRUCT_VERSION);
    if (Write_Buffer(&Buffer, &Buffer_Offset, &Buffer_Allocated, Out_Line)) goto cleanup;

    /* Write paramter records */
    sprintf(Out_Line, BUILD_STRUCT2_FORMAT, "BACKUP_INTERVAL", BACKUP_INTERVAL);
    if (Write_Buffer(&Buffer, &Buffer_Offset, &Buffer_Allocated, Out_Line)) goto cleanup;

    sprintf(Out_Line, BUILD_STRUCT_FORMAT, "BACKUP_LOCATION", BACKUP_LOCATION);
    if (Write_Buffer(&Buffer, &Buffer_Offset, &Buffer_Allocated, Out_Line)) goto cleanup;

    sprintf(Out_Line, BUILD_STRUCT_FORMAT, "CONTROL_DAEMON", CONTROL_DAEMON);
    if (Write_Buffer(&Buffer, &Buffer_Offset, &Buffer_Allocated, Out_Line)) goto cleanup;

    sprintf(Out_Line, BUILD_STRUCT2_FORMAT, "CONTROLLER_TIMEOUT", CONTROLLER_TIMEOUT);
    if (Write_Buffer(&Buffer, &Buffer_Offset, &Buffer_Allocated, Out_Line)) goto cleanup;

    sprintf(Out_Line, BUILD_STRUCT_FORMAT, "EPILOG", EPILOG);
    if (Write_Buffer(&Buffer, &Buffer_Offset, &Buffer_Allocated, Out_Line)) goto cleanup;

    sprintf(Out_Line, BUILD_STRUCT2_FORMAT, "HASH_BASE", HASH_BASE);
    if (Write_Buffer(&Buffer, &Buffer_Offset, &Buffer_Allocated, Out_Line)) goto cleanup;

    sprintf(Out_Line, BUILD_STRUCT2_FORMAT, "HEARTBEAT_INTERVAL", HEARTBEAT_INTERVAL);
    if (Write_Buffer(&Buffer, &Buffer_Offset, &Buffer_Allocated, Out_Line)) goto cleanup;

    sprintf(Out_Line, BUILD_STRUCT_FORMAT, "INIT_PROGRAM", INIT_PROGRAM);
    if (Write_Buffer(&Buffer, &Buffer_Offset, &Buffer_Allocated, Out_Line)) goto cleanup;

    sprintf(Out_Line, BUILD_STRUCT2_FORMAT, "KILL_WAIT", KILL_WAIT);
    if (Write_Buffer(&Buffer, &Buffer_Offset, &Buffer_Allocated, Out_Line)) goto cleanup;

    sprintf(Out_Line, BUILD_STRUCT_FORMAT, "PRIORITIZE", PRIORITIZE);
    if (Write_Buffer(&Buffer, &Buffer_Offset, &Buffer_Allocated, Out_Line)) goto cleanup;

    sprintf(Out_Line, BUILD_STRUCT_FORMAT, "PROLOG", PROLOG);
    if (Write_Buffer(&Buffer, &Buffer_Offset, &Buffer_Allocated, Out_Line)) goto cleanup;

    sprintf(Out_Line, BUILD_STRUCT_FORMAT, "SERVER_DAEMON", SERVER_DAEMON);
    if (Write_Buffer(&Buffer, &Buffer_Offset, &Buffer_Allocated, Out_Line)) goto cleanup;

    sprintf(Out_Line, BUILD_STRUCT2_FORMAT, "SERVER_TIMEOUT", SERVER_TIMEOUT);
    if (Write_Buffer(&Buffer, &Buffer_Offset, &Buffer_Allocated, Out_Line)) goto cleanup;

    sprintf(Out_Line, BUILD_STRUCT_FORMAT, "SLURM_CONF", SLURM_CONF);
    if (Write_Buffer(&Buffer, &Buffer_Offset, &Buffer_Allocated, Out_Line)) goto cleanup;

    sprintf(Out_Line, BUILD_STRUCT_FORMAT, "TMP_FS", TMP_FS);
    if (Write_Buffer(&Buffer, &Buffer_Offset, &Buffer_Allocated, Out_Line)) goto cleanup;

    Buffer = realloc(Buffer, Buffer_Offset);
    if (Buffer == NULL) {
#if DEBUG_SYSTEM
	fprintf(stderr, "Dump_Build: unable to allocate memory\n");
#else
	syslog(LOG_ALERT, "Dump_Build: unable to allocate memory\n");
#endif
	abort();
    } /* if */

    Buffer_Ptr[0] = Buffer;
    *Buffer_Size = Buffer_Offset;
    return 0;

cleanup:
    if (Buffer) free(Buffer);
    return EINVAL;
} /* Dump_Build */


/*
 * Slurmctld_Req - Process a slurmctld request from the given socket
 * Input: sockfd - The socket with a request to be processed
 */
void Slurmctld_Req(int sockfd) {
    int Error_Code, In_Size, i;
    char In_Line[BUF_SIZE], Node_Name[MAX_NAME_LEN];
    int CPUs, RealMemory, TmpDisk;
    char *NodeName, *PartName, *TimeStamp;
    time_t Last_Update;
    clock_t Start_Time;
    char *Dump;
    int Dump_Size, Dump_Loc;

    In_Size = recv(sockfd, In_Line, sizeof(In_Line), 0);

    /* Allocate:  Allocate resources for a job */
    if (strncmp("Allocate",   In_Line,  8) == 0) {	
	Start_Time = clock();
	NodeName = NULL;
	Error_Code = Select_Nodes(&In_Line[8], &NodeName);   /* Skip over "Allocate" */
#if DEBUG_SYSTEM
	if (Error_Code)
	    fprintf(stderr, "Slurmctld_Req: Error %d allocating resources for %s, ",
		 Error_Code, &In_Line[8]);
	else 
	    fprintf(stderr, "Slurmctld_Req: Allocated nodes %s to job %s, ", 
		NodeName, &In_Line[8]);
	fprintf(stderr, "time = %ld usec\n", (long)(clock() - Start_Time));
#endif
	if (Error_Code == 0)
	    send(sockfd, NodeName, strlen(NodeName)+1, 0);
	else if (Error_Code == EAGAIN)
	    send(sockfd, "EAGAIN", 7, 0);
	else
	    send(sockfd, "EINVAL", 7, 0);

	if (NodeName) free(NodeName);

    /* DumpBuild:  Dump build parameters to a buffer */
    } else if (strncmp("DumpBuild",    In_Line,  9) == 0) {	
	Start_Time = clock();
	Error_Code = Dump_Build(&Dump, &Dump_Size);
#if DEBUG_SYSTEM
	if (Error_Code)
	    fprintf(stderr, "Slurmctld_Req: Dump_Build error %d, ", Error_Code);
	else 
	   fprintf(stderr, "Slurmctld_Req: Dump_Build returning %d bytes, ", Dump_Size);
	fprintf(stderr, "time = %ld usec\n", (long)(clock() - Start_Time));
#endif
	if (Error_Code == 0) {
	    Dump_Loc = 0;
	    while (Dump_Size > 0) {
		i = send(sockfd, &Dump[Dump_Loc], Dump_Size, 0);
		Dump_Loc += i;
		Dump_Size -= i;
	    } /* while */
	} else
	    send(sockfd, "EINVAL", 7, 0);
	if (Dump) free(Dump);

    /* DumpNode:  Dump node state information to a buffer */
    } else if (strncmp("DumpNode",    In_Line,  8) == 0) {	
	Start_Time = clock();
	TimeStamp = NULL;
	Error_Code = Load_String(&TimeStamp, "LastUpdate=", In_Line);
	if (TimeStamp) {
	    Last_Update = strtol(TimeStamp, (char **)NULL, 10);
	    free(TimeStamp);
	} else 
	    Last_Update = (time_t) 0;
	Error_Code = Dump_Node(&Dump, &Dump_Size, &Last_Update);
#if DEBUG_SYSTEM
	if (Error_Code)
	    fprintf(stderr, "Slurmctld_Req: Dump_Node error %d, ", Error_Code);
	else 
	   fprintf(stderr, "Slurmctld_Req: Dump_Node returning %d bytes, ", Dump_Size);
	fprintf(stderr, "time = %ld usec\n", (long)(clock() - Start_Time));
#endif
	if (Dump_Size == 0) 
	    send(sockfd, "NOCHANGE", 9, 0);
	else if (Error_Code == 0) {
	    Dump_Loc = 0;
	    while (Dump_Size > 0) {
		i = send(sockfd, &Dump[Dump_Loc], Dump_Size, 0);
		Dump_Loc += i;
		Dump_Size -= i;
	    } /* while */
	} else
	    send(sockfd, "EINVAL", 7, 0);
	if (Dump) free(Dump);

    /* DumpPart:  Dump partition state information to a buffer */
    } else if (strncmp("DumpPart",    In_Line,  8) == 0) {	
	Start_Time = clock();
	TimeStamp = NULL;
	Error_Code = Load_String(&TimeStamp, "LastUpdate=", In_Line);
	if (TimeStamp) {
	    Last_Update = strtol(TimeStamp, (char **)NULL, 10);
	    free(TimeStamp);
	} else 
	    Last_Update = (time_t) 0;
	Error_Code = Dump_Part(&Dump, &Dump_Size, &Last_Update);
#if DEBUG_SYSTEM
	if (Error_Code)
	    fprintf(stderr, "Slurmctld_Req: Dump_Part error %d, ", Error_Code);
	else 
	    fprintf(stderr, "Slurmctld_Req: Dump_Part returning %d bytes, ", Dump_Size);
	fprintf(stderr, "time = %ld usec\n", (long)(clock() - Start_Time));
#endif
	if (Dump_Size == 0) 
	    send(sockfd, "NOCHANGE", 9, 0);
	else if (Error_Code == 0) {
	    Dump_Loc = 0;
	    while (Dump_Size > 0) {
		i = send(sockfd, &Dump[Dump_Loc], Dump_Size, 0);
		Dump_Loc += i;
		Dump_Size -= i;
	    } /* while */
	} else
	    send(sockfd, "EINVAL", 7, 0);
	if (Dump) free(Dump);

    /* JobSubmit:  Submit job to execute, TBD */
    } else if (strncmp("JobSubmit",    In_Line,  9) == 0) {	
	Start_Time = clock();
	TimeStamp = NULL;
	Error_Code = EINVAL;
#if DEBUG_SYSTEM
	if (Error_Code)
	    fprintf(stderr, "Slurmctld_Req: JobSubmit error %d", Error_Code);
	else 
	    fprintf(stderr, "Slurmctld_Req: JobSubmit success for %s", &In_Line[10]);
	fprintf(stderr, "JobSubmit Time = %ld usec\n", (long)(clock() - Start_Time));
#endif
	if (Error_Code == 0)
	    send(sockfd, Dump, Dump_Size, 0);
	else
	    send(sockfd, "EINVAL", 7, 0);

    /* JobWillRun:  Will job run if submitted, TBD */
    } else if (strncmp("JobWillRun",    In_Line,  10) == 0) {	
	Start_Time = clock();
	TimeStamp = NULL;
	Error_Code = EINVAL;
#if DEBUG_SYSTEM
	if (Error_Code)
	    fprintf(stderr, "Slurmctld_Req: JobWillRun error %d", Error_Code);
	else 
	    fprintf(stderr, "Slurmctld_Req: JobWillRun success for %s", &In_Line[10]);
	fprintf(stderr, "JobWillRun Time = %ld usec\n", (long)(clock() - Start_Time));
#endif
	if (Error_Code == 0)
	    send(sockfd, Dump, Dump_Size, 0);
	else
	    send(sockfd, "EINVAL", 7, 0);

    /* NodeConfig:   Process node configuration state on check-in */
    } else if (strncmp("NodeConfig",    In_Line,  10) == 0) {	
	Start_Time = clock();
	TimeStamp = NULL;
	Error_Code  = Load_String (&NodeName,   "NodeName=",   In_Line);
	if (NodeName == NULL) Error_Code = EINVAL;
	if (Error_Code == 0) Error_Code = Load_Integer(&CPUs,       "CPUs=",       In_Line);
	if (Error_Code == 0) Error_Code = Load_Integer(&RealMemory, "RealMemory=", In_Line);
	if (Error_Code == 0) Error_Code = Load_Integer(&TmpDisk,    "TmpDisk=",    In_Line);
	if (Error_Code == 0) Error_Code = Validate_Node_Specs(NodeName,CPUs,RealMemory,TmpDisk);
#if DEBUG_SYSTEM
	if (Error_Code)
	    fprintf(stderr, "Slurmctld_Req: NodeConfig error %d for %s", Error_Code, NodeName);
	else 
	    fprintf(stderr, "Slurmctld_Req: NodeConfig for %s", NodeName);
	fprintf(stderr, "NodeConfig Time = %ld usec\n", (long)(clock() - Start_Time));
#endif
	if (Error_Code == 0)
	    send(sockfd, Dump, Dump_Size, 0);
	else
	    send(sockfd, "EINVAL", 7, 0);
	if (NodeName) free(NodeName);

    /* Reconfigure:   Re-read configuration files */
    } else if (strncmp("Reconfigure",    In_Line,  11) == 0) {	
	Start_Time = clock();
	TimeStamp = NULL;
	Error_Code = Init_SLURM_Conf();
	if (Error_Code == 0) Error_Code = Read_SLURM_Conf(SLURM_CONF);
#if DEBUG_SYSTEM
	if (Error_Code)
	    fprintf(stderr, "Slurmctld_Req: Reconfigure error %d, ", Error_Code);
	else 
	    fprintf(stderr, "Slurmctld_Req: Reconfigure completed successfully, ");
	fprintf(stderr, "time = %ld usec\n", (long)(clock() - Start_Time));
#endif
	sprintf(In_Line, "%d", Error_Code);
	send(sockfd, In_Line, strlen(In_Line)+1, 0);

    /* Update:   Modify the configuration of a job, node, or partition */
    } else if (strncmp("Update",    In_Line,  6) == 0) {	
	Start_Time = clock();
	NodeName = PartName = NULL;
	Error_Code = Load_String(&NodeName, "NodeName=", In_Line);
	if ((Error_Code == 0) && (NodeName != NULL))
	    Error_Code = Update_Node(NodeName, &In_Line[6]);  /* Skip over "Update" */
	else {
	    Error_Code = Load_String(&PartName, "PartitionName=", In_Line);
	    if ((Error_Code == 0) && (PartName != NULL)) {
		Error_Code = Update_Part(PartName, &In_Line[6]);  /* Skip over "Update" */
	    } /* if */
	} /* else */
#if DEBUG_SYSTEM
	if (Error_Code) {
	    if (NodeName)
		fprintf(stderr, "Slurmctld_Req: Update error %d on node %s, ", Error_Code, NodeName);
	    else
		fprintf(stderr, "Slurmctld_Req: Update error %d on partition %s, ", Error_Code, PartName);
	} else {
	    if (NodeName)
		fprintf(stderr, "Slurmctld_Req: Updated node %s, ", NodeName);
	    else
		fprintf(stderr, "Slurmctld_Req: Updated partition %s, ", PartName);
	} /* else */
	fprintf(stderr, "time = %ld usec\n", (long)(clock() - Start_Time));
#endif
	sprintf(In_Line, "%d", Error_Code);
	send(sockfd, In_Line, strlen(In_Line)+1, 0);

	if (NodeName) free(NodeName);
	if (PartName) free(PartName);

    } else {
#if DEBUG_SYSTEM
	fprintf(stderr, "Slurmctld_Req: Invalid request %s\n", In_Line);
#else
	syslog(LOG_WARNING, "Slurmctld_Req: Invalid request %s\n", In_Line);
#endif
	send(sockfd, "EINVAL", 7, 0);
    } /* else */
    return;
} /* main */
