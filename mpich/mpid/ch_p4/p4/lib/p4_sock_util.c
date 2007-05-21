#include "p4.h"
#include "p4_sys.h"
/* p4_net_utils.h generally would suffice here */

#ifdef SCYLD_BEOWULF
#include <sys/bproc.h>
#endif

/* Type for get/setsockopt calls */
#ifdef USE_SOCKLEN_T
typedef socklen_t p4_sockopt_len_t;
#elif defined(USE_SIZE_T_FOR_SOCKLEN_T)
typedef size_t p4_sockopt_len_t;
#else
typedef int p4_sockopt_len_t;
#endif

/*  removed 11/27/94 by RL.  Causes problems in FreeBSD and is not used.
extern int errno;
extern char *sys_errlist[];
*/

/* getenv is part of stdlib.h */
#ifndef HAVE_STDLIB_H
extern char *getenv();
#endif

#ifdef HAVE_ARPA_INET_H
/* prototype for inet_ntoa() */
#include <arpa/inet.h>
#endif

#if defined(HAVE_WRITEV) && defined(HAVE_SYS_UIO_H)
#include <sys/uio.h>
#endif
/*
 *    Utility routines for socket hacking in p4:
 *        P4VOID p4_socket_control( argstr ) 
 *        P4VOID net_set_sockbuf_size(size, skt)
 *        P4VOID net_setup_listener(backlog, port, skt)
 *        P4VOID net_setup_anon_listener(backlog, port, skt)
 *        int net_accept(skt)
 *        int net_conn_to_listener(hostname, port, num_tries)
 *        int net_recv(fd, buf, size)
 *        P4VOID net_send(fd, buf, size)
 *        get_inet_addr(addr)
 *        get_inet_addr_str(str)
 *        dump_sockaddr(who, sa)
 *        dump_sockinfo(msg, fd)
 */

/* Forward references */
static void get_sock_info_by_hostname ( char *, struct sockaddr_in ** );

/*
 * Socket control - allows various socket parameters to be set through
 * the command line.  The format is
 * -p4sctrl bufsize=n:winsize=n:netsendw=y/n:stat=y/n:netrecvw=y/n:writev=y/n
 *
 * For example
 *   -p4sctrl bufsize=64:netsendw=y
 * selects 64 k socket buffers and uses the alternate net_send routine
 * 
 * bufsize is in k
 * netsendw is either y or n 
 *
 */

#define COLLECT_PERF_STAT
#ifdef COLLECT_PERF_STAT
static int n_send_w_calls = 0;
static int n_send_eagain = 0;
static int n_send_max = -1;
static int n_send_looped = 0;
static int n_send_loopcnt = 0;
static int n_writev_first = 0;

static int n_recv_calls = 0;
static int n_recv_eagain = 0;
static int n_recv_select = 0;
static int n_recv_max = 0;
static int n_recv_maxloop = 0;
#define COLLECT_STAT(a) a
#else
#define COLLECT_STAT(a)
#endif

/* Local variable controling socket behavior */
/*
 * After some testing, the following defaults seem appropriate:
 *    net_send_w   Yes
 *    net_recv_w   Yes
 *    writev       Yes
 *    readb        No
 * We may want to encourage a socket buffer size of 32k or 64k
 */
/* SOCK_BUF_SIZE is defined in p4_sock_util.h */
static int p4_default_sock_buf_size = SOCK_BUFF_SIZE;  
/* use_net_send_w selects a form of netsend that uses a blocking (waiting)
   select when writes fail (because the socket buffer is full) */
static int p4_use_net_send_w = 1;

/* net_recv_w is a special test in the net_recv code that allows the 
   net_recv to use select to wait for an incoming message */
static int p4_use_net_recv_w = 1;

/* P4_WINSHIFT can also override this */
static int p4_default_win_shft = 0;

/* Whether to output statistics on the performance of net_send_w */
static int p4_output_socket_stat = 0;

static int p4_use_writev = 1;

/* switch the fd to blocking mode for the duration of a net_recv .  
   Requires netrecvw be false */
static int p4_use_readb = 0;

/* in_str is foo=value; find value and copy to out_str */
static void p4_copy_parm( char *in_str, char *out_str, int out_size )
{
    while (*in_str && *in_str != '=') in_str++;
    if (*in_str != '=') { out_str[0] = 0; return; }
    in_str++;
    while (*in_str && *in_str != ':' && out_size-- > 0) {
	*out_str++ = *in_str++;
    }
    *out_str = 0;
}

void p4_socket_control( char *argstr )
{
    char *p;
    char digits[10], value[10];
    char *endptr;
    int  val;

    if (!*argstr) return;

    p = argstr;
    while (p) {
	if (strncmp("bufsize=",p,8) == 0) {
	    /* P4_SOCKBUFSIZE */
	    p4_copy_parm( p, digits, 10 );
	    val = strtol( digits, &endptr, 10 ) * 1024;
	    if (endptr && *endptr == '\0') 
		p4_default_sock_buf_size = val; 
	    p4_dprintfl( 5, "default sockbuf size is %d\n", 
			 p4_default_sock_buf_size );
	}
	else if (strncmp( "winsize=",p,8) == 0) {
	    /* P4_WINSHIFT */
	    p4_copy_parm( p, digits, 10 );
	    val = strtol( digits, &endptr, 10 ) * 1024;
	    if (endptr && *endptr == '\0') 
		p4_default_win_shft = val;
	    p4_dprintfl( 5, "default win shift size is %d\n",
			 p4_default_win_shft );
	}
	else if (strncmp( "netsendw=",p,9) == 0) {
	    p4_copy_parm( p, value, 2 );
	    p4_use_net_send_w = (value[0] == 'y');
	    p4_dprintfl( 5, "Using net_send_w = %d\n", p4_use_net_send_w );
	}
	else if (strncmp( "netrecvw=",p,9) == 0) {
	    p4_copy_parm( p, value, 2 );
	    p4_use_net_recv_w = (value[0] == 'y');
	    p4_dprintfl( 5, "Using net_recv_w = %d\n", p4_use_net_recv_w );
	}
	else if (strncmp( "stat=",p,5) == 0) {
	    p4_copy_parm( p, value, 2 );
	    p4_output_socket_stat = (value[0] == 'y');
	    p4_dprintfl( 5, "Socket stat = %d\n", p4_output_socket_stat );
	}
	else if (strncmp( "writev=",p,7) == 0) {
	    p4_copy_parm( p, value, 2 );
	    p4_use_writev = (value[0] == 'y');
	    p4_dprintfl( 5, "Writev = %d\n", p4_use_writev );
	}
	else if (strncmp( "readb=",p,6) == 0) {
	    p4_copy_parm( p, value, 2 );
	    p4_use_readb = (value[0] == 'y');
	    p4_dprintfl( 5, "Read with blocking = %d\n", p4_use_readb );
	}
	p = strchr( p, ':' );
	if (p && *p) p++;
    }
}

