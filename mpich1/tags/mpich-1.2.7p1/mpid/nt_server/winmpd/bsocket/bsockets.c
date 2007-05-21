/* -*- Mode: C; c-basic-offset:4 ; -*- */
/*
 *  (C) 2001 by Argonne National Laboratory.
 *      See COPYRIGHT in top-level directory.
 */
#include "bsocketimpl.h"
#ifdef HAVE_STDIO_H
#include <stdio.h>
#endif
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

#define BPRINTF printf

#ifdef DEBUG_BSOCKET
#define DBG_MSG(paramlist) BPRINTF( paramlist )
#else
#define DBG_MSG(paramlist) 
#endif

#define BSTRINGLEN 20

#if !defined(NO_BSOCKETS)

typedef enum { 
    BFD_FD_NOT_IN_USE, 
    BFD_ALLOCATING, 
    BFD_NEW_FD, 
    BFD_BOUND, 
    BFD_LISTENING, 
    BFD_ACCEPTED, 
    BFD_CONNECTED, 
    BFD_WRITING, 
    BFD_READING, 
    BFD_IDLE, 
    BFD_NOT_READY, 
    BFD_SOCKET_READY, 
    BFD_ERROR 
} BFD_State;

struct BFD_Buffer_struct {
    int        real_fd;        /* socket descriptor */
    int        read_flag;      /* set if reading */
    int        write_flag;     /* set if writing */
    int        curpos;         /* holds current position in bbuf */
    int        num_avail;      /* bytes in our buffered read buffer */
    BFD_State  state;          /* state of our socket */
    int        errval;         /* errno value */
    char       string[BSTRINGLEN];
    char       read_buf[1];    /* read buffer */
    /* do not add any members after the read_buf because they will be overwritten */
    /* the read_buf continues past the end of this structure by g_bbuflen bytes */
};

BlockAllocator Bsocket_mem;

#endif /* !defined(NO_BSOCKETS) */

#define BSOCKET_MIN(a, b) ((a) < (b) ? (a) : (b))
#define BSOCKET_MAX(a, b) ((a) > (b) ? (a) : (b))

static int g_beasy_connection_attempts = 5;

