#define get_proc_info(id) (&(p4_global->proctable[id]))

#ifndef FD_ZERO
#    define FD_ZERO(ptr) ((ptr)->fds_bits[0]) = 0;
#    define FD_SET(fd,ptr) ((ptr)->fds_bits[0]) |= 1 << fd;
#    define FD_ISSET(fd,ptr) (((ptr)->fds_bits[0]) & (1 << fd))
#endif

#ifdef P4SYSV
/*
 * Select uses bit masks of file descriptors in longs.
 * These macros manipulate such bit fields (the filesystem macros use chars).
 * FD_SETSIZE may be defined by the user.
 */

#ifndef FD_SETSIZE
#define FD_SETSIZE      256
#endif

#ifndef howmany
#define howmany(x, y)   (((x)+((y)-1))/(y))
#endif

#ifndef NFDBITS
typedef long    fd_mask;
#define NFDBITS (sizeof(fd_mask) * 8)           /* bits per mask */
typedef struct fd_set {
	fd_mask fds_bits[howmany(FD_SETSIZE, NFDBITS)];
} fd_set;
#endif

#endif

/* If strings.h is defined, we have index (I hope!) */
#if !defined(HAVE_STRINGS_H)
#if defined(P4SYSV)
#    ifndef index
#        define index(S,C)        strchr((S),(C))
#    endif
#    ifndef rindex
#        define rindex(S,C)       strrchr((S),(C))
#    endif
#endif
#endif

#if defined(P4SYSV)
#    ifndef bcopy
#        define bcopy(x,y,len)    memcpy((y),(x),(len))
#        define bcmp(x,y,len)     memcmp((y),(x),(len))
#        define bzero(x,len)      memset((x),0,(len))
#    endif
#endif

#define SOFTERR (p4_local->soft_errors)

#define SYSCALL_P4(RC,SYSCALL)                       \
	do {                                         \
	    RC = SYSCALL;                            \
	} while (RC < 0 && errno == EINTR);

/* Signal blocking.  Block and then release the old sig */
#if defined(HAVE_SIGPROCMASK)
#define P4_BLOCK_SIG_DECL 
#define P4_BLOCK_SIG(sig) \
    { sigset_t set;\
    sigemptyset( &set );\
    sigaddset( &set, sig );\
    sigprocmask( SIG_BLOCK, &set, (sigset_t *)0 );\
    }
#define P4_RELEASE_SIG(sig) \
    { sigset_t set;\
    sigemptyset( &set );\
    sigaddset( &set, sig );\
    sigprocmask( SIG_UNBLOCK, &set, (sigset_t *)0 ); \
    }
#elif defined(HAVE_SIGHOLD)
#define P4_BLOCK_SIG_DECL 
#define P4_BLOCK_SIG(sig)  sighold(sig)
#define P4_RELEASE_SIG(sig) sigrelse(sig)
#elif defined(HAVE_SIGBLOCK) && defined(HAVE_SIGSETMASK)
#define P4_BLOCK_SIG_DECL     int blockoldmask
#define P4_BLOCK_SIG(sig)     blockoldmask = sigblock(sigmask(sig)))
#define P4_RELEASE_SIG(sig)   sigsetmask(blockoldmask)
#else
#error 'Unknown signal handling'
#endif

