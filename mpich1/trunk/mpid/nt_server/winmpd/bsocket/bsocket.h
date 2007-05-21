/* -*- Mode: C; c-basic-offset:4 ; -*- */
/*
 *  (C) 2001 by Argonne National Laboratory.
 *      See COPYRIGHT in top-level directory.
 */
#ifndef BSOCKET_H
#define BSOCKET_H

#ifdef __cplusplus
extern "C" {
#endif

#define HAVE_WINDOWS_H
#define HAVE_WINDOWS_SOCKET
#define HAVE_WINSOCK2_H
#define HAVE_WIN32_SLEEP
#define HAVE_NT_LOCKS
#define HAVE_MAPVIEWOFFILE
#define HAVE_CREATEFILEMAPPING
#define HAVE_INTERLOCKEDEXCHANGE
#define HAVE_BOOL

/* sockaddr_in (Internet) */
#ifdef HAVE_NETINET_IN_H
#include <netinet/in.h> 
#endif
#ifdef HAVE_WINSOCK2_H
#include <winsock2.h>
#include <windows.h>
#endif
#ifdef HAVE_SYS_SOCKET_H
#include <sys/socket.h>
#endif
#ifdef HAVE_NETINET_TCP_H
#include <netinet/tcp.h> 
#endif

#ifndef SOCKET_ERROR
#define SOCKET_ERROR -1
#endif

#ifndef ADDR_ANY
#define ADDR_ANY 0
#endif
#ifndef INADDR_ANY
#define INADDR_ANY 0
#endif

#ifdef HAVE_WINDOWS_SOCKET
#define bfd_close closesocket
#define bfd_read(a,b,c) recv(a,b,c,0)
#define bfd_write(a,b,c) send(a,b,c,0)
#else
#define bfd_close close
#define bfd_read(a,b,c) read(a,b,c)
#define bfd_write(a,b,c) write(a,b,c)
#endif

#ifdef HAVE_WINSOCK2_H
#ifndef socklen_t
typedef int socklen_t;
#endif
#else
#ifndef socklen_t
#define socklen_t int
#endif
#endif

#ifdef HAVE_WINSOCK2_H
#define B_VECTOR         WSABUF
#define B_VECTOR_LEN     len
#define B_VECTOR_BUF     buf
#else
#ifdef HAVE_SYS_UIO_H
#include <sys/uio.h>
#endif
#define B_VECTOR         struct iovec
#define B_VECTOR_LEN     iov_len
#define B_VECTOR_BUF     iov_base
#endif
#define B_VECTOR_LIMIT   16

#define BFD_INVALID_SOCKET -1

#ifdef NO_BSOCKETS

#define bfd_set fd_set
#define BFD_CLR(bfd, s)       FD_CLR((unsigned int)bfd,s)
#define BFD_ZERO(s)           FD_ZERO(s)
#define BFD_SET(bfd, s)       FD_SET((unsigned int)bfd,s)
#define BFD_ISSET(bfd, s)     FD_ISSET((unsigned int)bfd,s)

#define BFD_MAX(a,b) (((a) > (b)) ? (a) : (b))

#define bget_fd(bfd) bfd
#define bclr(bfd, s) FD_CLR( (unsigned int)bfd, s )
#define bset(bfd, s) FD_SET( (unsigned int)bfd, s )
#define bsocket(family, type, protocol) socket(family, type, protocol)
#define bbind(bfd, servaddr, servaddr_len) bind(bfd, servaddr, servaddr_len)
#define blisten(bfd, backlog) listen(bfd, backlog)
#define bsetsockopt(bfd, level, optname, optval, optlen) setsockopt(bfd, level, optname, optval, optlen)
#define baccept(bfd, cliaddr, clilen) accept(bfd, cliaddr, clilen)
#define bconnect(bfd, servaddr, servaddr_len) connect(bfd, servaddr, servaddr_len)
#define bselect(maxfds, readbfds, writebfds, execbfds, tv) select(maxfds, readbfds, writebfds, execbfds, tv)
#define bwrite(bfd, ubuf, len) bfd_write(bfd, ubuf, len)
#define bread(bfd, ubuf, len) bfd_read(bfd, ubuf, len)
#define bclose(bfd) bfd_close(bfd)
#define bgetsockname(bfd, name, namelen) getsockname(bfd, name, namelen)

#else /* #ifdef NO_BSOCKETS */

typedef struct BFD_Buffer_struct BFD_Buffer;
typedef struct
{
    fd_set set;
    int n;
    BFD_Buffer *p[FD_SETSIZE];
} bfd_set;
/*#define BFD_CLR(bfd, s)       FD_CLR( bget_fd(bfd), & (s) -> set )*/
#define BFD_CLR(bfd, s)       bclr( bfd, s )
#define BFD_ZERO(s)           { FD_ZERO(& (s) -> set); (s) -> n = 0; }
#define BFD_SET(bfd, s)       bset( bfd , s )
#define BFD_ISSET(bfd, s)     FD_ISSET( bget_fd(bfd), & (s) -> set )
/*
#define bfd_set                 fd_set
#define BFD_CLR(bfd, set)       FD_CLR( bget_fd(bfd), set )
#define BFD_ZERO(set)           FD_ZERO(set)
#define BFD_SET(bfd, set)       FD_SET( bget_fd(bfd), set )
#define BFD_ISSET(bfd, set)     FD_ISSET( bget_fd(bfd), set )
*/

#define BFD_MAX(a,b) (((bget_fd(a)) > (bget_fd(b))) ? (a) : (b))

/* bsockets.c */
unsigned int bget_fd(int bfd);
void bset(int bfd, bfd_set *s);
void bclr(int bfd, bfd_set *s);
int bsocket_init( void );
int bsocket_finalize( void );
int bsocket( int, int, int );
int bbind( int, const struct sockaddr *, socklen_t );
int blisten( int, int );
int bsetsockopt( int, int, int, const void *, socklen_t );
int baccept( int, struct sockaddr *, socklen_t * );
int bconnect( int, const struct sockaddr *, socklen_t );
int bread( int, char *, int );
int breadwrite( int, int, char *, int, int *, int * );
int breadvwrite( int, int, B_VECTOR *, int, int *, int * );
int bwrite( int, char *, int );
int bclose( int );
int bclose_all( void );
int bgetsockname(int bfd, struct sockaddr *name, int *namelen );
int bselect( int maxfds, bfd_set *readbfds, bfd_set *writebfds, bfd_set *execbfds, struct timeval *tv );

#endif /* #else #ifdef NO_BSOCKETS */

int bsocket_init( void );
int bsocket_finalize( void );
int breadv( int, B_VECTOR *, int );
int bwritev( int, B_VECTOR *, int );
int bmake_nonblocking( int );
int bmake_blocking( int );
char *bto_string( int );
void bprint_set(bfd_set *);

int beasy_create(int *bfd, int port, unsigned long addr);
int beasy_connect(int bfd, char *host, int port);
int beasy_connect_quick(int bfd, char *host, int port);
int beasy_connect_timeout(int bfd, char *host, int port, int seconds);
int beasy_accept(int bfd);
int beasy_closesocket(int bfd);
int beasy_get_sock_info(int bfd, char *name, int *port);
int beasy_get_ip_string(char *ipstring);
int beasy_get_ip(unsigned long *ip);
int beasy_receive(int bfd, char *buffer, int len);
int beasy_receive_timeout(int bfd, char *buffer, int len, int timeout);
int beasy_receive_some(int bfd, char *buffer, int len);
int beasy_send(int bfd, char *buffer, int length);
int beasy_getlasterror(void);
int beasy_error_to_string(int error, char *str, int length);

#ifdef __cplusplus
}
#endif

#endif
