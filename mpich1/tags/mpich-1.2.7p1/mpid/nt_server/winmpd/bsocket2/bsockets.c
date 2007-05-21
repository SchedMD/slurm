/* -*- Mode: C; c-basic-offset:4 ; -*- */
/*
 *  (C) 2001 by Argonne National Laboratory.
 *      See COPYRIGHT in top-level directory.
 */
#include "bsocket.h"
#ifdef HAVE_UNISTD_H
#include <unistd.h>
#endif
#ifdef HAVE_SYS_FILIO_H
#include <sys/filio.h>
#elif defined(HAVE_SYS_IOCTL_H)
#include <sys/ioctl.h>
#endif
#include <stdio.h>
#ifdef HAVE_SYS_TIME_H
#include <sys/time.h> 
#endif
#include <errno.h> 
#ifdef HAVE_SYS_PARAM_H
#include <sys/param.h>
#endif
#ifdef HAVE_STDLIB_H
#include <stdlib.h>
#endif
#ifdef HAVE_SYS_SOCKET_H
#include <sys/socket.h>
#endif
/* FIONBIO (solaris sys/filio.h) */
#ifdef HAVE_SYS_IOCTL_H
#include <sys/ioctl.h> 
#endif
/* TCP_NODELAY */
#ifdef HAVE_NETINET_TCP_H
#include <netinet/tcp.h> 
#endif
/* defs of gethostbyname */
#ifdef HAVE_NETDB_H
#include <netdb.h> 
#endif
#ifdef HAVE_SYS_SELECT_H
#include <sys/select.h>
#endif
#ifdef HAVE_WINSOCK2_H
#include <time.h>
#endif

/*#define DEBUG_BSOCKET*/
#undef DEBUG_BSOCKET

#ifdef DEBUG_BSOCKET
#define DBG_MSG(paramlist) printf( paramlist )
#else
#define DBG_MSG(paramlist) 
#endif

#define BSOCKET_MIN(a, b) ((a) < (b) ? (a) : (b))
#define BSOCKET_MAX(a, b) ((a) > (b) ? (a) : (b))

static int g_beasy_connection_attempts = 15;

#ifdef HAVE_WINSOCK2_H
static void log_warning(char *str, ...)
{
    char    szMsg[256] = "bsocket error";
    HANDLE  hEventSource;
    char   *lpszStrings[2];
    char pszStr[4096];
    va_list list;

    // Write to a temporary string
    va_start(list, str);
    vsprintf(pszStr, str, list);
    va_end(list);
    
    hEventSource = RegisterEventSource(NULL, "bsocket");
    
    lpszStrings[0] = szMsg;
    lpszStrings[1] = pszStr;
    
    if (hEventSource != NULL) 
    {
	ReportEvent(hEventSource, /* handle of event source */
	    EVENTLOG_WARNING_TYPE,  /* event type */
	    0,                    /* event category */
	    0,                    /* event ID */
	    NULL,                 /* current user's SID */
	    2,                    /* strings in lpszStrings */
	    0,                    /* no bytes of raw data */
	    (LPCTSTR*)lpszStrings,/* array of error strings */
	    NULL);                /* no raw data */
	
	DeregisterEventSource(hEventSource);
    }
}
#else
#define log_warning()
#endif

/*@
   bget_fd - 

   Parameters:
+  int bfd

   Notes:
@*/
unsigned int bget_fd(int bfd)
{
    return bfd;
}

void bclr(int bfd, bfd_set *s)
{
    FD_CLR( (unsigned int)bfd, s );
}

void bset(int bfd, bfd_set *s)
{
    FD_SET( (unsigned int)bfd, s );
}

/*@
bsocket_init - 

  
    Notes:
@*/
static int g_nInitRefCount = 0;
int bsocket_init(void)
{
    char *szNum;
#ifdef HAVE_WINSOCK2_H
    WSADATA wsaData;
    int err;
    
    if (g_nInitRefCount)
    {
	g_nInitRefCount++;
	return 0;
    }

    /* Start the Winsock dll */
    if ((err = WSAStartup(MAKEWORD(2, 0), &wsaData)) != 0)
    {
	printf("Winsock2 dll not initialized, error %d\n", err);
	return err;
    }
#else
    if (g_bInitFinalize == 1)
	return 0;
#endif

    szNum = getenv("BSOCKET_CONN_TRIES");
    if (szNum != NULL)
	g_beasy_connection_attempts = atoi(szNum);

    g_nInitRefCount++;

    return 0;
}

