#include <stdio.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <netinet/in.h>
#include <errno.h>
#include <sys/time.h>
#include <fcntl.h>
#include <netinet/tcp.h>
#include <sys/resource.h>

    
#define RECV_OK    0
#define RECV_EOF  -1
#define NON_RESERVED_PORT 5001

char *sys_errlist[];

/* Macros to convert from integer to net byte order and vice versa */
#define i_to_n(n)  (int) htonl( (u_long) n)
#define n_to_i(n)  (int) ntohl( (u_long) n)

char buf[1000];
struct sockaddr_in sin;

main(argc,argv)
int argc;
char *argv[];
{

    if (argc == 3)
    {
	printf("calling client\n");
	client(argv[1],atoi(argv[2]));
	printf("back from client\n");
    }
    else
    {
	printf("calling server\n");
	server();
	printf("back from server\n");
    }
}

server()
{
int i, rc, ssocket, sfd;
int optval = 1,optlen;
int flags;
int ntimes;

    setup_to_accept(5, NON_RESERVED_PORT, &ssocket);
    sfd = accept_connection(ssocket);

    flags = fcntl(sfd, F_GETFL, 0);
    if (flags < 0)
	error_check(flags,"getfl");
    flags |= O_NDELAY;
    fcntl(sfd, F_SETFL, flags);
    if (flags < 0)
	error_check(flags,"setfl");

    setsockopt(sfd,IPPROTO_TCP,TCP_NODELAY,(char *)&optval,sizeof(optval));
    rc = recv_msg(sfd, &ntimes, sizeof(int));
    printf("received ntimes=%d\n",ntimes);
    for (i=0; i < ntimes; i++)
    {
	while (!msgs_available(sfd))
	    ;
	rc = recv_msg(sfd, buf, 4);	
	rc = recv_msg(sfd, buf, 4);	
	send_msg(sfd, buf, 4);
	send_msg(sfd, buf, 4);
    }
    shutdown(sfd,0);
    shutdown(ssocket,0);
    unlink((char *)&sin);
}

client(server_host,ntimes)
char *server_host;
int ntimes;
{
int i, cfd,rc;
int optval = 1,optlen;
int flags, start_time, end_time;

    cfd = connect_to_server(server_host,NON_RESERVED_PORT);
    flags = fcntl(cfd, F_GETFL, 0);
    if (flags < 0)
	error_check(flags,"getfl");
    flags |= O_NDELAY;
    fcntl(cfd, F_SETFL, flags);
    if (flags < 0)
	error_check(flags,"setfl");

    setsockopt(cfd,IPPROTO_TCP,TCP_NODELAY,(char *)&optval,sizeof(optval));
    printf("sending ntimes=%d\n",ntimes);
    send_msg(cfd, &ntimes, sizeof(int));
    start_time = getclock();
    for (i=0; i < ntimes; i++)
    {
	send_msg(cfd, buf, 4);
	send_msg(cfd, buf, 4);
	while (!msgs_available(cfd))
	    ;
	rc = recv_msg(cfd, buf, 4);	
	rc = recv_msg(cfd, buf, 4);	
    }
    end_time = getclock();
    printf("time=%d\n",end_time-start_time);
}

msgs_available(fd)
int fd;
{
    int nfds = 0;
    fd_set read_fds;
    struct timeval tv;

    FD_ZERO(&read_fds);
    FD_SET(fd, &read_fds);
    tv.tv_sec = 0;
    tv.tv_usec = 0;
    nfds = select(24, &read_fds, 0, 0, &tv);
    if (nfds == -1)
    {
	error_check(nfds, "socket_send select");
    }
    return(nfds);
}

recv_msg(fd, buf, size)	
int fd;
char *buf;
int size;
{
    int recvd = 0;
    int n;

    while (recvd < size)
    {
	n = read(fd, buf + recvd, size - recvd);
        if (n < 0)
        {
	    continue;
            printf("%s :%d: %s\n","recvmsg ",n,sys_errlist[errno]);
	    return(0);
        }
	if (n > 0)
	    recvd += n;
    }
    return(recvd);
}


send_msg(fd, buf, size)	
int fd;
char *buf;
int size;
{
    int sent = 0;
    int sendsize,rc,n,nfds,done = 0;
    fd_set read_fds;
    struct timeval tv;

    while (sent < size)
    {
	if ((size - sent) > 4096)
	    sendsize = 4096;
	else
	    sendsize = size - sent;
        n = write(fd, buf + sent, sendsize);
        if (n < 0)
        {
            printf("%s :%d: %s\n","send_msg ",n,sys_errlist[errno]);
	    return(0);
        }
        if (n > 0)
            sent += n;
    }
    return(sent);
}

setup_to_accept(backlog, port, skt)	
int backlog;
int port;
int *skt;
{
int rc;

    sin.sin_family = AF_INET;
    sin.sin_addr.s_addr = INADDR_ANY;
    sin.sin_port = htons(port);

    *skt = socket(AF_INET, SOCK_STREAM, 0);
    error_check(*skt,"net_setup_anon_listener socket");

    rc = bind(*skt, (struct sockaddr *)&sin ,sizeof(sin));
    error_check(rc,"net_setup_listener bind");

    rc = listen(*skt, backlog);
    error_check(rc,"net_setup_listener listen");
}

int accept_connection(skt)	
int skt;
{
struct sockaddr_in from;
int fromlen;
int skt2;
int gotit;

    fromlen = sizeof(from);
    gotit = 0;
    while (!gotit)
    {
	skt2 = accept(skt, (struct sockaddr *) &from, &fromlen);
	if (skt2 == -1)
	{
	    if (errno == EINTR)
		continue;
	    else
		error_check(skt2, "accept_connection accept");
	}
	else
	    gotit = 1;
    }

    return(skt2);
}

connect_to_server(hostname, port)	
char *hostname;
int port;
{
int s;
struct sockaddr_in listener;
struct hostent *hp;
int rc;

    hp = gethostbyname(hostname);
    if (hp == NULL)
    {
	printf("connect_to_server: gethostbyname %s: %s -- exiting\n",
		hostname, sys_errlist[errno]);
	exit(99);
    }

    bzero((void *)&listener, sizeof(listener));
    bcopy((void *)hp->h_addr, (void *)&listener.sin_addr, hp->h_length);
    listener.sin_family = hp->h_addrtype;
    listener.sin_port = htons(port);

    s = socket(AF_INET, SOCK_STREAM, 0);
    error_check(s, "net_connect_to_server socket");

    rc = connect(s,(struct sockaddr *) &listener, sizeof(listener));
    error_check(rc, "net_connect_to_server connect");

    return(s);
}

error_check(val, str)	
int val;
char *str;
{
    if (val < 0)
    {
	printf("%s :%d: %s\n", str, val, sys_errlist[errno]);
	exit(1);
    }
}


getclock()
{
    int i;
    struct timeval tp;
    struct timezone tzp;

    gettimeofday(&tp, &tzp);
    i = (int) tp.tv_sec;
    i *= 1000;
    i += (int) (tp.tv_usec / 1000);
    return(i);
}