/*
 *    Setup a listener:
 *        get a socket
 *        get a port
 *        listen on the port
 *
 *    Note that this does NOT actually start a listener process, but
 *    merely does the listen command.  It might be executed by a
 *    listener process, but we commonly use it prior to actually
 *    forking off the listener.
 */

/*
   Still needed:
   The prototypes for getsockopt, accept, etc pass the address of an
   integer of some kind to hold a length or other output value.
   Unfortunately, there is no standardization for this.  
   AIX: size_t
   Solaris, LINUX: socklen_t
   IRIX, SunOS: int
 */

/* 
 * If size is -1, get the size from either the environment (P4_SOCKBUFSIZE) or
 * the default (which may have been set through the command line)
 */
P4VOID net_set_sockbuf_size(int size, int skt)	/* 7/12/95, bri@sgi.com */
{
    int rc;
    char *env_value;
    int rsz,ssz;
    p4_sockopt_len_t dummy;
#ifdef TCP_WINSHIFT
    int shft = 0; /* Window shift; helpful on CRAY */
#endif
    /*
     * Need big honking socket buffers for fast honking networks.  It
     * would be nice if these would "autotune" for the underlying network,
     * but until then, we'll let the user specify socket send/recv buffer
     * sizes with P4_SOCKBUFSIZE.
     *
     */

#ifdef CAN_DO_SETSOCKOPT
    /* For the environment variables to work, the user really needs to
       set them in their .cshrc file (otherwise, the spawned processes
       may not get the correct values).

       Rumor has it that 0x40000 is a good size for AIX 4.x
     */
    /* 
     * Take the size either from the environment variable or from the
     * default set in p4_sock_util.h .
     */
    if (size <= 0)
    {
	    env_value = getenv("P4_SOCKBUFSIZE");
	    if (env_value) 
		size = atoi(env_value);
	    else 
		size = p4_default_sock_buf_size;
#ifdef TCP_WINSHIFT
	    shft = p4_default_win_shft;
            env_value = getenv("P4_WINSHIFT");
            if (env_value) shft = atoi(env_value);
#endif
    }

    if (size > 0)
    {
	    	/* Set Send & Receive Socket Buffers */

	    SYSCALL_P4(rc, setsockopt(skt,SOL_SOCKET,SO_SNDBUF,(char *)&size,sizeof(size)));
	    /* These should only generate informational messages ..., 
	       particularly for something like ENOBUFS */
	    if (rc < 0) {
		perror( "Set SO_SNDBUF" );
		p4_error("net_set_sockbuf_size socket", skt);
		}
	    SYSCALL_P4(rc, setsockopt(skt,SOL_SOCKET,SO_RCVBUF,(char *)&size,sizeof(size)));
	    if (rc < 0) {
		perror( "Set SO_RCVBUF" );
		p4_error("net_set_sockbuf_size socket", skt);
		}

	    	/* Fetch Back the Newly Set Sizes */

            dummy = sizeof(ssz);
            rc = getsockopt(skt,SOL_SOCKET,SO_SNDBUF,(char *)&ssz,&dummy);
            dummy = sizeof(rsz);
            rc = getsockopt(skt,SOL_SOCKET,SO_RCVBUF,(char *)&rsz,&dummy);

	    p4_dprintfl(80,
			"net_set_sockbuf_size: skt %d, new sizes = [%d,%d]\n",
			skt,ssz,rsz);
    }

#ifdef TCP_WINSHIFT
    /* 
       This code came from Dan Anderson (anderson@ncar.ucar.edu) for the
       CRAYs.  This is for systems that don't handle buffer sizes greater
       than 16 bits by default. An alternate mechanism is to do something
       like this:

	winshift = 0;
	bufsiz = SOCK_BUFF_SIZE; (use the actual size)
	while (bufsiz > 0XFFFF) {
		bufsiz >>= 1;
		++winshift;
	}

     */
    if ( shft > 0)
    {
	int wsarray[3];

	/* Set socket WINSHIFT */
        dummy = sizeof(wsarray);
        getsockopt(skt,IPPROTO_TCP,TCP_WINSHIFT,&wsarray,&dummy);
        if(wsarray[1] != shft){
	    
	    dummy = sizeof(shft);

            SYSCALL_P4(rc, setsockopt(skt,IPPROTO_TCP,TCP_WINSHIFT,&shft,dummy));
            if (rc < 0) {
		char hostname[MAXHOSTNAMELEN];
		gethostname_p4(hostname,255);
			 fprintf(stdout,
                        "ERROR_WINSHIFT in %s rc=%d, shft=%d, size_shft=%d \n",
                        hostname, rc,shft,(int)dummy);
                         p4_error("net_set_WINSHIFT socket", skt);}

                /* Fetch Back the Newly Set Sizes */

            dummy = sizeof(wsarray);
            rc = getsockopt(skt,IPPROTO_TCP,TCP_WINSHIFT,&wsarray,&dummy);

            p4_dprintfl(80,
                "net_set_sockbuf_WINSHIFT: skt %d, new values = [%x,%d,%d]\n",
                        skt,wsarray[0],wsarray[1],wsarray[2]);
        }
      }

#endif

#ifdef TCP_FASTACK
    { int arg;
    /*
      Some SGI systems will delay acks unless this field is set (even with
      TCP_NODELAY set!).  Without this, occassional 5 second (!) delays
      are introduced.
     */
    arg = 1;
    SYSCALL_P4(rc,setsockopt(skt,IPPROTO_TCP,TCP_FASTACK,&arg,sizeof(arg)));
    }
#endif

#endif
}

