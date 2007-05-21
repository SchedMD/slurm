#define P2P_EXTERN

/* This is the main file for the p2 shared-memory code (from P4, hence p2)
   
   In order to keep this file from getting too ugly, I've broken it down
   into subfiles for the various options.  

   The subfiles are:
   
   p2pshmat - SYSV shared memory allocator
   p2pshop  - SYSV semaphores
   p2pmmap  - Memory map
   p2pcnx   - Code for the Convex SPP
   p2pirix  - Code for special IRIX routines (usmalloc etc).

   These are INCLUDED in this file so that the Makefile doesn't need to
   know which files to compile.
 */

#include "mpid.h"
#ifdef malloc
#undef malloc
#undef free
#undef calloc
#endif
#include "mpiddev.h"
#include "mpid_debug.h"
#include "p2p.h"
#include <stdio.h>

#ifdef HAVE_SIGNAL_H
#include <signal.h>
#endif

#ifdef HAVE_STDLIB_H
#include <stdlib.h>
#endif

#define p2p_dprintf printf

#if defined(USE_MMAP)
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

void *xx_shmalloc (unsigned);
void xx_shfree (char *);
void xx_init_shmalloc ( char *, unsigned );

#elif defined(USE_SHMAT)
void *MD_init_shmem( int * );

void *xx_shmalloc (unsigned);
void xx_shfree (char *);
void xx_init_shmalloc ( char *, unsigned );

#endif

/* This is a process group, used to help clean things up when a process dies 
   It turns out that this causes strange failures when running a program
   under another program, like a debugger or mpirun.  Until this is
   resolved, I'm ifdef'ing this out.
 */
#if defined(MPI_cpss) || defined(FOO)
static int MPID_SHMEM_ppid = 0;
#endif

void
p2p_setpgrp()
{
#ifdef FOO
   MPID_SHMEM_ppid = getpid();
   if(setpgid((pid_t)MPID_SHMEM_ppid,(pid_t)MPID_SHMEM_ppid)) {
       perror("failure in p2p_setpgrp");
       exit(-1);
   }
#endif

#if defined(MPI_cpss)
   if (cnx_exec == 0) {
	MPID_SHMEM_ppid = getpid();
	if(setpgid((pid_t)MPID_SHMEM_ppid,(pid_t)MPID_SHMEM_ppid)) {
		perror("failure in p2p_setpgrp");
		exit(-1);
	}
   }
#endif
}

