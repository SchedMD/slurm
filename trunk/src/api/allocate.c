/* 
 * allocate.c - Allocate nodes for a job with supplied contraints
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

#if DEBUG_MODULE
/* main is used here for testing purposes only */
main(int argc, char * argv[]) {
    int Error_Code;
    char *NodeList;

    Error_Code = Allocate(
	"JobName=Job01 TotalNodes=400 TotalCPUs=1000 NodeList=lx[3000-3003] Partition=batch MinMemory=1024 MinTmpDisk=2034 Groups=students,employee MinCPUs=4 Contiguous Key=1234",
	&NodeList);
    if (Error_Code) 
	printf("Allocate error %d\n", Error_Code);
    else {
	printf("Allocate nodes %s\n", NodeList);
	free(NodeList);
    } /* else */

    while (1) {
	Error_Code = Allocate(
	    "JobName=More TotalCPUs=4000 Partition=batch Key=1234 ",
	    &NodeList);
	if (Error_Code) {
	    printf("Allocate error %d\n", Error_Code);
	    break;
	} else {
	    printf("Allocate nodes %s\n", NodeList);
	    free(NodeList);
	} /* else */
    } /* while */

    while (1) {
	Error_Code = Allocate(
	    "JobName=More TotalCPUs=40 Partition=batch Key=1234 ",
	    &NodeList);
	if (Error_Code) {
	    printf("Allocate error %d\n", Error_Code);
	    break;
	} else {
	    printf("Allocate nodes %s\n", NodeList);
	    free(NodeList);
	} /* else */
    } /* while */

    exit(0);
} /* main */
#endif


/*
 * Allocate - Allocate nodes for a job with supplied contraints. 
 * Input: Spec - Specification of the job's constraints
 *        NodeList - Place into which a node list pointer can be placed
 * Output: NodeList - List of allocated nodes
 *         Returns 0 if no error, EINVAL if the request is invalid, 
 *			EAGAIN if the request can not be satisfied at present
 * NOTE: Acceptable specifications include: JobName=<name>, NodeList=<list>, 
 *	Features=<features>, Groups=<groups>, Partition=<part_name>, Contiguous, 
 *	TotalCPUs=<number>, TotalNodes=<number>, MinCPUs=<number>, 
 *	MinMemory=<number>, MinTmpDisk=<number>, Key=<number>, Shared=<0|1>
 * NOTE: The calling function must free the allocated storage at NodeList[0]
 */
int Allocate(char *Spec, char **NodeList) {
    int Buffer_Offset, Buffer_Size, Error_Code, In_Size;
    char *Request_Msg, *Buffer;
    int sockfd;
    struct sockaddr_in serv_addr;

    if ((Spec == NULL) || (NodeList == (char **)NULL)) return EINVAL;
    Request_Msg = malloc(strlen(Spec)+10);
    if (Request_Msg == NULL) return EAGAIN;
    strcpy(Request_Msg, "Allocate ");
    strcat(Request_Msg, Spec);

    if ((sockfd = socket(AF_INET, SOCK_STREAM, 0)) < 0) return EINVAL;
    serv_addr.sin_family	= PF_INET;
    serv_addr.sin_addr.s_addr	= inet_addr(SLURMCTLD_HOST);
    serv_addr.sin_port  	= htons(SLURMCTLD_PORT);
    if (connect(sockfd, (struct sockaddr *) &serv_addr, sizeof(serv_addr)) < 0) {
	close(sockfd); 
	return EAGAIN;
    } /* if */
    if (send(sockfd, Request_Msg, strlen(Request_Msg)+1, 0) < strlen(Request_Msg)) {
	close(sockfd); 
	return EAGAIN;
    } /* if */

    Buffer = NULL;
    Buffer_Offset = 0;
    Buffer_Size = 8 * 1024;
    while (1) {
    	Buffer = realloc(Buffer, Buffer_Size);
	if (Buffer == NULL) {
	    close(sockfd); 
	    return EAGAIN;
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
    if (Buffer == NULL) return EAGAIN;

    if (strcmp(Buffer, "EAGAIN") == 0) {
	free(Buffer);
	return EAGAIN;
    } /* if */
    if (strcmp(Buffer, "EINVAL") == 0) {
	free(Buffer);
	return EINVAL;
    } /* if */
    NodeList[0] = Buffer;
    return 0;
} /* Allocate */