P4VOID net_setup_listener(int backlog, int port, int *skt)
{
    struct sockaddr_in s_in;
    int rc, optval = P4_TRUE;

    SYSCALL_P4(*skt, socket(AF_INET, SOCK_STREAM, 0));
    if (*skt < 0)
	p4_error("net_setup_listener socket", *skt);

#ifdef CAN_DO_SETSOCKOPT
    net_set_sockbuf_size(-1,*skt);     /* 7/12/95, bri@sgi.com */
    SYSCALL_P4(rc,setsockopt(*skt, IPPROTO_TCP, TCP_NODELAY, (char *) &optval, sizeof(optval)));

    if (p4_debug_level > 79)
	p4_print_sock_params( *skt );
#endif

    s_in.sin_family	= AF_INET;
    s_in.sin_addr.s_addr	= INADDR_ANY;
    s_in.sin_port	= htons(port);

    SYSCALL_P4(rc, bind(*skt, (struct sockaddr *) & s_in, sizeof(s_in)));
    if (rc < 0)
	p4_error("net_setup_listener bind", -1);

    SYSCALL_P4(rc, listen(*skt, backlog));
    if (rc < 0)
	p4_error("net_setup_listener listen", -1);
}

/* This sets up the sockets but not the listener process */
P4VOID net_setup_anon_listener(int backlog, int *port, int *skt)
{
    int rc;
    p4_sockopt_len_t sinlen;
    struct sockaddr_in s_in;
    int optval = P4_TRUE;

    SYSCALL_P4(*skt, socket(AF_INET, SOCK_STREAM, 0));
    if (*skt < 0)
	p4_error("net_setup_anon_listener socket", *skt);

#ifdef CAN_DO_SETSOCKOPT
    net_set_sockbuf_size(-1,*skt);	/* 7/12/95, bri@sgi.com */
    SYSCALL_P4(rc, setsockopt(*skt, IPPROTO_TCP, TCP_NODELAY, (char *) &optval, sizeof(optval)));

    if (p4_debug_level > 79)
	p4_print_sock_params( *skt );
#endif

    s_in.sin_family = AF_INET;
    s_in.sin_addr.s_addr = INADDR_ANY;
    s_in.sin_port = htons(0);

    sinlen = sizeof(s_in);

    SYSCALL_P4(rc, bind(*skt, (struct sockaddr *) & s_in, sizeof(s_in)));
    if (rc < 0)
	p4_error("net_setup_anon_listener bind", -1);

    SYSCALL_P4(rc, listen(*skt, backlog));
    if (rc < 0)
	p4_error("net_setup_anon_listener listen", -1);

    getsockname(*skt, (struct sockaddr *) & s_in, &sinlen);
    *port = ntohs(s_in.sin_port);
}

/*
  Accept a connection on socket skt and return fd of new connection.
  */
int net_accept(int skt)
{
    struct sockaddr_in from;
    int rc, flags, skt2, gotit, sockbuffsize;
    p4_sockopt_len_t fromlen;
    int optval = P4_TRUE;

    /* dump_sockinfo("net_accept call of dumpsockinfo \n",skt); */
    fromlen = sizeof(from);
    gotit = 0;
    while (!gotit)
    {
	p4_dprintfl(60, "net_accept - waiting for accept on %d.\n",skt);
	SYSCALL_P4(skt2, accept(skt, (struct sockaddr *) &from, &fromlen));
	if (skt2 < 0)
	    p4_error("net_accept accept", skt2);
	else
	    gotit = 1;
	p4_dprintfl(60, "net_accept - got accept\n");
    }

#if defined(CAN_DO_SETSOCKOPT) && !defined(SET_SOCK_BUF_SIZE)
    net_set_sockbuf_size(-1,skt2);     /* 7/12/95, bri@sgi.com */
#endif

#ifdef CAN_DO_SETSOCKOPT
    SYSCALL_P4(rc, setsockopt(skt2, IPPROTO_TCP, TCP_NODELAY, (char *) &optval, sizeof(optval)));

    sockbuffsize = p4_default_sock_buf_size;

#ifdef SET_SOCK_BUF_SIZE
      if (setsockopt(skt2,SOL_SOCKET,SO_RCVBUF,(char *)&sockbuffsize,sizeof(sockbuffsize)))
      p4_dprintf("net_accept: setsockopt rcvbuf failed\n");
      if (setsockopt(skt2,SOL_SOCKET,SO_SNDBUF,(char *)&sockbuffsize,sizeof(sockbuffsize)))
      p4_dprintf("net_accept: setsockopt sndbuf failed\n");
#endif

    if (p4_debug_level > 79)
	p4_print_sock_params( skt2 );
#endif
    /* Peter Krauss suggested eliminating these lines for HPs  */
    flags = fcntl(skt2, F_GETFL, 0);
    if (flags < 0)
	p4_error("net_accept fcntl1", flags);
#   if defined(HP)
    flags |= O_NONBLOCK;
#   else
    flags |= O_NDELAY;
#   endif
#   if defined(RS6000)
    flags |= O_NONBLOCK;
#   endif
    flags = fcntl(skt2, F_SETFL, flags);
    if (flags < 0)
	p4_error("net_accept fcntl2", flags);
    return (skt2);
}

