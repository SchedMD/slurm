/* 
 * reconfigure.c - Request that slurmctld re-read the configuration files
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

#if DEBUG_MODULE
/* main is used here for module testing purposes only */
main(int argc, char * argv[]) {
    int i, count, Error_Code;

    if (argc < 2) 
	count = 1;
    else
	count = atoi(argv[1]);

    for (i=0; i<count; i++) {
	Error_Code = Reconfigure();
	if (Error_Code != 0) printf("Reconfigure error %d\n", Error_Code);
    } 
    exit(0);
} /* main */
#endif


/* 
 * Reconfigure - _ Request that slurmctld re-read the configuration files
 * Output: Returns 0 on success, errno otherwise
 */
int Reconfigure() {
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
    sprintf(Request_Msg, "Reconfigure");
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
    Error_Code = atoi(Buffer);
    free(Buffer);
    return Error_Code;
} /* Reconfigure */