/*@
bsocket_finalize - 

  
    Notes:
@*/
int bsocket_finalize(void)
{
    g_nInitRefCount--;
    if (g_nInitRefCount < 1)
	g_nInitRefCount = 0;
    else
	return 0;

#ifdef HAVE_WINSOCK2_H
    WSACleanup();
#endif

    return 0;
}

/*@
bsocket - 

  Parameters:
  +   int family
  .  int type
  -  int protocol
  
    Notes:
@*/
int bsocket(int family, int type, int protocol)
{
    int bfd, bfdtemp;
    bfdtemp = socket(family, type, protocol);
    DuplicateHandle(GetCurrentProcess(), (HANDLE)bfdtemp, GetCurrentProcess(), &(HANDLE)bfd, 0, FALSE, DUPLICATE_CLOSE_SOURCE | DUPLICATE_SAME_ACCESS);
    return bfd;
}

/*@
bbind - bind

  Parameters:
  +  int bfd - bsocket
  .  const struct sockaddr *servaddr - address
  -  socklen_t servaddr_len - address length
  
    Notes:
@*/
int bbind(int bfd, const struct sockaddr *servaddr,	      
	  socklen_t servaddr_len)
{
    return bind(bfd, servaddr, servaddr_len);
}

/*@
blisten - listen

  Parameters:
  +  int bfd - bsocket
  -  int backlog - backlog
  
    Notes:
@*/
int blisten(int bfd, int backlog)
{
    return listen(bfd, backlog);
}

/*@
bsetsockopt - setsockopt

  Parameters:
  +  int bfd - bsocket
  .  int level - level
  .  int optname - optname
  .  const void *optval - optval
  -  socklen_t optlen - optlen
  
    Notes:
@*/
int bsetsockopt(int bfd, int level, int optname, const void *optval,		    
		socklen_t optlen)
{
    return setsockopt(bfd, level, optname, optval, optlen);
}

/*@
baccept - accept

  Parameters:
  +  int bfd - bsocket
  .  struct sockaddr *cliaddr - client address
  -  socklen_t *clilen - address length
  
    Notes:
@*/
int baccept(int bfd, struct sockaddr *cliaddr, socklen_t *clilen)
{
    int acceptedbfd, bfdtemp;
    bfdtemp = accept(bfd, cliaddr, clilen);
    DuplicateHandle(GetCurrentProcess(), (HANDLE)bfdtemp, GetCurrentProcess(), &(HANDLE)acceptedbfd, 0, FALSE, DUPLICATE_CLOSE_SOURCE | DUPLICATE_SAME_ACCESS);
    return acceptedbfd;
}

/*@
bconnect - connect

  Parameters:
  +  int bfd - bsocket
  .  const struct sockaddr *servaddr - address
  -  socklen_t servaddr_len - address length
  
    Notes:
@*/
int bconnect(int bfd, const struct sockaddr *servaddr,		    
	     socklen_t servaddr_len)
{
    return connect(bfd, servaddr, servaddr_len);
}

/*@
bselect - select

  Parameters:
  +  int maxfds - max bfd - 1 You must use BFD_MAX to get this value
  .  bfd_set *readbfds - read set
  .  bfd_set *writebfds - write set
  .  bfd_set *execbfds - exec set
  -  struct timeval *tv - timeout
  
    Notes:
@*/
int bselect(int maxfds, bfd_set *readbfds, bfd_set *writebfds,		   
	    bfd_set *execbfds, struct timeval *tv)
{
    return select(maxfds, readbfds, writebfds, execbfds, tv);
}

/*@
bwrite - write

  Parameters:
  +  int bfd - bsocket
  .  char *ubuf - buffer
  -  int len - length
  
    Notes:
@*/
int bwrite(int bfd, char *ubuf, int len)
{
    return write(bfd, ubuf, len);
}

/*
#define DBG_BWRITEV
#define DBG_BWRITEV_PRINT(a) printf a
*/
#undef DBG_BWRITEV
#define DBG_BWRITEV_PRINT

/*@
   bwritev - writev

   Parameters:
+  int bfd - bsocket
.  B_VECTOR *pIOVec - iovec structure
-  int n - length of iovec

   Notes:
@*/
int bwritev(int bfd, B_VECTOR *pIOVec, int n)
{
#ifdef HAVE_WINSOCK2_H
#ifdef DBG_BWRITEV
    int i;
#endif
    DWORD dwNumSent = 0;
    if (n == 0)
	return 0;
#ifdef DBG_BWRITEV
    printf("(bwritev");
    for (i=0; i<n; i++)
	printf(":%d", pIOVec[i].B_VECTOR_LEN);
#endif
    if (WSASend(bfd, pIOVec, n, &dwNumSent, 0, NULL/*overlapped*/, NULL/*completion routine*/) == SOCKET_ERROR)
    {
	if (WSAGetLastError() != WSAEWOULDBLOCK)
	{
	    return SOCKET_ERROR;
	}
    }
    DBG_BWRITEV_PRINT(("->%d)", dwNumSent));
    return dwNumSent;
#else
    return writev(bfd, pIOVec, n);
#endif
}