static void 
get_sock_info_by_hostname(char *hostname, struct sockaddr_in **sockinfo)
{
#ifndef P4_WITH_MPD
    int i;

    p4_dprintfl( 91, "Starting get_sock_info_by_hostname\n");
    if (p4_global) {
	p4_dprintfl( 90, "looking at %d hosts\n", 
		    p4_global->num_in_proctable );
	for (i = 0; i < p4_global->num_in_proctable; i++) {
	    p4_dprintfl(90,"looking up (%s), looking at (%s)\n",
			hostname,p4_global->proctable[i].host_name);
	    if (strcmp(p4_global->proctable[i].host_name,hostname) == 0) {
#ifdef LAZY_GETHOSTBYNAME
	      p4_procgroup_setsockaddr( &p4_global->proctable[i] );
#endif
		if (p4_global->proctable[i].sockaddr.sin_port == 0)
		    p4_error( "Uninitialized sockaddr port",i);
		*sockinfo = &(p4_global->proctable[i].sockaddr);
		return;
		}
	    }
	}
#endif

/* Error, no sockinfo.
   Try to get it from the hostname (this is NOT signal safe, so we 
   had better not be in a signal handler.  This MAY be ok for the listener) */
    p4_dprintfl(40, "get_sock_info_by_hostname: calling gethostbyname for %s\n",
      hostname);
    {
    struct hostent *hp = gethostbyname_p4( hostname );
    static struct sockaddr_in listener_sockaddr;
    if (hp) {
	bzero((P4VOID *) &listener_sockaddr, sizeof(listener_sockaddr));
	if (hp->h_length != 4)
	    p4_error("get_sock_info_by_hostname: hp length", hp->h_length);
	bcopy((P4VOID *) hp->h_addr, (P4VOID *) &listener_sockaddr.sin_addr, 
	      hp->h_length);
	listener_sockaddr.sin_family = hp->h_addrtype;
	*sockinfo = &listener_sockaddr;
	return;
	}
    }

    *sockinfo = 0;
    p4_error("Unknown host in getting sockinfo from proctable",-1);
}

/*
 * We must be careful here is using the sockinfo information from 
 * get_sock_info_by_hostname.  That routine returns a *pointer* to the
 * socket info, which is ok for readonly data, but we will need to 
 * have a modifiable version (so that we can set the indicated port).  
 * Thus, we first get a pointer to the readonly structure, then make 
 * a local copy of it.  Thanks to Peter Wycoff for finding this.
 */
int net_conn_to_listener(char *hostname, int port, int num_tries)
{
    int flags, rc, s;
    struct sockaddr_in sockinfo;
    struct sockaddr_in *sockinfo_ro; /* _ro for Read-Only */
    P4BOOL optval = P4_TRUE;
    P4BOOL connected = P4_FALSE;

    p4_dprintfl(80, "net_conn_to_listener: host=%s port=%d\n", hostname, port);
    /* gethostchange -RL */
/*
    bzero((P4VOID *) &listener, sizeof(listener));
    bcopy((P4VOID *) hp->h_addr, (P4VOID *) &listener.sin_addr, hp->h_length);
    listener.sin_family = hp->h_addrtype;
    listener.sin_port = htons(port);
*/
    get_sock_info_by_hostname(hostname,&sockinfo_ro);
    memcpy(&sockinfo, sockinfo_ro, sizeof(sockinfo));
    sockinfo.sin_port = htons(port);
#if !defined(CRAY)
    dump_sockaddr("sockinfo",&sockinfo);
#endif
    connected = P4_FALSE;
    s = -1;
    while (!connected && num_tries)
    {
	SYSCALL_P4(s, socket(AF_INET, SOCK_STREAM, 0));
	if (s < 0)
	    p4_error("net_conn_to_listener socket", s);

	p4_dprintfl(80,"net_conn_to_listener socket fd=%d\n", s );
#ifdef CAN_DO_SETSOCKOPT
        net_set_sockbuf_size(-1,s);    /* 7/12/95, bri@sgi.com */
	SYSCALL_P4(rc, setsockopt(s,IPPROTO_TCP,TCP_NODELAY,(char *) &optval,sizeof(optval)));
	if (p4_debug_level > 79)
	    p4_print_sock_params( s );
#       endif

	SYSCALL_P4(rc, connect(s, (struct sockaddr *) &sockinfo,
			       sizeof(struct sockaddr_in)));
	if (rc < 0)
	{
	    /* Since the socket is not yet non-blocking, EINPROGRESS should not
	       happen.  Other errors are fatal to the socket */
	    p4_dprintfl( 70, "Connect failed; closed socket %d\n", s );
	    if (p4_debug_level > 70) {
		/* Give the reason that the connection failed. */
		perror("Connection failed for reason: ");
	    }
	    close(s);
	    s = -1;
	    if (--num_tries)
	    {
		p4_dprintfl(60,"net_conn_to_listener: connect to %s failed; will try %d more times \n",hostname,num_tries);
		sleep(2);
	    }
	}
	else
	{
	    connected = P4_TRUE;
	    p4_dprintfl(70,"net_conn_to_listener: connected to %s\n",hostname);
	}
    }
    if (!connected)
	return(-1);

    /* Peter Krauss suggested eliminating these lines for HPs */
    flags = fcntl(s, F_GETFL, 0);
    if (flags < 0)
	p4_error("net_conn_to_listener fcntl1", flags);
#   if defined(HP)
    flags |= O_NONBLOCK;
#   else
    flags |= O_NDELAY;
#   endif
#   if defined(RS6000)
    flags |= O_NONBLOCK;
#   endif
    flags = fcntl(s, F_SETFL, flags);
    if (flags < 0)
	p4_error("net_conn_to_listener fcntl2", flags);

    return (s);
}