void p2p_init(maxprocs,memsize)
int maxprocs;
int memsize;
{
/* Initialize locks first */

#ifdef USE_SEMOP
    MD_init_semop();
#endif
#ifdef USE_MUTEX
    /* Ensure that we are linked with -lthread or -mt on Solaris systems
       (otherwise nonfunctional versions of mutex_xxx functions are supplied
       by libc !).  This may work because libc does not (at least in
       a recent version of Solaris) have thr_getstate while libthread does. 
       The strange test on maxprocs keeps the code from being execute, but
       does force the compiler to include it. */
    if (maxprocs < 0) {
	void (*a)(void);
	void thr_getstate(void);
	a = thr_getstate;
	if (a == 0) abort();
    }
#endif

#if defined(USE_ARENAS) || defined(USE_USLOCKS)
       
    strcpy(p2p_sgi_shared_arena_filename,"/tmp/p2p_shared_arena_");
    sprintf(&(p2p_sgi_shared_arena_filename[strlen(p2p_sgi_shared_arena_filename)]),"%d",getpid());

    if (usconfig(CONF_INITUSERS,maxprocs) == -1)
	p2p_error("p2p_init: usconfig failed for users: \n",maxprocs);
    if (usconfig(CONF_INITSIZE,memsize) == -1)
	p2p_error("p2p_init: usconfig failed: cannot map shared arena\n",
		  memsize);
    if (usconfig(CONF_ARENATYPE,US_SHAREDONLY) == -1)
	p2p_error("p2p_init: usconfig failed: cannot make shared-only\n",0);
    p2p_sgi_usptr = usinit(p2p_sgi_shared_arena_filename);
    if (p2p_sgi_usptr == NULL)
	p2p_error("p2p_init: usinit failed: can't map shared arena\n",memsize);
#endif

#if defined(USE_XX_SHMALLOC)
    {
    caddr_t p2p_start_shared_area;

#if defined(USE_MMAP) 

#if !defined(MAP_ANONYMOUS) && !defined(MAP_VARIABLE)
    static int p2p_shared_map_fd;
    /* In LINUX, we should try to open a large enough file of zeros.
       We can create a temp file, open it, write 0-filled blocks to 
       it, and mark it delete on close.  If you can create a
       /dev/zero.mpi file, try that.
     */
    p2p_shared_map_fd = open("/dev/zero", O_RDWR);
    if (p2p_shared_map_fd < 0) {
	perror( "Open of /dev/zero failed" );
	p2p_error( "OOPS: Could not open anonymous mmap area - check \
protections on /dev/zero\n", 0 );
    }
    p2p_start_shared_area = (char *) mmap((caddr_t) 0, memsize,
			PROT_READ|PROT_WRITE|PROT_EXEC,
			MAP_SHARED, 
			p2p_shared_map_fd, (off_t) 0);

#elif defined(MAP_ANONYMOUS) && !defined(MAP_VARIABLE)
    /* This might work for LINUX eventually */
    p2p_start_shared_area = (char *) mmap((caddr_t) 0, memsize,
			PROT_READ|PROT_WRITE|PROT_EXEC,
			MAP_SHARED|MAP_ANONYMOUS,
			-1, (off_t) 0);

#else
    p2p_start_shared_area = (char *) mmap((caddr_t) 0, memsize,
			PROT_READ|PROT_WRITE|PROT_EXEC,
			MAP_SHARED|MAP_ANONYMOUS|MAP_VARIABLE, 
			-1, (off_t) 0);

    /* fprintf(stderr,"memsize = %d, address = 0x%x\n",
	    memsize,p2p_start_shared_area); */
#endif /* !defined(MAP_ANONYMOUS) */

#elif defined(USE_SHMAT)
    p2p_start_shared_area = MD_init_shmem(&memsize);
#endif

    if (p2p_start_shared_area == (caddr_t)-1)
    {
	p2p_syserror("OOPS: mmap failed: cannot map shared memory, size=",
		  memsize);
    }

    /* Before we initialize shmalloc, we need to initialize any lock 
       information.  Some locks may use some of the shared memory */
    xx_init_shmalloc(p2p_start_shared_area,memsize);
#if defined(MPI_cspp)
    { 
	int	mynode, i, j, k, n;

	mynode = MPID_SHMEM_getNodeId();
	for (i = k = 0; i < numNodes; ++i) {
	    if ((n = numCPUs[i]) == 0) continue;
	    for (j = 0; j < n; ++j) {
		if ((i == mynode) && (j == (n - 1))) masterid = k;
		++k;
	    }
	}
    }
#endif
    }
#endif /* USE_XX_SHMALLOC */

#ifdef USE_SEMOP
    MD_init_sysv_semop();
#endif
}

#include "p2pprocs.c"

void *p2p_shmalloc(size)
int size;
{
    void *p = 0;

#if defined(USE_ARENAS)
    p = usmalloc(size,p2p_sgi_usptr);

#elif defined(USE_XX_SHMALLOC)
    p = (void *) xx_shmalloc(size);
#endif

    return (p);
}

void p2p_shfree(ptr)
char *ptr;
{

#if defined(USE_ARENAS)
    usfree(ptr,p2p_sgi_usptr);

#elif defined(USE_XX_SHMALLOC)
    (void) xx_shfree(ptr);
#endif

}

#if defined(USE_SEMOP)
#    include "p2psemop.c"
#endif

#if defined(USE_SHMAT)

#ifndef P2_SYSV_SHM_SEGSIZE
    /* This was the p4 choice */
    #define P2_SYSV_SHM_SEGSIZE (1*1024*1024)
#endif

#ifndef P2_MAX_SYSV_SHMIDS 
#define P2_MAX_SYSV_SHMIDS 8
#endif
static int sysv_num_shmids = 0;
static int sysv_shmid[P2_MAX_SYSV_SHMIDS];
/* We save the addresses so that we can free them */
static void *sysv_shmat[P2_MAX_SYSV_SHMIDS];