/*@
bread - read

  Parameters:
  +  int bfd - bsocket
  .  char *ubuf - buffer
  -  int len - length
  
    Notes:
@*/
int bread(int bfd, char *ubuf, int len)
{
    return read(bfd, ubuf, len);
}

/*
#define DBG_BREADV
#define DBG_BREADV_PRINT(a) printf a
*/
#undef DBG_BREADV
#define DBG_BREADV_PRINT(a) 

/*@
   breadv - readv

   Parameters:
+  int bfd - bsocket
.  B_VECTOR *uvec - iovec array
-  int len - length of array

   Notes:
   The vec parameter must have one more element than veclen.  This extra
   element is used by this function to read additional data into an internal
   buffer.
   The elements of the vec parameter may be changed by this function.
@*/
int breadv(int bfd, B_VECTOR *vec, int veclen)
{
    int k;
#ifdef HAVE_WINSOCK2_H
    DWORD    n = 0;
    DWORD    nFlags = 0;
#else
    int      n = 0;
#endif

    DBG_MSG("Enter bread\n");
    
#ifdef HAVE_WINSOCK2_H
    if (WSARecv(bfd, vec, veclen, &n, &nFlags, NULL/*overlapped*/, NULL/*completion routine*/) == SOCKET_ERROR)
    {
	if (WSAGetLastError() != WSAEWOULDBLOCK)
	{
	    for (k=0; k<veclen; k++)
		printf("vec[%d] len: %d\nvec[%d] buf: 0x%x\n", k, vec[k].B_VECTOR_LEN, k, vec[k].B_VECTOR_BUF);
	    n = 0; /* Set this to zero so it can be added to num_read */
	}
    }
#else
    n = readv(bfd, vec, veclen);
#endif
    
    return n;
}

/*@
   bclose - close

   Parameters:
.  int bfd - bsocket

   Notes:
@*/
int bclose(int bfd)
{
    DBG_MSG("Enter bclose\n");
    return close(bfd);
}

/*@
bgetsockname - 

  Parameters:
  +  int bfd
  .  struct sockaddr *name
  -  int *namelen
  
    Notes:
@*/
int bgetsockname(int bfd, struct sockaddr *name, int *namelen)
{
    return getsockname(bfd, name, namelen);
}

/*@
make_nonblocking - make a bsocket non-blocking

  Parameters:
  . int bfd - bsocket
  
    Notes:
@*/
int bmake_nonblocking(int bfd)
{
    
    int      flag = 1;
    int      rc;
    
    DBG_MSG("Enter make_nonblocking\n");
    
#ifdef HAVE_WINDOWS_SOCKET
    rc = ioctlsocket(bfd, FIONBIO, &flag);
#else
    rc = ioctl(bfd, FIONBIO, &flag);
#endif
    
    return rc;
}

/*@
make_blocking - make a bsocket blocking

  Parameters:
  . int bfd - bsocket
  
    Notes:
@*/
int bmake_blocking(int bfd)
{
    int      flag = 0;
    int      rc;
    
    DBG_MSG("Enter make_blocking\n");
    
#ifdef HAVE_WINDOWS_SOCKET
    rc = ioctlsocket(bfd, FIONBIO, &flag);
#else
    rc = ioctl(bfd, FIONBIO, &flag);
#endif
    
    return rc;
}