int net_recv(int fd, P4VOID *in_buf, int size)
{
    int recvd = 0;
    int n;
    int read_counter = 0;
    int block_counter = 0;
    int eof_counter = 0;
    char *buf = (char *)in_buf;
    int set_fd_blocking = 0;
    int orig_flags = 0;  /* Set to keep gcc quiet */
#if defined(P4SYSV) && !defined(NONBLOCKING_READ_WORKS)
    int n1 = 0;
    struct timeval tv;
    fd_set read_fds;
    int rc;
    char tempbuf[1];
#endif
    COLLECT_STAT(int n_loop = 0;);

    COLLECT_STAT(n_recv_calls++);

    p4_dprintfl( 99, "Beginning net_recv of %d on fd %d\n", size, fd );
    while (recvd < size)
    {
	read_counter++;

	SYSCALL_P4(n, read(fd, buf + recvd, size - recvd));
	if (n == 0)		/* maybe EOF, maybe not */
#if defined(P4SYSV) && !defined(NONBLOCKING_READ_WORKS)
	{
	    eof_counter++;

	    tv.tv_sec = 5;
	    tv.tv_usec = 0;
	    FD_ZERO(&read_fds);
	    FD_SET(fd, &read_fds);
	    SYSCALL_P4(n1, select(fd+1, &read_fds, 0, 0, &tv));
	    if (n1 == 1  &&  FD_ISSET(fd, &read_fds))
	    {
		rc = recv(fd, tempbuf, 1, MSG_PEEK);
		if (rc == -1)
		{
		    /* -1 indicates ewouldblock (eagain) (check errno) */
		    p4_error("net_recv recv:  got -1", -1);
		}
		if (rc == 0)	/* if eof */
		{
		    /* eof; a process has closed its socket; may have died */
		    p4_error("net_recv recv:  EOF on socket", read_counter);
		}
		else
		    continue;
	    }
	    sleep(1);
	    if (eof_counter < 5)
		continue;
	    else
		p4_error("net_recv read:  probable EOF on socket fd", fd );
	}
#else
	{
	    /* Except on SYSV, n == 0 is EOF */
	    /* Note that this is an error even during rundown because sockets
	       should be closed with a "close socket" message first. */
	    p4_error("net_recv read:  probable EOF on socket", read_counter);
        }
#endif
	if (n < 0)
	{
	    /* EAGAIN is really POSIX, so we check for either EAGAIN 
	       or EWOULDBLOCK.  Note some systems set EAGAIN == EWOULDBLOCK
	     */
	    /* Solaris 2.5 occasionally sets n == -1 and errno == 0 (!!).
	       since n == -1 and errno == 0 is invalid (i.e., a bug in read),
	       it should be safe to treat it as EAGAIN and to try the
	       read once more (probably a race in the kernel)
	     */
	    if (errno == EAGAIN || errno == EWOULDBLOCK || errno == 0)
	    {
		COLLECT_STAT(n_recv_eagain++);
		block_counter++;
		/* Use a select here to wait for more data
		   to arrive.  This may give better performance, 
		   particularly when the system is actively involved in
		   trying to get the message to us
		 */
		if (p4_use_net_recv_w) {
		    fd_set         lread_fds;  /* l is for local */
		    struct timeval ltv;
		    int            ln1;
		    ltv.tv_sec = 5;     /* This is arbitrary */
		    ltv.tv_usec = 0;
		    FD_ZERO(&lread_fds);
		    FD_SET(fd, &lread_fds);
		    COLLECT_STAT(n_recv_select++);
		    SYSCALL_P4(ln1, select(fd+1, &lread_fds, 0, 0, &ltv));
		}
		else if (p4_use_readb && !set_fd_blocking) {
		    int flags;
		    set_fd_blocking = 1;
		    /* If we cached these flags in the p4 structure 
		       associated with the fd, we could avoid the F_GETFL */
		    flags = fcntl( fd, F_GETFL, 0 );
		    orig_flags = flags;
		    flags &= ~O_NDELAY;
		    fcntl( fd, F_SETFL, flags );
		}
		continue;
	    }
	    else {
		/* A closed socket can cause this to happen. */
		p4_dprintf( "net_recv failed for fd = %d\n", fd );
		p4_error("net_recv read, errno = ", errno);
	    }
	}
	recvd += n;
	COLLECT_STAT( if (n > n_recv_max) n_recv_max = n; );
	COLLECT_STAT( if (recvd < size) n_loop++);
    }
    p4_dprintfl( 99, 
		"Ending net_recv of %d on fd %d (eof_c = %d, block = %d)\n", 
		 size, fd, eof_counter, block_counter );
    COLLECT_STAT(if (n_loop > n_recv_maxloop) n_recv_maxloop = n_loop;);
    if (set_fd_blocking) fcntl( fd, F_SETFL, orig_flags );
    return (recvd);
}