#ifdef HAVE_WINSOCK2_H
static void log_warning(char *str, ...)
{
    char    szMsg[256] = "bsocket error";
    HANDLE  hEventSource;
    char   *lpszStrings[2];
    char pszStr[4096];
    va_list list;

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

#ifdef NO_BSOCKETS

static int g_nInitRefCount = 0;
int bsocket_init(void)
{
    char *szNum;
#ifdef HAVE_WINSOCK2_H
    WSADATA wsaData;
    int err;
#endif

    if (g_nInitRefCount)
    {
	g_nInitRefCount++;
	return 0;
    }

#ifdef HAVE_WINSOCK2_H
    /* Start the Winsock dll */
    if ((err = WSAStartup(MAKEWORD(2, 0), &wsaData)) != 0)
    {
	printf("Winsock2 dll not initialized, error %d\n", err);
	return err;
    }
#endif

    szNum = getenv("BSOCKET_CONN_TRIES");
    if (szNum != NULL)
    {
	g_beasy_connection_attempts = atoi(szNum);
	if (g_beasy_connection_attempts < 1)
	    g_beasy_connection_attempts = 5;
    }

    g_nInitRefCount++;

    return 0;
}

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

char g_btempstr[BSTRINGLEN];
char *bto_string( int bfd )
{
    snprintf(g_btempstr, BSTRINGLEN, "%d", bfd);
    return g_btempstr;
}

void bprint_set(bfd_set *p)
{
    unsigned int i;
    if (p->fd_count < 1)
	return;
    for (i=0; i<p->fd_count; i++)
	printf("%d ", p->fd_array[i]);
    printf("\n");
    fflush(stdout);
}

int bwritev(int bfd, B_VECTOR *pIOVec, int n)
{
#ifdef HAVE_WINSOCK2_H
    DWORD dwNumSent = 0;
    if (n == 0)
	return 0;
    if (WSASend(bfd, pIOVec, n, &dwNumSent, 0, NULL/*overlapped*/, NULL/*completion routine*/) == SOCKET_ERROR)
    {
	if (WSAGetLastError() != WSAEWOULDBLOCK)
	{
	    return SOCKET_ERROR;
	}
    }
    return dwNumSent;
#else
    return writev(bfd, pIOVec, n);
#endif
}

int breadv(int bfd, B_VECTOR *vec, int veclen)
{
#ifdef HAVE_WINSOCK2_H
    /*int k;*/
    DWORD    n = 0;
    DWORD    nFlags = 0;
#else
    int      n = 0;
#endif

    DBG_MSG("Enter breadv\n");
    
#ifdef HAVE_WINSOCK2_H
    if (WSARecv(bfd, vec, veclen, &n, &nFlags, NULL/*overlapped*/, NULL/*completion routine*/) == SOCKET_ERROR)
    {
	if (WSAGetLastError() != WSAEWOULDBLOCK)
	{
	    /*
	    for (k=0; k<veclen; k++)
		msg_printf("vec[%d] len: %d\nvec[%d] buf: 0x%x\n", k, vec[k].B_VECTOR_LEN, k, vec[k].B_VECTOR_BUF);
	    */
	    return SOCKET_ERROR;
	}
	n = 0;
    }
#else
    n = readv(bfd, vec, veclen);
#endif
    
    return n;
}

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

#else /* #ifdef NO_BSOCKETS */

#define BBUF_LOWER_LIMIT 100
#define BBUF_DEFAULT_LEN 1024

static int g_bbuflen = BBUF_DEFAULT_LEN;
static int g_buf_pool_size = FD_SETSIZE;

/*@
   bget_fd - get fd

   Parameters:
+  int bfd - bfd

   Notes:
@*/
unsigned int bget_fd(int bfd)
{
    /*dbg_printf("bget_fd\n");*/
    return (unsigned int)(((BFD_Buffer*)bfd)->real_fd);
}

/*@
   bset - bset

   Parameters:
+  int bfd - bfd
-  bfd_set *s - set

   Notes:
@*/
void bset(int bfd, bfd_set *s)
{
    int i;
    /*dbg_printf("bset\n");*/
    FD_SET( bget_fd(bfd), & (s) -> set );
    for (i=0; i<s->n; i++)
    {
	if (s->p[i] == (BFD_Buffer*)bfd)
	    return;
    }
    s->p[s->n] = (BFD_Buffer*)bfd;
    s->n++;
}

/*@
   bclr - blcr

   Parameters:
+  int bfd - bfd
-  bfd_set *s - set

   Notes:
@*/
void bclr(int bfd, bfd_set *s)
{
    int i;
    BFD_Buffer* p;

    /*dbg_printf("bclr\n");*/

    FD_CLR( bget_fd(bfd), & (s) -> set );

    if (s->n == 0)
	return;

    p = (BFD_Buffer*)bfd;
    for (i=0; i<s->n; i++)
    {
	if (s->p[i] == p)
	{
	    s->p[i] = s->p[s->n-1];
	    s->n--;
	    return;
	}
    }
}

/*@
bsocket_init - init

  
    Notes:
@*/
static int g_nInitRefCount = 0;
int bsocket_init(void)
{
    char *pszEnvVar;
    char *szNum;
#ifdef HAVE_WINSOCK2_H
    WSADATA wsaData;
    int err;
#endif
    
    if (g_nInitRefCount)
    {
	g_nInitRefCount++;
	return 0;
    }

#ifdef HAVE_WINSOCK2_H
    /* Start the Winsock dll */
    if ((err = WSAStartup(MAKEWORD(2, 0), &wsaData)) != 0)
    {
	BPRINTF("Winsock2 dll not initialized, error %d\n", err);
	return err;
    }
#endif

    szNum = getenv("BSOCKET_CONN_TRIES");
    if (szNum != NULL)
    {
	g_beasy_connection_attempts = atoi(szNum);
	if (g_beasy_connection_attempts < 1)
	    g_beasy_connection_attempts = 5;
    }

    pszEnvVar = getenv("BSOCKET_BBUFLEN");
    if (pszEnvVar != NULL)
    {
	g_bbuflen = atoi(pszEnvVar);
	if (g_bbuflen < BBUF_LOWER_LIMIT)
	    g_bbuflen = BBUF_DEFAULT_LEN;
    }

    Bsocket_mem = BlockAllocInit(sizeof(BFD_Buffer) + g_bbuflen, 64, 64, malloc, free);

    g_nInitRefCount++;

    return 0;
}

/*@
bsocket_finalize - finalize

  
    Notes:
@*/
int bsocket_finalize(void)
{
    dbg_printf("bsocket_finalize\n");
    g_nInitRefCount--;
    if (g_nInitRefCount < 1)
	g_nInitRefCount = 0;
    else
	return 0;

    /* Free up the memory used by Bsocket_mem */
    BlockAllocFinalize(&Bsocket_mem);
    
#ifdef HAVE_WINSOCK2_H
    WSACleanup();
#endif

    return 0;
}

char *bto_string( int bfd )
{
    return ((BFD_Buffer*)bfd)->string;
}

void bprint_set(bfd_set *p)
{
    unsigned int i;
    if (p->set.fd_count < 1)
	return;
    for (i=0; i<p->set.fd_count; i++)
	printf("%d ", p->set.fd_array[i]);
    printf("\n");
    fflush(stdout);
}

/*@
bsocket - socket

  Parameters:
+  int family - family
.  int type - type
-  int protocol - protocol
  
    Notes:
@*/
int bsocket(int family, int type, int protocol)
{
#ifdef HAVE_WINSOCK2_H
    int bfdtemp;
#endif
    BFD_Buffer *pbfd;

    DBG_MSG("Enter bsocket\n");
    /*dbg_printf("bsocket\n");*/
    
    pbfd = (BFD_Buffer *)BlockAlloc( Bsocket_mem );
    if (pbfd == 0) 
    {
	DBG_MSG(("ERROR in bsocket: BlockAlloc returned NULL"));
	return BFD_INVALID_SOCKET;
    }
    
    memset(pbfd, 0, sizeof(BFD_Buffer));
    pbfd->state = BFD_FD_NOT_IN_USE;

#ifdef HAVE_WINSOCK2_H
    bfdtemp = socket(family, type, protocol);
    DuplicateHandle(GetCurrentProcess(), (HANDLE)bfdtemp, GetCurrentProcess(), &(HANDLE)(pbfd->real_fd), 0, FALSE, DUPLICATE_CLOSE_SOURCE | DUPLICATE_SAME_ACCESS);
#else
    pbfd->real_fd = socket(family, type, protocol);
#endif

    if (pbfd->real_fd == SOCKET_ERROR) 
    {
	DBG_MSG("ERROR in bsocket: socket returned SOCKET_ERROR\n");
	memset(pbfd, 0, sizeof(BFD_Buffer));
	BlockFree( Bsocket_mem, pbfd );
	return BFD_INVALID_SOCKET;
    }

    snprintf(pbfd->string, BSTRINGLEN, "0x%p:%d", pbfd, pbfd->real_fd);

    return (int)pbfd;
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
    DBG_MSG("Enter bbind\n");
    /*dbg_printf("bbind\n");*/
    
    return bind(((BFD_Buffer*)bfd)->real_fd, servaddr, servaddr_len);
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
    /*dbg_printf("blisten\n");*/
    return listen(((BFD_Buffer*)bfd)->real_fd, backlog);
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
    /*dbg_printf("bsetsockopt\n");*/
    return setsockopt(((BFD_Buffer*)bfd)->real_fd, level, optname, optval, optlen);
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
    int 	       conn_fd, bfdtemp;
    BFD_Buffer 	       *new_bfd;
    
    DBG_MSG("Enter baccept\n");
    /*dbg_printf("baccept\n");*/
    
    bfdtemp = accept(((BFD_Buffer*)bfd)->real_fd, cliaddr, clilen);
    if (bfdtemp == SOCKET_ERROR) 
    {
	DBG_MSG("ERROR in baccept: accept returned SOCKET_ERROR\n");
	return BFD_INVALID_SOCKET;
    }
    
    new_bfd = (BFD_Buffer *)BlockAlloc( Bsocket_mem );
    if (new_bfd == 0) 
    {
	DBG_MSG(("ERROR in baccept: BlockAlloc return NULL\n"));
	return BFD_INVALID_SOCKET;
    }

#ifdef HAVE_WINSOCK2_H
    DuplicateHandle(GetCurrentProcess(), (HANDLE)bfdtemp, GetCurrentProcess(), (HANDLE*)&conn_fd, 0, FALSE, DUPLICATE_CLOSE_SOURCE | DUPLICATE_SAME_ACCESS);
#else
    conn_fd = bfdtemp;
#endif

    memset(new_bfd, 0, sizeof(BFD_Buffer));
    new_bfd->real_fd = conn_fd;
    new_bfd->state = BFD_IDLE;

    snprintf(new_bfd->string, BSTRINGLEN, "0x%p:%d", new_bfd, new_bfd->real_fd);

    return (int)new_bfd;
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
    /*dbg_printf("bconnect\n");*/
    return connect(((BFD_Buffer*)bfd)->real_fd, servaddr, servaddr_len);
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
    int 	   nbfds;
    bfd_set        rcopy;
    BFD_Buffer     *p;
    int            i;

    DBG_MSG("Enter bselect\n");
    /*dbg_printf("bselect\n");*/
    
    if (readbfds)
    {
	nbfds = 0;
	rcopy = *readbfds;
	/* check to see if there are any bfds with buffered data */
	for (i=0; i<readbfds->n; i++)
	{
	    p = readbfds->p[i];
	    if (p->num_avail > 0 && (FD_ISSET(p->real_fd, &rcopy.set)))
	    {
		FD_SET((unsigned int)p->real_fd, &readbfds->set);
		nbfds++;
	    }
	}
	if (nbfds)
	{
	    /* buffered data is available, return it plus any writeable bfds */
	    if (writebfds)
	    {
		maxfds = ((BFD_Buffer*)maxfds)->real_fd + 1;
		i = select(maxfds, NULL, &writebfds->set, NULL, tv);
		if (i != SOCKET_ERROR)
		    nbfds += i;
	    }
	    return nbfds;
	}
    }

    if (maxfds)
	maxfds = ((BFD_Buffer*)maxfds)->real_fd + 1;
    nbfds = select(maxfds, 
	readbfds ? &readbfds->set : NULL, 
	writebfds ? &writebfds->set : NULL, 
	execbfds ? &execbfds->set : NULL, tv);
    if (nbfds == SOCKET_ERROR) 
	return SOCKET_ERROR;
    
    if (readbfds)
    {
	for (i=0; i<readbfds->n; i++)
	{
	    p = readbfds->p[i];
	    if (p->num_avail > 0 && (FD_ISSET(p->real_fd, &rcopy.set)) && !(FD_ISSET(p->real_fd, &readbfds->set)))
	    {
		FD_SET((unsigned int)p->real_fd, &readbfds->set);
		nbfds++;
	    }
	}
    }
    
    return nbfds;
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
    /*dbg_printf("bwrite\n");*/
    return bfd_write(((BFD_Buffer*)bfd)->real_fd, ubuf, len);
    /*return bfd_write(((BFD_Buffer*)bfd)->real_fd, ubuf, BSOCKET_MIN(len, 20*1024));*/
}

/*
#define DBG_BWRITEV
#define DBG_BWRITEV_PRINT(a) BPRINTF a
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
    BPRINTF("(bwritev");
    for (i=0; i<n; i++)
	BPRINTF(":%d", pIOVec[i].B_VECTOR_LEN);
#endif
    if (WSASend(((BFD_Buffer*)bfd)->real_fd, pIOVec, n, &dwNumSent, 0, NULL/*overlapped*/, NULL/*completion routine*/) == SOCKET_ERROR)
    {
	if (WSAGetLastError() != WSAEWOULDBLOCK)
	{
	    return SOCKET_ERROR;
	}
    }
    DBG_BWRITEV_PRINT(("->%d)", dwNumSent));
    return dwNumSent;