/*@
   beasy_create - create a bsocket

   Parameters:
+  int *bfd - bsocket
.  int port - port
-  unsigned long addr - address

   Notes:
@*/
int beasy_create(int *bfd, int port, unsigned long addr)
{
    struct sockaddr_in sin;
    int optval = 1;
#ifdef USE_LINGER_SOCKOPT
    struct linger linger;
#endif

    /* Create a new bsocket */
    *bfd = bsocket(AF_INET, SOCK_STREAM, 0);
    if (*bfd == BFD_INVALID_SOCKET)
    {
	return SOCKET_ERROR;
    }
    
    memset(&sin, 0, sizeof(struct sockaddr_in));
    sin.sin_family = AF_INET;
    sin.sin_addr.s_addr = addr;
    sin.sin_port = htons((u_short)port);

    /* bind it to the port provided */
    if (bbind(*bfd, (const struct sockaddr *)&sin, sizeof(struct sockaddr)) == SOCKET_ERROR)
    {
	return SOCKET_ERROR;
    }

    /* Set the no-delay option */
    bsetsockopt(*bfd, IPPROTO_TCP, TCP_NODELAY, (char *)&optval, sizeof(optval));

#ifdef USE_LINGER_SOCKOPT
    /* Set the linger on close option */
    linger.l_onoff = 1 ;
    linger.l_linger = 60;
    bsetsockopt(*bfd, SOL_SOCKET, SO_LINGER, (char*)&linger, sizeof(linger));
#endif

    return 0;
}

/*@
   beasy_connect - connect

   Parameters:
+  int bfd - bsocket
.  char *host - hostname
-  int port - port

   Notes:
@*/
int beasy_connect(int bfd, char *host, int port)
{
#ifdef HAVE_WINSOCK2_H
    BOOL b;
#endif
    int error;
    int reps = 0;
    struct hostent *lphost;
    struct sockaddr_in sockAddr;
#ifdef USE_LINGER_SOCKOPT
    struct linger linger;
#endif
#ifdef HAVE_WINSOCK2_H
    /* use this array to make sure the warning only gets logged once */
    BOOL bWarningLogged[4] = { FALSE, FALSE, FALSE, FALSE };
#endif
    memset(&sockAddr,0,sizeof(sockAddr));
    
    sockAddr.sin_family = AF_INET;
    sockAddr.sin_addr.s_addr = inet_addr(host);
    
    if (sockAddr.sin_addr.s_addr == INADDR_NONE || sockAddr.sin_addr.s_addr == 0)
    {
	lphost = gethostbyname(host);
	if (lphost != NULL)
	    sockAddr.sin_addr.s_addr = ((struct in_addr *)lphost->h_addr)->s_addr;
	else
	    return SOCKET_ERROR;
    }
    
    sockAddr.sin_port = htons((u_short)port);
    
    while (bconnect(bfd, (SOCKADDR*)&sockAddr, sizeof(sockAddr)) == SOCKET_ERROR)
    {
#ifdef HAVE_WINSOCK2_H
	error = WSAGetLastError();
	srand(clock());
	if( (error == WSAECONNREFUSED || error == WSAETIMEDOUT || error == WSAENETUNREACH || error == WSAEADDRINUSE)
	    && (reps < g_beasy_connection_attempts) )
	{
	    double d = (double)rand() / (double)RAND_MAX;
	    Sleep(200 + (int)(d*200));
	    reps++;
	    switch (error)
	    {
	    case WSAECONNREFUSED:
		if (!bWarningLogged[0])
		{
		    /*log_warning("WSAECONNREFUSED error, re-attempting bconnect(%s)", host);*/
		    bWarningLogged[0] = TRUE;
		}
		break;
	    case WSAETIMEDOUT:
		if (!bWarningLogged[1])
		{
		    log_warning("WSAETIMEDOUT error, re-attempting bconnect(%s)", host);
		    bWarningLogged[1] = TRUE;
		}
		break;
	    case WSAENETUNREACH:
		if (!bWarningLogged[2])
		{
		    log_warning("WSAENETUNREACH error, re-attempting bconnect(%s)", host);
		    bWarningLogged[2] = TRUE;
		}
		break;
	    case WSAEADDRINUSE:
		if (!bWarningLogged[3])
		{
		    log_warning("WSAEADDRINUSE error, re-attempting bconnect(%s)", host);
		    bWarningLogged[3] = TRUE;
		}
		break;
	    default:
		log_warning("%d error, re-attempting bconnect");
		break;
	    }
	}
	else
	{
	    return SOCKET_ERROR;
	}
#else
	if( (errno == ECONNREFUSED || errno == ETIMEDOUT || errno == ENETUNREACH)
	    && (reps < g_beasy_connection_attempts) )
	{
#ifdef HAVE_USLEEP
	    usleep(200);
#else
	    sleep(0);
#endif
	    reps++;
	}
	else
	{
	    return SOCKET_ERROR;
	}
#endif
    }

#ifdef USE_LINGER_SOCKOPT
    /* Set the linger on close option */
    linger.l_onoff = 1 ;
    linger.l_linger = 60;
    bsetsockopt(bfd, SOL_SOCKET, SO_LINGER, (char*)&linger, sizeof(linger));
#endif

#ifdef HAVE_WINSOCK2_H
    b = TRUE;
    bsetsockopt(bfd, IPPROTO_TCP, TCP_NODELAY, (char*)&b, sizeof(BOOL));
#endif
    return 0;
}