/* flag --> fromid < toid; tie-breaker to avoid 2 procs rcving at same time */
/* typically set false for small internal messages, esp. when ids may not */
/*     yet be available */
/* set true for user msgs which may be quite large */
int net_send(int fd, P4VOID *in_buf, int size, int flag)
{
    struct p4_msg *dmsg;
    int n, sent = 0;
    int write_counter = 0;
    int block_counter = 0;
    char *buf = (char *)in_buf;

    /* net_send_w is a tuned version of net_send */
    if (p4_use_net_send_w) return net_send_w( fd, in_buf, size, flag );

    p4_dprintfl( 99, "Starting net_send of %d on fd %d\n", size, fd );
    while (sent < size)
    {
	write_counter++;		/* for debugging */
	SYSCALL_P4(n, write(fd, buf + sent, size - sent));
	if (n < 0)
	{
	    /* See net_read; these are often the same and EAGAIN is POSIX */
	    /* Solaris sometimes sets errno to 0 even though n is -1 (i.e.,
	       a bug in Solaris); we treat this as EAGAIN */
	    if (errno == EAGAIN || errno == EWOULDBLOCK || errno == 0)
	    {
/*	        
                p4_dprintfl( 90, "write net_send in EAGAIN with %d left", 
			     size ); 
*/
		block_counter++;
		if (flag)
		{
		    /* Someone may be writing to us ... */
		    if (socket_msgs_available())
		    {
			dmsg = socket_recv( P4_FALSE );
			/* close of a connection may return a null msg */
			if (dmsg) 
			    queue_p4_message(dmsg, 
					     p4_local->queued_messages);
		    }
		}
		continue;
	    }
	    else
	    {
		if (p4_local->in_wait_for_exit) {
		    /* Exit the while if we can't send a close message */
		    break;
		}
		p4_dprintf("net_send: could not write to fd=%d, errno = %d\n",
			   fd, errno);
		p4_error("net_send write", n);
		break;    /* Allow p4_error() to return in case of a 
			     recursive call to p4_error */
	    }
	}
	sent += n;
    }
    p4_dprintfl( 99, "Ending net_send of %d on fd %d (blocked %d times)\n", 
		 size, fd, block_counter );
    return (sent);
}

/* net_send_w is a special version of net_send that uses select to wait on
 * *write* access to the socket as well as read access when a message cannot
 * be sent.  This keeps p4 from endless looping when it can't send. 
 */

int net_send_w(int fd, void *in_buf, int size, int flag)

/* flag --> fromid < toid; tie-breaker to avoid 2 procs rcving at same time */
/* typically set false for small internal messages, esp. when ids may not */
/*     yet be available */
/* set true for user msgs which may be quite large */
{
    struct p4_msg *dmsg;
    int n, sent = 0;
    int block_counter = 0;
    int size_left = size;
    char *buf = (char *)in_buf;
    COLLECT_STAT(int n_loop = 0);

    COLLECT_STAT(n_send_w_calls++);
    p4_dprintfl( 99, "Starting net_send_w of %d on fd %d\n", size, fd );
    while (size_left)
    {
	SYSCALL_P4(n, write(fd, buf + sent, size_left));
	if (n < 0)
	{
	    /* See net_read; these are often the same and EAGAIN is POSIX */
	    /* Solaris sometimes sets errno to 0 even though n is -1 (i.e.,
	       a bug in Solaris); we treat this as EAGAIN */
	    if (errno == EAGAIN || errno == EWOULDBLOCK || errno == 0)
	    {
		block_counter++;
		COLLECT_STAT(n_send_eagain++);
		/* Someone may be writing to us.  This waits until
		   either we can write or someone sends to use.
		   returns -1 if the write_fd is ready. */
		if (p4_sockets_ready( fd, 1 ) != -1)
		{
		    if (flag) {
			/* Only try to receive if the flag is set */
			dmsg = socket_recv( P4_FALSE );
			/* close of a connection may return a null msg */
			if (dmsg) 
			    queue_p4_message(dmsg, 
					     p4_local->queued_messages);
		    }
		}
		continue;
	    }
	    else
	    {
		if (p4_local->in_wait_for_exit) {
		    /* Exit the while if we can't send a close message */
		    break;
		}
		p4_dprintf("net_send: could not write to fd=%d, errno = %d\n",
			   fd, errno);
		p4_error("net_send write", n);
		break;    /* Allow p4_error() to return in case of a 
			     recursive call to p4_error */
	    }
	}
	COLLECT_STAT(if (n >n_send_max) n_send_max = n);
	sent      += n;
	size_left -= n;
	COLLECT_STAT(if (size_left > 0) { n_send_looped++ ; n_loop++; });
    }
    p4_dprintfl( 99, "Ending net_send of %d on fd %d (blocked %d times)\n", 
		 size, fd, block_counter );
    COLLECT_STAT(if (n_loop > n_send_loopcnt) n_send_loopcnt = n_loop);
    return (sent);
}

/* Send the header and the message together if possible */
int net_send2( int fd, void *header, int header_len, 
	       void *data, int len, int flag )
{
    int n;

#if defined(HAVE_WRITEV) && defined(HAVE_SYS_UIO_H)
    if (p4_use_writev) {
	struct iovec vbuf[2];
	vbuf[0].iov_base = header;
	vbuf[0].iov_len  = header_len;
	vbuf[1].iov_base = data;
	vbuf[1].iov_len  = len;
	n = writev( fd, vbuf, 2 );
	if (n == -1) {
	    /* Solaris sometimes sets errno to 0 even though n is -1 (i.e.,
	       a bug in Solaris); we treat this as EAGAIN */
	    if (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR ||
		errno == 0) {
		/* Just pretend nothing was written */
		n = 0;
	    }
	    else {
		/* Error on writing */
		/* We'll let the net_send code handle it */
		n = 0;
	    }
	}
	if (n < header_len + len) {
	    char *hptr = (char *)header;
	    if (n < header_len) {
		net_send( fd, hptr + n, header_len - n, flag );
		net_send( fd, data, len, flag );
	    }
	    else {
		char *dptr = (char *)data;
		int len_sent = n - header_len;
		net_send( fd, dptr + len_sent, len - len_sent, flag );
	    }
	}
	COLLECT_STAT(else {n_writev_first++;});
	/* Return only the length of the data sent */
	n = len;
    }
    else 
#endif
    {
	(void)net_send(fd, header, header_len, flag);
	p4_dprintfl(20, "sent hdr on fd %d via socket\n",fd);

	n = net_send(fd, data, len, flag);
    }
    return n;
}