void *MD_init_shmem(int *memsize)
{
    int i,nsegs;
    unsigned size, segsize = P2_SYSV_SHM_SEGSIZE;
    char *mem, *tmem, *pmem;

    /* First, try to allocate the space as a single segment */
    if ((sysv_shmid[0] = shmget(getpid(),*memsize,IPC_CREAT|0600)) != -1)
    {
	if ((mem = (char *)shmat(sysv_shmid[0],NULL,0)) == (char *)-1)
	{
	    printf( "could not allocate mem\n" );
	    shmctl( sysv_shmid[0],IPC_RMID, 0 );
	}
	else {
	    sysv_shmat[0] = mem;
	    sysv_num_shmids++;
	    return mem;
	}
    }

    /* We couldn't get a single segment.  Try for multiple segments */
    if (*memsize  &&  (*memsize % P2_SYSV_SHM_SEGSIZE) == 0)
	nsegs = *memsize / segsize;
    else
	nsegs = *memsize / segsize + 1;
    size = nsegs * segsize;
    *memsize = size;
    if ((sysv_shmid[0] = shmget(getpid(),segsize,IPC_CREAT|0600)) == -1)
    {
	p2p_syserror("OOPS: shmget failed\n",sysv_shmid[0]);
    }
    if ((mem = (char *)shmat(sysv_shmid[0],NULL,0)) == (char *)-1)
    {
	p2p_syserror("OOPS: shmat failed for (id,NULL,0)\n",0);
    }
    /* There are rumors that under LINUX, we can free these things 
       right away, and they then last until process exits. */
    sysv_shmat[0] = mem;
    sysv_num_shmids++;
    nsegs--;
    pmem = mem;
    for (i=1; i <= nsegs; i++)
    {
	if ((sysv_shmid[i] = shmget(i+getpid(),segsize,IPC_CREAT|0600)) == -1)
	{
	    p2p_syserror("OOPS: shmget failed\n",sysv_shmid[i]);
	}
        if ((tmem = (char *)shmat(sysv_shmid[i],pmem+segsize,0)) == (char *)-1)
        {
            if ((tmem = (char *)shmat(sysv_shmid[i],pmem-segsize,0)) == (char *)-1)
            {
		char buf[1024];
		sprintf( buf, "OOPS: shmat failed for segment %d location %ld\n", 			 i, (long)(pmem-segsize));
                p2p_syserror( buf, 0 );
            }
	    else
	    {
		mem = tmem;
	    }
        }
	sysv_shmat[i] = tmem;
	sysv_num_shmids++;
	pmem = tmem;
    }
    return mem;
}

void MD_remove_sysv_mipc( void )
{
    int i;

    /* ignore -1 return codes below due to multiple processes cleaning
       up the same sysv stuff; commented out "if" used to make sure
       that only the cluster master cleaned up in each cluster
    */

    if (sysv_shmid[0] == -1)
	return;
    for (i=0; i < sysv_num_shmids; i++) {
	/* Unmap the addresses */
	shmdt( sysv_shmat[i] );
	/* Remove the ids */
        shmctl(sysv_shmid[i],IPC_RMID,0);
    }
    /*
    if (sysv_semid0 != -1)
	semctl(g->sysv_semid[0],0,IPC_RMID,0); 
    */ /* delete initial set */
}

#endif /* USE_SHMAT */


/* Cleanup is the NORMAL termination code; it may be called in abormal
   termination as well */
void p2p_cleanup()
{
 
#if defined(USE_ARENAS)
    unlink(p2p_sgi_shared_arena_filename);
#endif

#ifdef USE_SEMOP
    MD_remove_sysv_sipc();
#endif

/* The locks (freed in USE_SEMOP code) are stored shared memory, so we have to 
   do this last */
#ifdef USE_SHMAT
    MD_remove_sysv_mipc();
#endif


}