#else
    int nWritten;
    /*bg_printf("bwritev\n");*/
    nWritten = writev(((BFD_Buffer*)bfd)->real_fd, pIOVec, n);
    return nWritten;
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
    int      fd;
    int      num_used;
    int      num_copied;
    int      n;
    char     *bbuf;
    BFD_Buffer *pbfd;
    
    DBG_MSG("Enter bread\n");
    /*dbg_printf("bread\n");*/
    
    pbfd = (BFD_Buffer*)bfd;

    if (pbfd->state == BFD_ERROR) 
	return pbfd->errval;
    
    pbfd->state = BFD_READING;
    fd = pbfd->real_fd;
    bbuf = pbfd->read_buf;
    
    if (len <= pbfd->num_avail) 
    {
	memcpy(ubuf, bbuf + pbfd->curpos, len);
	pbfd->curpos += len;
	pbfd->num_avail -= len;
	if (pbfd->num_avail == 0)
	    pbfd->curpos = 0;

	DBG_MSG(("bread: copied %d bytes into ubuf starting at bbuf[%d]\n", len, *(bbuf) + conn->curpos));
	
	return len;
    }
    
    if (pbfd->num_avail > 0) 
    {
	memcpy(ubuf, bbuf + pbfd->curpos, pbfd->num_avail);
	ubuf += pbfd->num_avail;
	len -= pbfd->num_avail;
	pbfd->curpos = 0;
    }
    
    if (len > g_bbuflen) 
    {
	n = bfd_read(fd, ubuf, len);
	if (n == 0) 
	{
	    pbfd->state = BFD_ERROR;
	    pbfd->errval = 0;
	}
	else if (n == SOCKET_ERROR) 
	{
	    if ((errno != EINTR) || (errno != EAGAIN)) 
	    {
		pbfd->state = BFD_ERROR;
		pbfd->errval = errno;
	    }
	    n = 0;
	}
	
	DBG_MSG(("bread: Read %d bytes directly into ubuf\n", n));
	n += pbfd->num_avail;
	pbfd->num_avail = 0;

	return n;
    }
    
    num_copied = pbfd->num_avail;
    n = bfd_read(fd, bbuf, g_bbuflen);
    pbfd->curpos = 0;
    if (n == 0) 
    {
	pbfd->state = BFD_ERROR;
	pbfd->errval = 0;
    }
    else if (n == SOCKET_ERROR) 
    {
	if ((errno != EINTR) || (errno != EAGAIN)) 
	{
	    pbfd->state = BFD_ERROR;
	    pbfd->errval = errno;
	}
	if (pbfd->num_avail)
	    return pbfd->num_avail;
	return SOCKET_ERROR;
    }
    
    pbfd->num_avail = n;
    num_used = (((len) < (pbfd->num_avail)) ? (len) : (pbfd->num_avail));
    memcpy(ubuf, bbuf, num_used);
    pbfd->curpos += num_used;
    pbfd->num_avail -= num_used;
    /*if (pbfd->num_avail > 0) BPRINTF("bread: %d extra bytes read into bbuf %d\n", pbfd->num_avail, pbfd->real_fd);*/
    pbfd->state = BFD_IDLE;

    DBG_MSG(("bread: Read %d bytes on socket %d into bbuf\n", n, fd));
    DBG_MSG(("bread: copied %d bytes into ubuf from bbuf\n", num_used));
    
    n = num_used + num_copied;
    return n;
}