/*@
   beasy_connect_quick - connect without retries

   Parameters:
+  int bfd - bsocket
.  char *host - hostname
-  int port - port

   Notes:
@*/
int beasy_connect_quick(int bfd, char *host, int port)
{
    struct hostent *lphost;
    struct sockaddr_in sockAddr;
#ifdef USE_LINGER_SOCKOPT
    struct linger linger;
#endif
    memset(&sockAddr,0,sizeof(sockAddr));
    
    sockAddr.sin_family = AF_INET;
    sockAddr.sin_addr.s_addr = inet_addr(host);
    
    if (sockAddr.sin_addr.s_addr == INADDR_NONE || sockAddr.sin_addr.s_addr == 0)
    {
	lphost = gethostbyname(host);
	if (lphost != NULL)
	    sockAddr.sin_addr.s_addr = ((struct in_addr *)lphost->h_addr)->s_addr;
	else
	    return SOCKET_ERROR;
    }
    
    sockAddr.sin_port = htons((u_short)port);
    
    if (bconnect(bfd, (SOCKADDR*)&sockAddr, sizeof(sockAddr)) == SOCKET_ERROR)
    {
	return SOCKET_ERROR;
    }

#ifdef USE_LINGER_SOCKOPT
    /* Set the linger on close option */
    linger.l_onoff = 1 ;
    linger.l_linger = 60;
    bsetsockopt(bfd, SOL_SOCKET, SO_LINGER, (char*)&linger, sizeof(linger));
#endif

    return 0;
}

/*@
   beasy_connect - connect

   Parameters:
+  int bfd - bsocket
.  char *host - hostname
.  int port - port
-  int seconds - timeout value in seconds

   Notes:
@*/
int beasy_connect_timeout(int bfd, char *host, int port, int seconds)
{
#ifdef HAVE_WINSOCK2_H
    BOOL b;
#endif
    clock_t start, current;
    int error;
    int reps = 0;
    struct hostent *lphost;
    struct sockaddr_in sockAddr;
#ifdef USE_LINGER_SOCKOPT
    struct linger linger;
#endif
#ifdef HAVE_WINSOCK2_H
    /* use this array to make sure the warning only gets logged once */
    BOOL bWarningLogged[4] = { FALSE, FALSE, FALSE, FALSE };
#endif

    start = clock();

    memset(&sockAddr,0,sizeof(sockAddr));
    
    sockAddr.sin_family = AF_INET;
    sockAddr.sin_addr.s_addr = inet_addr(host);
    
    if (sockAddr.sin_addr.s_addr == INADDR_NONE || sockAddr.sin_addr.s_addr == 0)
    {
	lphost = gethostbyname(host);
	if (lphost != NULL)
	    sockAddr.sin_addr.s_addr = ((struct in_addr *)lphost->h_addr)->s_addr;
	else
	    return SOCKET_ERROR;
    }
    
    sockAddr.sin_port = htons((u_short)port);
    
    while (bconnect(bfd, (SOCKADDR*)&sockAddr, sizeof(sockAddr)) == SOCKET_ERROR)
    {
	current = clock();
	if (((current - start) / CLOCKS_PER_SEC) > seconds)
	{
#ifdef HAVE_WINSOCK2_H
	    WSASetLastError(WSAETIMEDOUT);
#endif
	    return SOCKET_ERROR;
	}
#ifdef HAVE_WINSOCK2_H
	error = WSAGetLastError();
	srand(clock());
	if( (error == WSAECONNREFUSED || error == WSAETIMEDOUT || error == WSAENETUNREACH || error == WSAEADDRINUSE)
	    && (reps < g_beasy_connection_attempts) )
	{
	    double d = (double)rand() / (double)RAND_MAX;
	    Sleep(200 + (int)(d*200));
	    reps++;
	    switch (error)
	    {
	    case WSAECONNREFUSED:
		if (!bWarningLogged[0])
		{
		    /*log_warning("WSAECONNREFUSED error, re-attempting bconnect(%s)", host);*/
		    bWarningLogged[0] = TRUE;
		}
		break;
	    case WSAETIMEDOUT:
		if (!bWarningLogged[1])
		{
		    log_warning("WSAETIMEDOUT error, re-attempting bconnect(%s)", host);
		    bWarningLogged[1] = TRUE;
		}
		break;
	    case WSAENETUNREACH:
		if (!bWarningLogged[2])
		{
		    log_warning("WSAENETUNREACH error, re-attempting bconnect(%s)", host);
		    bWarningLogged[2] = TRUE;
		}
		break;
	    case WSAEADDRINUSE:
		if (!bWarningLogged[3])
		{
		    log_warning("WSAEADDRINUSE error, re-attempting bconnect(%s)", host);
		    bWarningLogged[3] = TRUE;
		}
		break;
	    default:
		log_warning("%d error, re-attempting bconnect");
		break;
	    }
	}
	else
	{
	    return SOCKET_ERROR;
	}
#else
	if( (errno == ECONNREFUSED || errno == ETIMEDOUT || errno == ENETUNREACH)
	    && (reps < g_beasy_connection_attempts) )
	{
#ifdef HAVE_USLEEP
	    usleep(200);
#else
	    sleep(0);
#endif
	    reps++;
	}
	else
	{
	    return SOCKET_ERROR;
	}
#endif
    }

#ifdef USE_LINGER_SOCKOPT
    /* Set the linger on close option */
    linger.l_onoff = 1 ;
    linger.l_linger = 60;
    bsetsockopt(bfd, SOL_SOCKET, SO_LINGER, (char*)&linger, sizeof(linger));
#endif

#ifdef HAVE_WINSOCK2_H
    b = TRUE;
    bsetsockopt(bfd, IPPROTO_TCP, TCP_NODELAY, (char*)&b, sizeof(BOOL));
#endif
    return 0;
}