/*****

void p2p_dprintf(fmt, va_alist)
char *fmt;
va_dcl
{
    va_list ap;
 
    printf("%s: ", p2p_my_dprintf_id);
    va_start(ap);
#   if defined(HAVE_VPRINTF)
    vprintf(fmt, ap);
#   else
    _doprnt(fmt, ap, stdout);
#   endif
    va_end(ap);
    fflush(stdout);
}
 
void p2p_dprintfl(level, fmt, va_alist)
int level;
char *fmt;
va_dcl
{
    va_list ap;
 
    if (level > p2p_debug_level)
        return;
    printf("%d: %s: ", level, p2p_my_dprintf_id);
    va_start(ap);
#   if defined(HAVE_VPRINTF)
    vprintf(fmt, ap);
#   else
    _doprnt(fmt, ap, stdout);
#   endif
    va_end(ap);
    fflush(stdout);
}

*****/

/*
 * Generate an error message for operations that have errno values.
 */
void p2p_syserror( string, value )
char *string;
int  value;
{
    perror( "Error detected by system routine: " );
    p2p_error( string, value );
}


void p2p_error(string,value)
char * string;
int value;
{
    printf("%s %d\n",string, value);
    /* printf("p2p_error is not fully cleaning up at present\n"); */
    p2p_cleanup();

#if !defined(MPI_cspp)
    /* Manually kill all processes */
    if (MPID_myid == 0) {
	int i;
	/* We are no longer interested in signals from the children */
	p2p_clear_signal();
	/* numprocs - 1 because the parent is not in the list */
	for (i=0; i<MPID_numprocs; i++) {
	    if (MPID_child_pid[i] > 0) 
		kill( MPID_child_pid[i], SIGINT );
	    }
	}
#endif
#ifdef FOO
    if (MPID_SHMEM_ppid) 
	kill( -MPID_SHMEM_ppid, SIGKILL );
#endif

#if defined(MPI_cspp)
    if (MPID_SHMEM_ppid && (cnx_exec == 0))
	kill( -MPID_SHMEM_ppid, SIGKILL );
#endif

    /* We need to do an abort to make sure that the children get killed */
    abort();
    /* exit(99); */
}
#include <sys/time.h>

void p2p_wtime_init( void )
{
}

double p2p_wtime()
{

#if defined(MPI_cspp) && defined(__USE_LONG_LONG)
#include <sys/cnx_ail.h>
return (toc_read() * ((double) 0.000001));
#elif defined(MPI_cspp)
/*
 * This is needed for SPP when HP compiler is used (i.e. no long long).
 */
	cnx_toc_t	toc;
	unsigned int	*ptoc;

	ptoc = (unsigned int *) &toc;
	toc = toc_read();
	return((ptoc[0] * ((double) 4294.967296)) +
			(ptoc[1] * ((double) 0.000001)));
#else
    double timeval;

#if defined(HAVE_BSDGETTIMEOFDAY)
    struct timeval tp;
    struct timezone tzp;

    BSDgettimeofday(&tp,&tzp);
#elif defined(USE_WIERDGETTIMEOFDAY)
    /* This is for Solaris, where they decided to change the CALLING
       SEQUENCE OF gettimeofday! (Solaris 2.3 and 2.4 only?) */
    struct timeval tp;

    gettimeofday(&tp);
#elif defined(HAVE_GETTIMEOFDAY)
    struct timeval tp;
    struct timezone tzp;

    gettimeofday(&tp,&tzp);
#else
    struct timeval tp;
    tp.tv_sec  = 0;
    tp.tv_usec = 0;
#endif

/* Some versions of Solaris need 1 argument, some need 2.  See the configure
   for the test for "Wierd" */

    timeval = (double) tp.tv_sec;
    timeval = timeval + (double) ((double) .000001 * (double) tp.tv_usec);

    return(timeval);
#endif
}

/*
   Yield to other processes (rather than spinning in place)
 */
#if defined(HAVE_SCHED_YIELD) && (defined(USE_DYNAMIC_YIELD) || defined(USE_SCHED_YIELD))
#include <sched.h>
#endif

