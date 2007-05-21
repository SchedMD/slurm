#ifdef CAN_DO_SOCKET_MSGS
#include <sys/socket.h>
#endif
#include <netdb.h>

#ifdef P4BSD
#include <sys/wait.h>
#include <sys/resource.h>
#endif

#ifdef CAN_DO_SETSOCKOPT
#include <netinet/tcp.h>
#endif

#define NET_RECV_GOOD 0
#define NET_RECV_EOF  -1    

#define NET_EXEC 1
#define NET_DONE 2
#define NET_RESPONSE 3

#ifndef SOCK_BUFF_SIZE
#define SOCK_BUFF_SIZE 16384
#endif

#define UNRESERVED_PORT 5001

struct net_message_t 
{
    int type:32;
    int port:32;
    int success:32;
    char pgm[256];
    char host[128];
    char am_slave[32];
    char message[512];
};

/* Definitions for system messages between slaves */
/* Macros to convert from integer to net byte order and vice versa */
#ifdef P4BSD
#define p4_i_to_n(n)  (int) htonl( (u_long) n)
#define p4_n_to_i(n)  (int) ntohl( (u_long) n)
#endif

#ifdef P4SYSV
#if defined(IPSC860)  &&  !defined(IPSC860_SOCKETS)
#define p4_i_to_n(n)  (n)
#define p4_n_to_i(n)  (n)
#else
#define p4_i_to_n(n)  (int) htonl(n)
#define p4_n_to_i(n)  (int) ntohl(n)
#endif
#endif