/*@
   beasy_accept - accept

   Parameters:
.  int bfd - listening bsocket

   Notes:
@*/
int beasy_accept(int bfd)
{
#ifdef HAVE_WINSOCK2_H
    BOOL b;
#endif
#ifdef USE_LINGER_SOCKOPT
    struct linger linger;
#endif
    struct sockaddr addr;
    int len;
    int client;

    len = sizeof(addr);
    client = baccept(bfd, &addr, &len);

    if (client == BFD_INVALID_SOCKET)
    {
	return BFD_INVALID_SOCKET;
    }

#ifdef USE_LINGER_SOCKOPT
    linger.l_onoff = 1;
    linger.l_linger = 60;
    bsetsockopt(client, SOL_SOCKET, SO_LINGER, (char*)&linger, sizeof(linger));
#endif

#ifdef HAVE_WINSOCK2_H
    b = TRUE;
    bsetsockopt(client, IPPROTO_TCP, TCP_NODELAY, (char*)&b, sizeof(BOOL));
#endif
    return client;
}

/*@
   beasy_closesocket - closesocket

   Parameters:
+  int bfd - bsocket

   Notes:
@*/
int beasy_closesocket(int bfd)
{
    WSAEVENT hEvent = WSACreateEvent();
    if (hEvent != WSA_INVALID_EVENT)
    {
	if (WSAEventSelect(bget_fd(bfd), hEvent, FD_CLOSE) == 0)
	{
	    shutdown(bget_fd(bfd), SD_BOTH);
	    WaitForSingleObject(hEvent, 200);
	    /*
	    if (WaitForSingleObject(hEvent, 100) == WAIT_TIMEOUT)
	    {
		printf("wait for close timed out\n");fflush(stdout);
	    }
	    else
	    {
		printf("wait for close succeeded\n");fflush(stdout);
	    }
	    */
	    WSACloseEvent(hEvent);
	}
	else
	    shutdown(bget_fd(bfd), SD_BOTH);
    }
    else
	shutdown(bget_fd(bfd), SD_BOTH);
    bclose(bfd);
    return 0;
}

/*@
   beasy_get_sock_info - get bsocket information

   Parameters:
+  int bfd - bsocket
.  char *name - hostname
-  int *port - port

   Notes:
@*/
int beasy_get_sock_info(int bfd, char *name, int *port)
{
    struct sockaddr_in addr;
    int name_len = sizeof(addr);

    getsockname(bget_fd(bfd), (struct sockaddr*)&addr, &name_len);
    *port = ntohs(addr.sin_port);
    gethostname(name, 100);
    return 0;
}