#ifdef USE_DYNAMIC_YIELD
void p2p_yield( void )
{
    static int first_call = 1;
    static int kind = 1;

    if (first_call) {
	/* Get the yield style from the environment.  The default is 
	   sched_yield */
	char *name = getenv( "MPICH_YIELD" );
	if (name) {
	    if (strcmp( "sched_yield", name ) == 0) {
		kind = 1;
	    }
	    else if (strcmp( "select", name ) == 0) {
		kind = 2;
	    }
	    else if (strcmp( "none", name ) == 0) {
		kind = 0;
	    }
	}
	first_call = 0;
    }
    switch (kind) {
    case 0: return;
    case 1:
	sched_yield();
	break;
    case 2: 
    {
	struct timeval tp;
	/* fd_set readmask; */
	tp.tv_sec  = 0;
	tp.tv_usec = 0;
	/* FD_ZERO(&readmask);
	   FD_SET(1,&readmask); */
	/*select( 2, (void *)&readmask, (void *)0, (void *)0, &tp ); */
	select( 0, (void *)0, (void *)0, (void *)0, &tp );
    }
    break;
    }
}
#else
void p2p_yield( void )
{
#if defined(USE_SGINAP_YIELD)
    /* Multiprocessor IRIX machines may want to comment this out for lower
       latency */
    sginap(0);

#elif defined(USE_SCHED_YIELD)
    /* This is a POSIX function to yield the process */
    sched_yield();
#elif defined(USE_YIELD_YIELD)
    yield();
#elif defined(USE_SELECT_YIELD)
    /* Use a short select as a way to suggest to the OS to deschedule the 
       process.  Solaris select hangs if count is zero, so we check fd 1  
       This may not accomplish a process yield, depending on the OS.  
    */
    struct timeval tp;
    /*    fd_set readmask; */
    tp.tv_sec  = 0;
    tp.tv_usec = 0;
    /*FD_ZERO(&readmask);
      FD_SET(1,&readmask); */
    /*select( 2, (void *)&readmask, (void *)0, (void *)0, &tp ); */
    select( 0, (void *)0, (void *)0, (void *)0, &tp );
#endif
}
#endif

#if defined(USE_XX_SHMALLOC)
/* This is not machine dependent code but is only used on some machines */

/*
  Memory management routines from ANSI K&R C, modified to manage
  a single block of shared memory.
  Have stripped out all the usage monitoring to keep it simple.

  To initialize a piece of shared memory:
    xx_init_shmalloc(char *memory, unsigned nbytes)

  Then call xx_shmalloc() and xx_shfree() as usual.
*/

#ifdef MPID_CACHE_LINE_SIZE
#define ALIGNMENT (2*MPID_CACHE_LINE_SIZE)
#define LOG_ALIGN (MPID_CACHE_LINE_LOG_SIZE+1)
#else
#define LOG_ALIGN 6
#define ALIGNMENT (1 << LOG_ALIGN)
#endif
/* ALIGNMENT is assumed below to be bigger than sizeof(p2p_lock_t) +
   sizeof(Header *), so do not reduce LOG_ALIGN below 4 */

union header
{
    struct
    {
	union header *ptr;	/* next block if on free list */
	unsigned size;		/* size of this block */
    } s;
    char align[ALIGNMENT];	/* Align to ALIGNMENT byte boundary */
};

typedef union header Header;

static Header **freep;		/* pointer to pointer to start of free list
                                   *freep = NULL: shared memory is entirely
used */
static p2p_lock_t *p2p_shmem_lock;	/* Pointer to lock */

void xx_init_shmalloc(memory, nbytes)
char *memory;
unsigned nbytes;
/*
  memory points to a region of shared memory nbytes long.
  initialize the data structures needed to manage this memory
*/
{
    int nunits = nbytes >> LOG_ALIGN;
    Header *region = (Header *) memory;

#if defined(MPI_cspp)
    myshmem = memory;
    myshmemsize = nbytes;
#endif

    /* Quick check that things are OK */

    if (ALIGNMENT != sizeof(Header) ||
	ALIGNMENT < (sizeof(Header *) + sizeof(p2p_lock_t)))
    {
        p2p_dprintf("%d %d\n",sizeof(Header),sizeof(p2p_lock_t));
	p2p_error("xx_init_shmem: Alignment is wrong", ALIGNMENT);
    }

    if (!region)
	p2p_error("xx_init_shmem: Passed null pointer", 0);

    if (nunits < 2)
	p2p_error("xx_init_shmem: Initial region is ridiculously small",
		 (int) nbytes);

    /*
     * Shared memory region is structured as follows
     *
     * 1) (Header *) freep ... free list pointer 2) (p2p_lock_t) p2p_shmem_lock
...
     * space to hold lock 3) padding up to alignment boundary 4) First header
     * of free list
     */

    freep = (Header **) region;	/* Free space pointer in first block  */
#if defined(MPI_hpux)
    p2p_shmem_lock = (p2p_lock_t *) ((char *)freep + 16);/* aligned for HP  */
#else
    p2p_shmem_lock = (p2p_lock_t *) (freep + 1);/* Lock still in first block */
#endif
    (region + 1)->s.ptr = *freep = region + 1;	/* Data in rest */
    (region + 1)->s.size = nunits - 1;	/* One header consumed already */

#   ifdef USE_SEMOP
    p2p_lock_init(p2p_shmem_lock);
    /*
    p2p_shmem_lock->semid = sysv_semid0;
    p2p_shmem_lock->semnum = 0;
    */
#   else
    p2p_lock_init(p2p_shmem_lock);                /* Initialize the lock */
#   endif

}

