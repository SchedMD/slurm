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

#ifdef HAVE_WINBCONF_H
#include "winbconf.h"
#else
#include "bconf.h"
#endif

/* sockaddr_in (Internet) */
#ifdef HAVE_NETINET_IN_H
#include <netinet/in.h> 
#endif
#ifdef HAVE_WINSOCK2_H
#define FD_SETSIZE 256
#include <winsock2.h>
#include <windows.h>
#endif

#ifndef SOCKET_ERROR
#define SOCKET_ERROR -1
#endif

#ifdef HAVE_WINDOWS_SOCKET
#define close closesocket
#define read(a,b,c) recv(a,b,c,0)
#define write(a,b,c) send(a,b,c,0)
#endif

#ifdef HAVE_WINSOCK2_H
#ifndef socklen_t
typedef int socklen_t;
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
typedef struct fd_set bfd_set;
#define BFD_CLR(bfd, s)       FD_CLR((unsigned int)bfd,s)
#define BFD_ZERO(s)           FD_ZERO(s)
#define BFD_SET(bfd, s)       FD_SET((unsigned int)bfd,s)
#define BFD_ISSET(bfd, s)     FD_ISSET((unsigned int)bfd,s)

#ifndef MAX
#define MAX(a,b)            (((a) > (b)) ? (a) : (b))
#endif
#define BFD_MAX(a,b) MAX(a,b)

/* bsockets.c */
unsigned int bget_fd(int bfd);
int bsocket_init( void );
int bsocket_finalize( void );
int bsocket( int, int, int );
int bbind( int, const struct sockaddr *, socklen_t );
int blisten( int, int );
int bsetsockopt( int, int, int, const void *, socklen_t );
int baccept( int, struct sockaddr *, socklen_t * );
int bconnect( int, const struct sockaddr *, socklen_t );
int bread( int, char *, int );
int breadv( int, B_VECTOR *, int );
int breadwrite( int, int, char *, int, int *, int * );
int breadvwrite( int, int, B_VECTOR *, int, int *, int * );
int bwrite( int, char *, int );
int bwritev( int, B_VECTOR *, int );
int bclose( int );
int bclose_all( void );
int bgetsockname(int bfd, struct sockaddr *name, int *namelen );
int bselect( int maxfds, bfd_set *readbfds, bfd_set *writebfds, bfd_set *execbfds, struct timeval *tv );
int bmake_nonblocking( int );

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
int beasy_getlasterror();
int beasy_error_to_string(int error, char *str, int length);

#ifdef __cplusplus
}
#endif

#endif