/*@
   beasy_get_ip_string - get ip string a.b.c.d

   Parameters:
.  char *ipstring - string

   Notes:
@*/
int beasy_get_ip_string(char *ipstring)
{
    char hostname[100];
    unsigned int a, b, c, d;
    struct hostent *pH;

    gethostname(hostname, 100);
    pH = gethostbyname(hostname);
    if (pH == NULL)
	return SOCKET_ERROR;
    a = (unsigned char)(pH->h_addr_list[0][0]);
    b = (unsigned char)(pH->h_addr_list[0][1]);
    c = (unsigned char)(pH->h_addr_list[0][2]);
    d = (unsigned char)(pH->h_addr_list[0][3]);
    sprintf(ipstring, "%u.%u.%u.%u", a, b, c, d);
    return 0;
}

/*@
   beasy_get_ip - get ip address

   Parameters:
.  long *ip - ip address

   Notes:
@*/
int beasy_get_ip(unsigned long *ip)
{
    char hostname[100];
    struct hostent *pH;

    gethostname(hostname, 100);
    pH = gethostbyname(hostname);
    *ip = *((unsigned long *)(pH->h_addr_list));
    return 0;
}

/*@
   beasy_receive - receive

   Parameters:
+  int bfd - bsocket
.  char *buffer - buffer
-  int len - length

   Notes:
@*/
int beasy_receive(int bfd, char *buffer, int len)
{
    int ret_val;
    int num_received;
    bfd_set readfds;
    int total = len;
    
    num_received = bread(bfd, buffer, len);
    if (num_received == SOCKET_ERROR)
    {
	if ((errno != EINTR) || (errno != EAGAIN))
	    return SOCKET_ERROR;
    }
    else
    {
	len -= num_received;
	buffer += num_received;
    }
    
    while (len)
    {
	BFD_ZERO(&readfds); 
	BFD_SET((unsigned int)bfd, &readfds);
	
	ret_val = bselect(bfd+1, &readfds, NULL, NULL, NULL);
	if (ret_val == 1)
	{
	    num_received = bread(bfd, buffer, len);
	    if (num_received == SOCKET_ERROR)
	    {
		if ((errno != EINTR) || (errno != EAGAIN))
		    return SOCKET_ERROR;
	    }
	    else
	    {
		if (num_received == 0)
		{
		    //printf("beasy_receive: socket %d closed\n", bfd);
		    //bmake_blocking(bfd);
		    //printf("beasy_receive: socket read 0 bytes after bselect returned read signalled therefore the socket is closed.\n");fflush(stdout);
		    return 0;
		}
		len -= num_received;
		buffer += num_received;
	    }
	}
	else
	{
	    if (ret_val == SOCKET_ERROR)
	    {
		if ((errno != EINTR) || (errno != EAGAIN))
		    return SOCKET_ERROR;
	    }
	}
    }

    //bmake_blocking(bfd);
    return total;
}

/*@
   beasy_receive_some - receive

   Parameters:
+  int bfd - bsocket
.  char *buffer - buffer
-  int len - length

   Notes:
@*/
int beasy_receive_some(int bfd, char *buffer, int len)
{
    int ret_val;
    int num_received;
    bfd_set readfds;
    
    num_received = bread(bfd, buffer, len);
    if (num_received == SOCKET_ERROR)
    {
	if ((errno != EINTR) || (errno != EAGAIN))
	    return SOCKET_ERROR;
    }
    else
    {
	if (num_received > 0)
	    return num_received;
    }
    
    BFD_ZERO(&readfds); 
    BFD_SET(bfd, &readfds);
    
    ret_val = bselect(bfd+1, &readfds, NULL, NULL, NULL);
    if (ret_val == 1)
    {
	num_received = bread(bfd, buffer, len);
	if (num_received == SOCKET_ERROR)
	{
	    if ((errno != EINTR) || (errno != EAGAIN))
		return SOCKET_ERROR;
	}
	else
	{
	    if (num_received == 0)
	    {
		//printf("beasy_receive_some: socket %d closed\n", bfd);
		//bmake_blocking(bfd);
		return SOCKET_ERROR;
	    }
	    return num_received;
	}
    }

    return SOCKET_ERROR;
}