/*
#define DBG_BREADV
#define DBG_BREADV_PRINT(a) BPRINTF a
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
    int      fd;
    int      i;
    char     *bbuf;
    BFD_Buffer *pbfd;
    int      num_read = 0;
#ifdef HAVE_WINSOCK2_H
    DWORD    n = 0;
    DWORD    nFlags = 0;
#else
    int      n = 0;
#endif
    B_VECTOR pVector[B_VECTOR_LIMIT];
    int iVector;

    DBG_MSG("Enter breadv\n");
    /*dbg_printf("breadv\n");*/
    
    pbfd = (BFD_Buffer*)bfd;
    
    if (pbfd->state == BFD_ERROR) 
	return pbfd->errval;
    
    pbfd->state = BFD_READING;
    fd = pbfd->real_fd;
    bbuf = pbfd->read_buf;
    
#ifdef DBG_BREADV
    BPRINTF("(breadv");
    for (i=0; i<veclen; i++)
	BPRINTF(":%d", vec[i].B_VECTOR_LEN);
#endif
    num_read = 0;
    for (i=0; i<veclen; i++)
    {
	if (pbfd->num_avail)
	{
	    n = BSOCKET_MIN((unsigned int)pbfd->num_avail, vec[i].B_VECTOR_LEN);
	    DBG_BREADV_PRINT((",bcopy %d", n));
	    memcpy(vec[i].B_VECTOR_BUF, bbuf + pbfd->curpos, n);
	    if ((unsigned int)pbfd->num_avail <= vec[i].B_VECTOR_LEN)
	    {
		if ((unsigned int)pbfd->num_avail == vec[i].B_VECTOR_LEN)
		{
		    i++;
		    if (i==veclen)
		    {
			pbfd->num_avail = 0;
			pbfd->curpos = 0;
			DBG_BREADV_PRINT(("->%d,%da)", num_read+n, pbfd->num_avail));
			return num_read + n;
		    }
		}
		else
		{
		    /* Make a copy of the vector */
		    for (iVector = 0; iVector <= i; iVector++)
		    {
			pVector[iVector].B_VECTOR_BUF = vec[iVector].B_VECTOR_BUF;
			pVector[iVector].B_VECTOR_LEN = vec[iVector].B_VECTOR_LEN;
		    }
		    pVector[i].B_VECTOR_BUF += n;
		    pVector[i].B_VECTOR_LEN -= n;
		    vec = pVector;
		}
	    }
	    pbfd->num_avail -= n;
	    pbfd->curpos += n;
	    num_read += n;
	}
	
	if (pbfd->num_avail == 0)
	{
	    pbfd->curpos = 0;
	    break;
	}
	if (i == veclen - 1)
	{
	    DBG_BREADV_PRINT(("->%d,%db)", num_read, conn->num_avail));
	    return num_read;
	}
    }
    
    vec[veclen].B_VECTOR_BUF = bbuf;
    vec[veclen].B_VECTOR_LEN = g_bbuflen;