void p4_socket_stat( FILE *fp )
{
    if (p4_output_socket_stat) {

	fprintf( fp, 
	     "send calls = %d eagain = %d maxbytes = %d loop %d maxloop %d\n",
	     n_send_w_calls, n_send_eagain, n_send_max, n_send_looped, 
	     n_send_loopcnt );
	fprintf( fp,
		 "send w writev %d\n", n_writev_first );
	fprintf( fp, 
	     "recv calls = %d eagain %d maxbytes %d select %d maxloop %d\n",
	     n_recv_calls, n_recv_eagain, n_recv_max, n_recv_select, 
	     n_recv_maxloop  );
	fflush( fp );
    }
}
/* This can FAIL if the host name is invalid.  For that reason, there is
   a timeout in the test, with a failure return if the entry cannot be found 

   Note also that the name returned may or may not be the canonnical, 
   "well known" name for the host, depending on the implementation of Unix.  
   This may not be the same as the input name, particularly 
   if the system has several networks.

   Finally, this can hang on systems that don't have a working name resolution
   service (this is not uncommon on LINUX clusters).  There is currently
   no fix for this (we need something like the timeout code in other 
   parts of the P4 implementation).

   We have added rudimentary timing to this routine to keep track of the
   amount of time that is spent in this routine.

   Another option, not implemented, is to maintain a local cache of
   names.  This would prevent us from making multiple queries about the same
   name.  However, since occurs most often when testing rather than using the
   p4 system, we have not implemented this idea.
 */
#include <sys/time.h>
static time_t time_in_gethostbyname = 0;
static int    n_gethostbyname = 0;
void p4_timein_hostbyname( int *t, int *count )
{
    *t     = (int)time_in_gethostbyname;
    *count = n_gethostbyname;
}
#ifndef TIMEOUT_VALUE 
#define TIMEOUT_VALUE 60
#endif
struct hostent *gethostbyname_p4(char *hostname)
{
    struct hostent *hp;
#ifdef SCYLD_BEOWULF
    struct sockaddr_in s_in;    
    long nodenum;
    int size;
    
    p4_dprintfl(10,"Beowulf: using beowulf version of gethostbyname_p4\n");
    
    nodenum=strtol(hostname,NULL,10);
    
    size=sizeof(struct sockaddr_in);
    bproc_nodeaddr(nodenum,(struct sockaddr *)&s_in,&size);
    
    hp=(struct hostent *)calloc(1,sizeof(struct hostent));
    hp->h_name=strdup(hostname);
    hp->h_aliases=NULL;
    hp->h_addrtype=AF_INET;
    hp->h_length=4;
    hp->h_addr_list=(char **)calloc(2,sizeof(char *));
    hp->h_addr_list[0]=calloc(1,4);
    memcpy(hp->h_addr_list[0],(char *)&(s_in.sin_addr.s_addr),4);
    hp->h_addr_list[1]=NULL;
#else
    int i = 100;
    time_t start_time, cur_time;

    start_time = time( (time_t) 0 );

    while ((hp = gethostbyname(hostname)) == NULL)
    {
	if (!--i)
	{
	    i = 100;
	    p4_dprintfl(10,"gethostbyname failed 100 times for host %s\n",
			hostname);
	    cur_time = time( (time_t) 0 );
	    if (cur_time - start_time > TIMEOUT_VALUE) {
		/* Dump out current procgroup */
		char msgbuf[512];
		if (p4_local && p4_local->procgroup) 
		    dump_procgroup(p4_local->procgroup,00);
		sprintf( msgbuf, 
"Could not gethostbyname for host %s; may be invalid name\n", hostname );
		p4_error(msgbuf, cur_time - start_time);
		return 0;
	    }
	}
    }
    time_in_gethostbyname += (time( (time_t) 0 ) - start_time );
    n_gethostbyname ++;
#endif /* SCYLD_BEOWULF */
    return(hp);
}

/* General replacement for gethostname for Solaris and Scyld */
int gethostname_p4(char *name,size_t len)
{
#   if defined(SUN_SOLARIS) || defined(MEIKO_CS2)
	return sysinfo(SI_HOSTNAME, name, len );
#   elif defined (SCYLD_BEOWULF)
	return -(snprintf(name, len , "%d", bproc_currnode()) == -1);
#   else
	return gethostname(name,len);
#   endif
}

P4VOID get_inet_addr(struct in_addr *addr)
{
    char hostname[100];
    struct hostent *hp;

    hostname[0] = '\0';
    get_qualified_hostname(hostname,sizeof(hostname));
    hp = gethostbyname_p4(hostname);
    bcopy(hp->h_addr, addr, hp->h_length);
}

P4VOID get_inet_addr_str( char *str )
{
    struct in_addr addr;

    get_inet_addr(&addr);
    strcpy(str, (char *) inet_ntoa(addr));
}

/* 
   This routine prints information on a socket, including many of the options
 */