/*@
   beasy_receive_timeout - receive

   Parameters:
+  int bfd - bsocket
.  char *buffer - buffer
.  int len - length
-  int timeout - timeout

   Notes:
@*/
int beasy_receive_timeout(int bfd, char *buffer, int len, int timeout)
{
    int ret_val;
    int num_received;
    bfd_set readfds;
    struct timeval tv;
    int total = len;
    
    /*
    num_received = bread(bfd, buffer, len);
    if (num_received == SOCKET_ERROR)
    {
	return SOCKET_ERROR;
    }
    else
    {
	len -= num_received;
	buffer += num_received;
    }
    */
    
    while (len)
    {
	BFD_ZERO(&readfds); 
	BFD_SET((unsigned int)bfd, &readfds);
	tv.tv_sec = timeout;
	tv.tv_usec = 0;
	
	ret_val = bselect(bfd+1, &readfds, NULL, NULL, &tv);
	if (ret_val == 1)
	{
	    num_received = bread(bfd, buffer, len);
	    if (num_received == SOCKET_ERROR)
	    {
		if ((errno != EINTR) || (errno != EAGAIN))
		    return SOCKET_ERROR;
	    }
	    else
	    {
		if (num_received == 0)
		{
		    //printf("beasy_receive_timeout: socket %d closed\n", bfd);
		    //bmake_blocking(bfd);
		    return total - len;
		}
		len -= num_received;
		buffer += num_received;
	    }
	}
	else
	{
	    if (ret_val == SOCKET_ERROR)
	    {
		if ((errno != EINTR) || (errno != EAGAIN))
		    return SOCKET_ERROR;
	    }
	    else
	    {
		//bmake_blocking(bfd);
		return total - len;
	    }
	}
    }
    //bmake_blocking(bfd);
    return total;
}

/*@
   beasy_send - send

   Parameters:
+  int bfd - bsocket
.  char *buffer - buffer
-  int length - length

   Notes:
@*/
int beasy_send(int bfd, char *buffer, int length)
{
#ifdef HAVE_WINSOCK2_H
    int error;
    int num_sent;

    while ((num_sent = write(bfd, buffer, length)) == SOCKET_ERROR)
    {
	error = WSAGetLastError();
	if (error == WSAEWOULDBLOCK)
	{
            //Sleep(0);
	    continue;
	}
	if (error == WSAENOBUFS)
	{
	    // If there is no buffer space available then split the buffer in half and send each piece separately.
	    if (beasy_send(bfd, buffer, length/2) == SOCKET_ERROR)
		return SOCKET_ERROR;
	    if (beasy_send(bfd, buffer+(length/2), length - (length/2)) == SOCKET_ERROR)
		return SOCKET_ERROR;
	    return length;
	}
	WSASetLastError(error);
	return SOCKET_ERROR;
    }
    
    return length;
#else
    int ret_val;
    int num_written;
    bfd_set writefds;
    int total = length;
    
    num_written = write(bfd, buffer, length);
    if (num_written == SOCKET_ERROR)
    {
	if ((errno != EINTR) || (errno != EAGAIN))
	    return SOCKET_ERROR;
    }
    else
    {
	length -= num_written;
	buffer += num_written;
    }
    
    while (length)
    {
	BFD_ZERO(&writefds); 
	BFD_SET(bfd, &writefds);
	
	ret_val = bselect(1, NULL, &writefds, NULL, NULL);
	if (ret_val == 1)
	{
	    num_written = write(((BFD_Buffer*)bfd)->real_fd, buffer, length);
	    if (num_written == SOCKET_ERROR)
	    {
		if ((errno != EINTR) || (errno != EAGAIN))
		    return SOCKET_ERROR;
	    }
	    else
	    {
		if (num_written == 0)
		{
		    //printf("beasy_send: socket closed\n");
		    return total - length;
		}
		length -= num_written;
		buffer += num_written;
	    }
	}
	else
	{
	    if (ret_val == SOCKET_ERROR)
	    {
		if ((errno != EINTR) || (errno != EAGAIN))
		    return SOCKET_ERROR;
	    }
	}
    }
    return total;
#endif
}

int beasy_getlasterror()
{
#ifdef HAVE_WINSOCK2_H
    return WSAGetLastError();
#else
    return errno;
#endif
}

int beasy_error_to_string(int error, char *str, int length)
{
#ifdef HAVE_WINSOCK2_H
    HLOCAL str_local;
    int num_bytes;
    num_bytes = FormatMessage(
	FORMAT_MESSAGE_FROM_SYSTEM |
	FORMAT_MESSAGE_ALLOCATE_BUFFER,
	0,
	error,
	MAKELANGID( LANG_NEUTRAL, SUBLANG_DEFAULT ),
	(LPTSTR) &str_local,
	0,0);
    if (num_bytes < length)
	memcpy(str, str_local, num_bytes+1);
    else
    {
	/* sprintf(str, "error %d", error); */
	LocalFree(str);
	return num_bytes+1;
    }
    LocalFree(str);
    strtok(str, "\r\n"); /* remove any CR/LF characters from the output */
#else
    /*sprintf(str, "error %d", error);*/
    strncpy(str, strerror(error), length);
#endif
    return 0;
}