#ifdef DBG_BREADV
    BPRINTF(",breadv");
    for (k=0; k<veclen-i+1; k++)
	BPRINTF(":%d", vec[k+i].B_VECTOR_LEN);
#endif
#ifdef HAVE_WINSOCK2_H
    if (WSARecv(fd, &vec[i], veclen - i + 1, &n, &nFlags, NULL/*overlapped*/, NULL/*completion routine*/) == SOCKET_ERROR)
    {
	if (WSAGetLastError() != WSAEWOULDBLOCK)
	{
	    pbfd->state = BFD_ERROR;
	    pbfd->errval = WSAGetLastError();
	    BPRINTF("***WSARecv failed reading %d WSABUFs, error %d***\n", veclen - i + 1, pbfd->errval);
	    for (k=0; k<veclen-i+1; k++)
		BPRINTF("vec[%d] len: %d\nvec[%d] buf: 0x%x\n", k+i, vec[k+i].B_VECTOR_LEN, k+i, vec[k+i].B_VECTOR_BUF);
	    n = 0; /* Set this to zero so it can be added to num_read */
	}
    }
#else
    n = readv(fd, &vec[i], veclen - i + 1);
    if (n == SOCKET_ERROR) 
    {
	if ((errno != EINTR) || (errno != EAGAIN)) 
	{
	    pbfd->state = BFD_ERROR;
	    pbfd->errval = errno;
	}
	n = 0; /* Set this to zero so it can be added to num_read */
    }
