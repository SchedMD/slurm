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
	return;
    } /* if (Allocate */

    /* DumpNode:  Dump node state information to a buffer */
    if (strncmp("DumpNode",    In_Line,  8) == 0) {	
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
	return;
    } /* if (DumpNode */

    /* DumpPart:  Dump partition state information to a buffer */
    if (strncmp("DumpPart",    In_Line,  8) == 0) {	
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
	return;
    } /* if (Dump_Part */

    /* JobSubmit:  Submit job to execute, TBD */
    if (strncmp("JobSubmit",    In_Line,  9) == 0) {	
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
	return;
    } /* if (JobSubmit */

    /* JobWillRun:  Will job run if submitted, TBD */
    if (strncmp("JobWillRun",    In_Line,  10) == 0) {	
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
	return;
    } /* if (JobWillRun */

    /* NodeConfig:   Process node configuration state on check-in */
    if (strncmp("NodeConfig",    In_Line,  10) == 0) {	
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
	return;
    } /* if (NodeConfig */

    /* Reconfigure:   Re-read configuration files */
    if (strncmp("Reconfigure",    In_Line,  11) == 0) {	
	Start_Time = clock();
	TimeStamp = NULL;
	Node_Lock();
	Part_Lock();
	Error_Code = Init_SLURM_Conf();
	if (Error_Code == 0) Error_Code = Read_SLURM_Conf(SLURM_CONF);
	Part_Unlock();
	Node_Unlock();
#if DEBUG_SYSTEM
	if (Error_Code)
	    fprintf(stderr, "Slurmctld_Req: Reconfigure error %d", Error_Code);
	else 
	    fprintf(stderr, "Slurmctld_Req: Reconfigure completed successfully");
	fprintf(stderr, "Reconfigure Time = %ld usec\n", (long)(clock() - Start_Time));
#endif
	if (Error_Code == 0)
	    send(sockfd, "SUCCESS", 8, 0);
	else
	    send(sockfd, "EINVAL", 7, 0);
	return;
    } /* if (Reconfigure */

    /* Update:   Modify the configuration of a job, node, or partition */
    if (strncmp("Update",    In_Line,  6) == 0) {	
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
		fprintf(stderr, "Slurmctld_Req: Update error %d on node %s", Error_Code, NodeName);
	    else
		fprintf(stderr, "Slurmctld_Req: Update error %d on partition %s", Error_Code, PartName);
	} else {
	    if (NodeName)
		fprintf(stderr, "Slurmctld_Req: Updated node %s", NodeName);
	    else
		fprintf(stderr, "Slurmctld_Req: Updated partition %s", PartName);
	} /* else */
	fprintf(stderr, "Update Time = %ld usec\n", (long)(clock() - Start_Time));
#endif
	if (Error_Code == 0)
	    send(sockfd, "SUCCESS", 8, 0);
	else
	    send(sockfd, "EINVAL", 7, 0);
	if (NodeName) free(NodeName);
	if (PartName) free(PartName);
	return;
    } /* if (Update */

#if DEBUG_SYSTEM
    fprintf(stderr, "Slurmctld_Req: Invalid input %s", In_Line);
#else
    syslog(LOG_WARNING, "Slurmctld_Req: Invalid input %s", In_Line);
#endif
    send(sockfd, "EINVAL", 7, 0);
    return;
} /* main */
