/* Test program for mpd startup */
#include "mpd.h"
#include "mpdlib.h"
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

int  check_connections(int *, int*);
void peer_request_handler(int);

int  jobid, rank, jobsize; 
int  peer_socket_table[4096];
int  man_msgs_fd;

int main( argc, argv, envp )
int argc;
char *argv[];
char *envp[];
{
    int  i, rc;
    int  listen_socket, listen_port;
    int  peer_socket, peer_rank;
    char hostname[MAXLINE];
    char buf[MAXLINE];

    Signal( SIGUSR1, peer_request_handler ); /* when poked by manager */

    rc       = MPD_Init();
    jobid    = MPD_Job();
    rank     = MPD_Rank();
    jobsize  = MPD_Size();
    man_msgs_fd = MPD_Man_msgs_fd();
    gethostname(hostname,MAXLINE);
    mpdprintf(1, "jobid=%d rank=%d jobsize=%d on %s : hello\n",jobid,rank,jobsize,buf );

    for (i=0; i < 4096; i++)
	peer_socket_table[i] = -1;

    if (rank == 0) 
    {
	for (i=1; i < jobsize; i++)  /* for ranks 1-N */ 
	{
	    rc = 0;
	    while (rc == 0)
	    {
		rc = check_connections(&peer_rank,&peer_socket);
		if (rc > 0)
		{
		    read_line( peer_socket, buf, MAXLINE );
		    mpdprintf(1, "received from rank=%d buf=:%s:\n",peer_rank,buf);
		}
		else if (rc < 0)
		{
		    mpdprintf(1, "check_connections failed\n");
		}
	    }
	}
    }
    else
    {
	listen_port = 0;
	listen_socket = setup_network_socket(&listen_port);
	sprintf(buf,"cmd=connect_to_me host=%s port=%d\n",hostname,listen_port);
	MPD_Send_request_to_peer(jobid,0,buf);  /* to process 0 */
	peer_socket = accept_connection(listen_socket);
	sprintf(buf, "this is a msg from %d", rank );
	write(peer_socket,buf,strlen(buf));
    }

    printf("%d: CALLING FINALIZE \n",rank);

    mpdprintf(1, "rank %d exiting\n", rank );
    MPD_Finalize();
    return(0);
}


int check_connections(peer_rank,peer_socket)
int *peer_rank, *peer_socket;
{
    int i, rc, num_fds;
    struct timeval tv;
    fd_set readfds, writefds;

    FD_ZERO( &readfds );
    FD_ZERO( &writefds );

    for (i=0; i < jobsize; i++)
	if (peer_socket_table[i] != -1) 
	    FD_SET(peer_socket_table[i],&readfds);
	
    num_fds = FD_SETSIZE;
    tv.tv_sec  = 0;  /* setup for zero time (null would be indefinite) */
    tv.tv_usec = 0;
 
    rc = select( num_fds, &readfds, &writefds, NULL, &tv );

    if (rc == 0)
	return(0);
    if (rc < 0)
    {
	if (errno == EINTR)
	{
	    mpdprintf( 1, "select interrupted; returning\n" );
	    return(0);  /* probably should loop back to select here */
	}
	else
	{
	    mpdprintf( 1, "select failed; returning\n" );
	    return(-1);
	}
    }

    for (i=0; i < jobsize; i++)
    {
	if (peer_socket_table[i] != -1) 
	{
	    if (FD_ISSET(peer_socket_table[i],&readfds))
	    {
		rc = i + 1;  /* make rc > 0 */
		*peer_rank = i;
		*peer_socket = peer_socket_table[i];
		break;
	    }
	}
    }
    return(rc);
}

void peer_request_handler(signo)
int signo;
{
    int rc, peer_port, peer_rank;
    char buf[MAXLINE], peer_hostname[MAXLINE];

    mpdprintf( 1, "cli inside peer_request_handler\n" );
    rc = read_line(man_msgs_fd,buf,MAXLINE);
    mpdprintf( 1, "peer_request_handler got buf=:%s:\n",buf );
    parse_keyvals( buf );
    getval("cmd",buf);
    if (strcmp(buf,"connect_to_me") != 0)
    {
	mpdprintf(1,"bad cmd received :%s",buf);
	return;
    }
    getval("host",peer_hostname);
    getval("port",buf);
    peer_port = atoi(buf);
    getval("rank",buf);
    peer_rank = atoi(buf);
    peer_socket_table[peer_rank] = network_connect(peer_hostname,peer_port);
    mpdprintf( 1, "peer_request_handler connected on fd=%d\n",
               peer_socket_table[peer_rank]);
}