#endif
    
    if (n)
    {
	for ( ; i <= veclen; i++)
	{
	    if (i == veclen)
	    {
		pbfd->num_avail = n;
	    }
	    else
	    {
		num_read += BSOCKET_MIN(vec[i].B_VECTOR_LEN, n);
		n = n - BSOCKET_MIN(vec[i].B_VECTOR_LEN, n);
		if (n == 0)
		{
		    DBG_BREADV_PRINT(("->%d,%dc)", num_read, pbfd->num_avail));
		    return num_read;
		}
	    }
	}
    }

    DBG_BREADV_PRINT(("->%d,%dd)", num_read, pbfd->num_avail));
    return num_read;
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
    /*dbg_printf("bclose\n");*/

    bfd_close(((BFD_Buffer*)bfd)->real_fd);
    memset((void*)bfd, 0, sizeof(BFD_Buffer));
    BlockFree( Bsocket_mem, (BFD_Buffer*)bfd );

    return 0;
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
    /*dbg_printf("bgetsockname\n");*/
    return getsockname(((BFD_Buffer*)bfd)->real_fd, name, namelen);
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
    /*dbg_printf("bmake_nonblocking\n");*/
    
#ifdef HAVE_WINDOWS_SOCKET
    rc = ioctlsocket(((BFD_Buffer*)bfd)->real_fd, FIONBIO, &flag);