void *xx_shmalloc(nbytes)
unsigned nbytes;
{
    Header *p, *prevp;
    char *address = (char *) NULL;
    unsigned nunits;

    /* Force entire routine to be single threaded */
    (void) p2p_lock(p2p_shmem_lock);

#if defined(MPI_hpux) || defined(USE_MSEM)
    /* Why? */
    nbytes += sizeof(MPID_msemaphore);
#endif

    if (*freep) {
        /* Look for free shared memory */

    nunits = ((nbytes + sizeof(Header) - 1) >> LOG_ALIGN) + 1;

    prevp = *freep;
    for (p = prevp->s.ptr;; prevp = p, p = p->s.ptr)
    {
	if (p->s.size >= nunits)
	{			/* Big enuf */
	    if (p->s.size == nunits)	/* exact fit */
            {
           	if (p == p->s.ptr)
                {
                   /* No more shared memory available */
                   prevp = (Header *) NULL;
              	}
               	else {
	  	   prevp->s.ptr = p->s.ptr;
             	}
            }
	    else
	    {			/* allocate tail end */
		p->s.size -= nunits;
		p += p->s.size;
		p->s.size = nunits;
	    }
	    *freep = prevp;
	    address = (char *) (p + 1);
	    break;
	}
	if (p == *freep)
	{			/* wrapped around the free list ... no fit
				 * found */
	    address = (char *) NULL;
	    break;
	}
    }
    }

    /* End critical region */
    (void) p2p_unlock(p2p_shmem_lock);

	    /*
    if (address == NULL)
	p2p_dprintf("xx_shmalloc: returning NULL; requested %d
bytes\n",nbytes);
	*/
    return address;
}

void xx_shfree(ap)
char *ap;
{
    Header *bp, *p;

    if (!ap)
	return;			/* Do nothing with NULL pointers */

    /* Begin critical region */

    (void) p2p_lock(p2p_shmem_lock);

    bp = (Header *) ap - 1;	/* Point to block header */

    if (*freep) {
         /* there are already free region(s) in the shared memory region */

    	for (p = *freep; !(bp > p && bp < p->s.ptr); p = p->s.ptr) {
	if (p >= p->s.ptr && (bp > p || bp < p->s.ptr))
	    break;		/* Freed block at start of end of arena */

    	}

        /* Integrate bp in list */

    	*freep = p;

    if (bp + bp->s.size == p->s.ptr)
    {				/* join to upper neighbour */
                if (p->s.ptr == *freep) *freep = bp;
                if (p->s.ptr == p) bp->s.ptr = bp;
                else               bp->s.ptr = p->s.ptr->s.ptr;

	bp->s.size += p->s.ptr->s.size;
    }
    else
	bp->s.ptr = p->s.ptr;

    if (p + p->s.size == bp)
    {				/* Join to lower neighbour */
	p->s.size += bp->s.size;
	p->s.ptr = bp->s.ptr;
    }
    else
	p->s.ptr = bp;

    }
    else {
        /* There wasn't a free shared memory region before */

       	bp->s.ptr = bp;

       	*freep = bp;
    }

    /* End critical region */
    (void) p2p_unlock(p2p_shmem_lock);
}

#endif