void p4_print_sock_params( int skt )
{
#ifdef CAN_DO_SETSOCKOPT
    int rc, ival;
    p4_sockopt_len_t ivallen;
#ifdef SO_KEEPALIVE
    ivallen = sizeof(ival);
    rc = getsockopt( skt, SOL_SOCKET, SO_KEEPALIVE, (char *)&ival, &ivallen );
    if (!rc)
	printf( "Socket %d SO_KEEPALIVE = %d\n", skt, ival );
#endif
#ifdef SO_OOBINLINE
    ivallen = sizeof(ival);
    rc = getsockopt( skt, SOL_SOCKET, SO_OOBINLINE, (char *)&ival, &ivallen );
    if (!rc)
	printf( "Socket %d SO_OOBINLINE = %d\n", skt, ival );
#endif
#ifdef SO_SNDBUF
    ivallen = sizeof(ival);
    rc = getsockopt( skt, SOL_SOCKET, SO_SNDBUF, (char *)&ival, &ivallen );
    if (!rc)
	printf( "Socket %d SO_SNDBUF = %d\n", skt, ival );
#endif
#ifdef SO_RCVBUF
    ivallen = sizeof(ival);
    rc = getsockopt( skt, SOL_SOCKET, SO_RCVBUF, (char *)&ival, &ivallen );
    if (!rc)
	printf( "Socket %d SO_RCVBUF = %d\n", skt, ival );
#endif
#ifdef SO_SNDTIMEO
    ivallen = sizeof(ival);
    rc = getsockopt( skt, SOL_SOCKET, SO_SNDTIMEO, (char *)&ival, &ivallen );
    if (!rc)
	printf( "Socket %d SO_SNDTIMEO = %d\n", skt, ival );
#endif
#ifdef SO_RCVTIMEO
    ivallen = sizeof(ival);
    rc = getsockopt( skt, SOL_SOCKET, SO_RCVTIMEO, (char *)&ival, &ivallen );
    if (!rc)
	printf( "Socket %d SO_RCVTIMEO = %d\n", skt, ival );
#endif
#ifdef SO_SNDLOWAT
    ivallen = sizeof(ival);
    rc = getsockopt( skt, SOL_SOCKET, SO_SNDLOWAT, (char *)&ival, &ivallen );
    if (!rc)
	printf( "Socket %d SO_SNDLOWAT = %d\n", skt, ival );
#endif
#ifdef SO_RCVLOWAT
    ivallen = sizeof(ival);
    rc = getsockopt( skt, SOL_SOCKET, SO_RCVLOWAT, (char *)&ival, &ivallen );
    if (!rc)
	printf( "Socket %d SO_RCVLOWAT = %d\n", skt, ival );
#endif
#endif
}

#if !defined(CRAY)
/* cray complains about addr being addr of bit field */
/* can probably get around this problem if ever necessary */

P4VOID dump_sockaddr(char *who, struct sockaddr_in *sa)
{
    unsigned char *addr;

    addr = (unsigned char *) &(sa->sin_addr.s_addr);

    p4_dprintfl(90,"%s: family=%d port=%d addr=%d.%d.%d.%d\n",
		who,
                sa->sin_family,
                ntohs(sa->sin_port),
                addr[0], addr[1], addr[2], addr[3]);
}

P4VOID dump_sockinfo( char *msg, int fd)
{
    p4_sockopt_len_t nl;
    struct sockaddr_in peer, me;

    p4_dprintfl(00, "Dumping sockinfo for fd=%d: %s\n", fd, msg);

    nl = sizeof(me);
    getsockname(fd, (struct sockaddr *) &me, &nl);
    dump_sockaddr("Me", &me);
	   
    nl = sizeof(peer);
    getpeername(fd, (struct sockaddr *) &peer, &nl);
    dump_sockaddr("Peer",&peer);
}

#endif

/*
 * mpiexec_reopen_stdin is used by the OSC mpiexec interface.  This 
 * reinitializes stdin to a selected host and port.
 */

/*
 * Search the environment for variables which might say that mpiexec
 * requested stdin be grabbed from the spawning process.  Only happens
 * in the case of "-allstdin", i.e., where the user requested that the
 * same input be replicated into each process.
 */
void mpiexec_reopen_stdin(void)
{
    char *host = getenv("MPIEXEC_STDIN_HOST");
    char *sport = getenv("MPIEXEC_STDIN_PORT");
    struct sockaddr_in s_in;
    char *cq;
    int fd, port, tries;
    struct hostent *hp;

    if (!sport || !host)
	return;
    hp = gethostbyname_p4(host);
    if (!hp)
	p4_error("mpiexec_reopen_stdin: MPIEXEC_STDIN_HOST did not parse", 0);
    port = strtol(sport, &cq, 10);
    if (*cq)
	p4_error("mpiexec_reopen_stdin: MPIEXEC_STDIN_PORT did not parse", 0);
    fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0)
	p4_error("mpiexec_reopen_stdin: socket", fd);
    memset(&s_in, 0, sizeof(s_in));
    s_in.sin_family = AF_INET;
    s_in.sin_port = htons(port);
    memcpy(&s_in.sin_addr, hp->h_addr_list[0], hp->h_length);

    /*
     * Probably not necessary in the general case, but a swamped mpiexec
     * stdio process with a short listening backlog might require this.
     */
    tries = 0;
    for (;;) {
	int cc;

	cc = connect(fd, (struct sockaddr *)&s_in, sizeof(s_in));
	if (cc == 0)
	    break;
	if ((errno == ECONNREFUSED || errno == EINTR || errno == EAGAIN)
	  && tries < 5) {
	    ++tries;
	    sleep(1);
	    continue;
	}
	p4_error("mpiexec_reopen_stdin: connect", cc);
    }
    close(0);
    dup2(fd, 0);
    close(fd);
}

int p4_make_socket_nonblocking( int fd )
{
    /* Set the socket to be nonblocking.  */
    int rc, flags = fcntl( fd, F_GETFL, 0 );
    flags |= O_NONBLOCK;
    rc = fcntl( fd, F_SETFL, flags );
    return rc;
}