#else
    rc = ioctl(((BFD_Buffer*)bfd)->real_fd, FIONBIO, &flag);
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
    /*dbg_printf("bmake_blocking\n");*/
    
#ifdef HAVE_WINDOWS_SOCKET
    rc = ioctlsocket(((BFD_Buffer*)bfd)->real_fd, FIONBIO, &flag);
#else
    rc = ioctl(((BFD_Buffer*)bfd)->real_fd, FIONBIO, &flag);
#endif
    
    return rc;
}

#endif /* NO_BSOCKETS #else */

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
    struct linger linger;
    int len;

    /*dbg_printf("beasy_create\n");*/

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

    /* Set the linger on close option */
    linger.l_onoff = 1 ;
    linger.l_linger = 60;
    bsetsockopt(*bfd, SOL_SOCKET, SO_LINGER, (char*)&linger, sizeof(linger));

#ifdef HAVE_WINSOCK2_H
    /* set the socket buffer size to 64k */
    len = sizeof(int);
    if (!getsockopt(bget_fd((*bfd)), SOL_SOCKET, SO_RCVBUF, (char*)&optval, &len))
    {
	optval = 64*1024;
	bsetsockopt(*bfd, SOL_SOCKET, SO_RCVBUF, (char*)&optval, sizeof(int));
    }
    len = sizeof(int);
    if (!getsockopt(bget_fd((*bfd)), SOL_SOCKET, SO_SNDBUF, (char*)&optval, &len))
    {
	optval = 64*1024;
	bsetsockopt(*bfd, SOL_SOCKET, SO_SNDBUF, (char*)&optval, sizeof(int));
    }
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
-  int port - port

   Notes:
@*/
int beasy_connect(int bfd, char *host, int port)
{
    int error;
    int reps = 0;
    struct hostent *lphost;
    struct sockaddr_in sockAddr;
    struct linger linger;
#ifdef HAVE_WINSOCK2_H
    /* use this array to make sure the warning only gets logged once */
    BOOL bWarningLogged[4] = { FALSE, FALSE, FALSE, FALSE };
#endif

    dbg_printf("beasy_connect(%s:%d)\n", host, port);

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
    
    while (bconnect(bfd, (struct sockaddr*)&sockAddr, sizeof(sockAddr)) == SOCKET_ERROR)
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
	    && (reps < 10) )
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

    /* Set the linger on close option */
    linger.l_onoff = 1 ;
    linger.l_linger = 60;
    bsetsockopt(bfd, SOL_SOCKET, SO_LINGER, (char*)&linger, sizeof(linger));

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
    struct linger linger;
    struct sockaddr addr;
    int len;
    int client;

    dbg_printf("beasy_accept\n");

    len = sizeof(addr);
    client = baccept(bfd, &addr, &len);

    if (client == BFD_INVALID_SOCKET)
    {
	return BFD_INVALID_SOCKET;
    }

    linger.l_onoff = 1;
    linger.l_linger = 60;
    bsetsockopt(client, SOL_SOCKET, SO_LINGER, (char*)&linger, sizeof(linger));

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
#ifdef HAVE_WINSOCK2_H
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
		BPRINTF("wait for close timed out\n");fflush(stdout);
	    }
	    else
	    {
		BPRINTF("wait for close succeeded\n");fflush(stdout);
	    }
	    */
	    WSACloseEvent(hEvent);
	}
	else
	    shutdown(bget_fd(bfd), SD_BOTH);
    }
    else
	shutdown(bget_fd(bfd), SD_BOTH);
#endif
    dbg_printf("beasy_closesocket\n");
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

    dbg_printf("beasy_get_sock_info: ");

    getsockname(bget_fd(bfd), (struct sockaddr*)&addr, &name_len);
    *port = ntohs(addr.sin_port);
    gethostname(name, 100);

    dbg_printf("%s:%d\n", name, *port);

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

    dbg_printf("beasy_get_ip_string: ");

    gethostname(hostname, 100);
    pH = gethostbyname(hostname);
    if (pH == NULL)
	return SOCKET_ERROR;
    a = (unsigned char)(pH->h_addr_list[0][0]);
    b = (unsigned char)(pH->h_addr_list[0][1]);
    c = (unsigned char)(pH->h_addr_list[0][2]);
    d = (unsigned char)(pH->h_addr_list[0][3]);
    snprintf(ipstring, 100, "%u.%u.%u.%u", a, b, c, d);

    dbg_printf("%s\n", ipstring);

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

    dbg_printf("beasy_get_ip\n");

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

    /*dbg_printf("beasy_receive\n");*/
    
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
    
    while (len)
    {
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
		    /*BPRINTF("beasy_receive: socket closed\n");*/
		    /*bmake_blocking(bfd);*/
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

    /*bmake_blocking(bfd);*/
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

    /*dbg_printf("beasy_receive_some\n");*/
    
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
		/*BPRINTF("beasy_receive_some: socket closed\n");*/
		/*bmake_blocking(bfd);*/
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

    /*dbg_printf("beasy_receive_timeout\n");*/
    
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
	BFD_SET(bfd, &readfds);
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
		    /*BPRINTF("beasy_receive_timeout: socket closed\n");*/
		    /*bmake_blocking(bfd);*/
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
		/*bmake_blocking(bfd);*/
		return total - len;
	    }
	}
    }
    /*bmake_blocking(bfd);*/
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

    while ((num_sent = bwrite(bfd, buffer, length)) == SOCKET_ERROR)
    {
	error = WSAGetLastError();
	if (error == WSAEWOULDBLOCK)
	{
            /*Sleep(0);*/
	    continue;
	}
	if (error == WSAENOBUFS)
	{
	    /* If there is no buffer space available then split the buffer in half and send each piece separately.*/
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

    /*dbg_printf("beasy_send\n");*/
    
    num_written = bwrite(bfd, buffer, length);
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
	    num_written = bwrite(bfd, buffer, length);
	    if (num_written == SOCKET_ERROR)
	    {
		if ((errno != EINTR) || (errno != EAGAIN))
		    return SOCKET_ERROR;
	    }
	    else
	    {
		if (num_written == 0)
		{
		    /*BPRINTF("beasy_send: socket closed\n");*/
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
    /*dbg_printf("beasy_getlasterror\n");*/
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
	LocalFree(str);
	return num_bytes+1;
    }
    LocalFree(str);
    strtok(str, "\r\n"); /* remove any CR/LF characters from the output */
#else
    /*dbg_printf("beasy_error_to_string\n");*/
    strncpy(str, strerror(error), length);
#endif
    return 0;
}
