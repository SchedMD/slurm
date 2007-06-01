/* general declarations needed by p2p that don't fit elsewhere */

#if !defined(P2P_EXTERN)
#define P2P_EXTERN extern
#endif

/* Definitions for improving Cache locality */

#define MPID_CACHE_ALIGN	/* could become machine-dependent  */

#if defined(MPID_CACHE_ALIGN)
#    define PAD(n) char pad[n];
#else
#    define PAD(n)
#endif

/* It is often advisable to flush cache of shared memory objects when
   they are no longer needed.  This macro does that; if it is undefined,
   then nothing happens 
 */
#ifndef MPID_FLUSH_CACHE
#    define  MPID_FLUSH_CACHE(addr,size)
#endif

/* function declarations for p2p */

double p2p_wtime        (void);
void   p2p_wtime_init   (void);
void   p2p_init         (int,int);
void   p2p_shfree       (char *);
void   p2p_cleanup      (void);
void   p2p_error        (char *, int);
void   p2p_syserror     (char *, int);
void   p2p_setpgrp      (void);
void   p2p_yield        (void);
void   p2p_kill_procs   (void); 
void   p2p_clear_signal (void); 
void   p2p_cleanup      (void); 
void   p2p_makesession  (void);
void   p2p_create_procs (int,int,char **);
int    p2p_proc_info    ( int, char **, char ** );

#ifdef USE_DISTRIB_SHMALLOC
    void *p2p_shmalloc  (int, int);
#else
    void *p2p_shmalloc  (int);
#endif
