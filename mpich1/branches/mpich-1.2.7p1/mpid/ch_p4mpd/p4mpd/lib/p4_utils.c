#include "p4.h"
#include "p4_sys.h"

static char hold_patchlevel[16];
static char hold_machine_type[16];

/* getenv is part of stdlib.h */
#ifndef HAVE_STDLIB_H
extern char *getenv();
#endif

void p4_post_init()
{
    /* This routine can be called to do any further initialization after p4
       has itself been intialized. */

/* undef preconnect since we do not want to do it for mpd */
#undef P4_PRECONNECT 
#ifdef P4_PRECONNECT
    /* set up all sockets on systems with interrupt-unsafe socket calls */
    p4_dprintfl(10, "pre-establishing connections\n");
    p4_establish_all_conns();
#endif
}    

char *p4_version()
{
    strcpy(hold_patchlevel,P4_PATCHLEVEL);
    return(hold_patchlevel);
}

char *p4_machine_type()
{
    strcpy(hold_machine_type,P4_MACHINE_TYPE);
    return(hold_machine_type);
}


int p4_initenv(argc, argv)
int *argc;
char **argv;
{
    int i, rc;
    char *env_value;
    int p4_globmemsize;
    P4BOOL am_slave = P4_FALSE;

    sprintf(whoami_p4, "xm_%d", (int)getpid());

    p4_globmemsize = GLOBMEMSIZE;		/* 7/12/95, bri@sgi.com */
    env_value = getenv("P4_GLOBMEMSIZE");	/* 7/12/95, bri@sgi.com */
    if (env_value)				/* 7/12/95, bri@sgi.com */
	    p4_globmemsize = atoi(env_value);	/* 7/12/95, bri@sgi.com */
    globmemsize = p4_globmemsize;		/* 7/12/95, bri@sgi.com */

    logging_flag = P4_FALSE;
    process_args(argc,argv);

    for (i=0; i < *argc; i++)
    {
	if ((strcmp(argv[i], "-p4amslave") == 0) ||
	    (strcmp(argv[i], "-amp4slave") == 0) ||
            (strcmp(argv[i], "-p4amp4slave") == 0))
	{
	    strcpy(argv[i]," ");    
	    /* strip this arg as Prolog engine flags begin with - SRM */
	    am_slave = P4_TRUE;
	}
    }

    p4_local  = NULL;
    p4_global = NULL;

    /* Note that for mpd everyone is a master - RL  */
    if (am_slave)
    {
	rc = rm_start(argc, argv);
    }
    else
    {
	rc = bm_start(argc, argv);
        ALOG_MASTER(0,ALOG_TRUNCATE);
        ALOG_DEFINE(BEGIN_USER,"beg_user","");
        ALOG_DEFINE(END_USER,"end_user","");
        ALOG_DEFINE(BEGIN_SEND,"beg_send","");
        ALOG_DEFINE(END_SEND,"end_send","");
        ALOG_DEFINE(BEGIN_RECV,"beg_recv","");
        ALOG_DEFINE(END_RECV,"end_recv","");
        ALOG_DEFINE(BEGIN_WAIT,"beg_wait","");
        ALOG_DEFINE(END_WAIT,"end_wait","");
    }
    ALOG_LOG(p4_local->my_id,BEGIN_USER,0,"");
    return (rc);
}

char *p4_shmalloc(n)
int n;
{
    char *rc;

    if ((rc = MD_shmalloc(n)) == NULL)
	p4_dprintf("p4_shmalloc returning NULL; request = %d bytes\n\
You can increase the amount of memory by setting the environment variable\n\
P4_GLOBMEMSIZE (in bytes)\n",n);
    return (rc);
}

P4VOID p4_shfree(p)
P4VOID *p;
{
    MD_shfree(p);
}

int p4_num_cluster_ids()
{
    return (p4_global->local_slave_count + 1);
}

int p4_num_total_ids()
{
    return (p4_global->num_in_proctable);
}

int p4_num_total_slaves()
{
    return (p4_global->num_in_proctable - 1);
}


P4VOID p4_global_barrier(type)
int type;
{
    int dummy[1];

    dummy[0] = 0;
    p4_global_op(type, (char *) dummy, 1, sizeof(int), p4_int_sum_op, P4INT);
}


P4VOID p4_get_cluster_masters(numids, ids)
int *numids, ids[];
{
    int node;

    ids[0] = 0;
    *numids = 1;
    for (node = 1; node < p4_global->num_in_proctable; node++)
    {
	if (p4_global->proctable[node].slave_idx != 0)
	    continue;
	ids[(*numids)++] = node;
    }
}


P4VOID p4_get_cluster_ids(start, end)
int *start;
int *end;
{

    *start = p4_global->low_cluster_id;
    *end = p4_global->hi_cluster_id;
}


/* This is used to figure out the local id of the calling process by
 * indexing into the proctable until you find a hostname and a unix id
 * that are the same as yours.
 */
int p4_get_my_id_from_proc()
{
    int i, my_unix_id;
    int n_match, match_id;
    struct proc_info *pi;
    struct hostent *myhp, *pghp;
    struct in_addr myaddr;       /* 8/14/96, llewins@msmail4.hac.com */
    char myname[MAXHOSTNAMELEN]; /* 9/10/96, llewins@msmail4.hac.com */
    struct hostent myh;			/* 7/12/95, bri@sgi.com */

#   if (defined(IPSC860)  &&  !defined(IPSC860_SOCKETS))  ||  \
       (defined(CM5)      &&  !defined(CM5_SOCKETS))      ||  \
       (defined(NCUBE)    &&  !defined(NCUBE_SOCKETS))    ||  \
       (defined(SP1_EUI))                                 ||  \
       (defined(SP1_EUIH))
    return(MYNODE());
#   else
    my_unix_id = getpid();
    if (p4_local->my_id == LISTENER_ID)
	return (LISTENER_ID);

    /* gethostbyname returns a pointer to a shared (!) structure.  
       Rather than being very careful not to change it, we immediately
       copy the structure into myh; then get a pointer to that structure
       (just to simplify the code below).  This version was originally
       due to Ron Brightwell while at SGI (bri@sgi.com) 
     */
    myh = *gethostbyname_p4(p4_global->my_host_name);	/* 7/12/95, bri@sgi.com */
    myhp = &myh;					/* 7/12/95, bri@sgi.com */
    bcopy(myhp->h_addr, (char *)&myaddr, myhp->h_length); /* 8/14/96, llewins@msmail4.hac.com */
    strcpy(myname, myhp->h_name);                         /* 9/10/96, llewins@msmail4.hac.com */
    p4_dprintfl(60,"p4_get_my_id_from_proc: hostname = :%s:\n", myname);

/*    p4_dprintf( "proctable size is %d\n", p4_global->num_in_proctable ); */
    /*
     * The following code identifies the rank of the running process relative
     * to the procgroup file.  It does this by finding the pid of the process
     * in the proctable, and comparing it to the procid of the calling process.
     * A match isn't good enough, since some clusters could manage to 
     * co-incidentally assign the same process id to several of the processes
     * in the procgroup.  To check for this case, we compare the host name
     * in the procgroup to the hostname of the calling process.
     *
     * BUT what we really want to compare is an id of the physical hardware,
     * and since a machine might have multiple network interfaces, this test
     * might also fail.  So as a backstop, if there is precisely ONE match
     * to the process ids, we accept that as the UNIQUE solution.
     * 
     * Note that this code is trying to catch a procgroup file that is 
     * inconsistent with the build.  This should be caught in the procgroup
     * file reading routine instead.
     *
     * Remaining bugs: if there are multiple matches, we can't resolve which
     * we are if the hostname matching didn't find us (because of different
     * interfaces).
     *
     * To fix the test for a match, we need either to rethink this entire 
     * logic (don't figure out who we are this way) or place the value
     * of gethostbyname_p4 into the proctable that gets shipped around (
     * as part of the startup, the started process could ship that back 
     * with its pid).  This would guarantee that the correct name would 
     * be used, and could be different from the name used to establish 
     * connections. - WDG
     */
    n_match = 0;
    match_id = -1;
    for (pi = p4_global->proctable, i = 0; i < p4_global->num_in_proctable; i++, pi++)
    {
	p4_dprintfl(88, "pid %d ?= %d\n", pi->unix_id, my_unix_id );
	if (pi->unix_id == my_unix_id)
	{
	    /* Save match incase hostname test fails */
	    n_match++;
	    match_id = i;

            pghp = gethostbyname_p4(pi->host_name);
	    p4_dprintfl( 60, ":%s: ?= :%s:\n", pghp->h_name, myname );
	    /* We might have localhost.... and the same system.  
	       This test won't handle that.  
	     */
            if (strcmp(pghp->h_name, myname) == 0) /* 8/27/96, llewins@msmail4.hac.com */
            {
		p4_dprintfl(60,"get_my_id_from_proc: returning %d\n",i);
                return (i);
            }   
            /* all nodes on sp1 seem to have same inet address by this test.
*/
            /* After the fix above, the SP1 will probably be fine!! llewins */
#           if !defined(SP1)
	    else
	    {
/*		p4_dprintf( "Compare inet addresses %x ?= %x\n",
			    *(int *)&myaddr, *(int *)pghp->h_addr ); */
                if (bcmp((char *)&myaddr, pghp->h_addr, pghp->h_length) == 0) 
/* 8/14/96, llewins@msmail4.hac.com */
		{
		    return (i);
		}
	    }
#           endif
	}
    }

    /* See comments above on match algorithm */
    if (n_match == 1) return match_id;

    /* If we reach here, we did not find the process */
    p4_dprintf("process not in process table; my_unix_id = %d my_host=%s\n",
	       getpid(), p4_global->my_host_name);
    p4_dprintf("Probable cause:  local slave on uniprocessor without shared memory\n");
    p4_dprintf("Probable fix:  ensure only one process on %s\n",p4_global->my_host_name);
    p4_dprintf("(on master process this means 'local 0' in the procgroup file)\n");
    p4_dprintf("You can also remake p4 with SYSV_IPC set in the OPTIONS file\n");
    p4_dprintf( "Alternate cause:  Using localhost as a machine name in the progroup\n" );
    p4_dprintf( "file.  The names used should match the external network names.\n" );
    p4_error("p4_get_my_id_from_proc",0);
#   endif
    return (-2);
}

int p4_get_my_id()
{
    return (p4_local->my_id);
}

int p4_get_my_cluster_id()
{
#   if (defined(IPSC860)  &&  !defined(IPSC860_SOCKETS))  ||  \
       (defined(CM5)      &&  !defined(CM5_SOCKETS))      ||  \
       (defined(NCUBE)    &&  !defined(NCUBE_SOCKETS))    ||  \
       (defined(SP1_EUI))                                 ||  \
       (defined(SP1_EUIH))              
    return(MYNODE());
#   else
    if (p4_local->my_id == LISTENER_ID)
	return (LISTENER_ID);
    else
	return (p4_global->proctable[p4_local->my_id].slave_idx);
#   endif
}

P4BOOL p4_am_i_cluster_master()
{
    if (p4_local->my_id == LISTENER_ID)
	return (0);
    else
	return (p4_global->proctable[p4_local->my_id].slave_idx == 0);
}

P4BOOL in_same_cluster(i, j)
int i, j;
{
    return(P4_FALSE);		/* for MPD for now, no clusters */

/*    return (p4_global->proctable[i].group_id ==
	    p4_global->proctable[j].group_id);
	    */
}

P4VOID p4_cluster_shmem_sync(cluster_shmem)
P4VOID **cluster_shmem;
{
    int myid = p4_get_my_cluster_id();

    if (myid == 0)  /* cluster master */
	p4_global->cluster_shmem = *cluster_shmem;
    p4_barrier(&(p4_global->cluster_barrier),p4_num_cluster_ids());
    if (myid != 0)
	*cluster_shmem = p4_global->cluster_shmem;
}

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

/* IBM AIX 4.3.3 defined ALIGNMENT in sys/socket.h (!) */
#define LOG_ALIGN 6
#define P4_MEM_ALIGNMENT (1 << LOG_ALIGN)

/* P4_MEM_ALIGNMENT is assumed below to be bigger than sizeof(p4_lock_t) +
   sizeof(Header *), so do not reduce LOG_ALIGN below 4 */

union header
{
    struct
    {
	union header *ptr;	/* next block if on free list */
	unsigned size;		/* size of this block */
    } s;
    char align[P4_MEM_ALIGNMENT];   /* Align to ALIGNMENT byte boundary */
};

typedef union header Header;

static Header **freep;		/* pointer to pointer to start of free list */
static p4_lock_t *shmem_lock;	/* Pointer to lock */

P4VOID xx_init_shmalloc(memory, nbytes)
char *memory;
unsigned nbytes;
/*
  memory points to a region of shared memory nbytes long.
  initialize the data structures needed to manage this memory
*/
{
    int nunits = nbytes >> LOG_ALIGN;
    Header *region = (Header *) memory;

    /* Quick check that things are OK */

    if (P4_MEM_ALIGNMENT != sizeof(Header) ||
	P4_MEM_ALIGNMENT < (sizeof(Header *) + sizeof(p4_lock_t)))
    {
        p4_dprintfl(00,"%d %d\n",sizeof(Header),sizeof(p4_lock_t));
	p4_error("xx_init_shmem: Alignment is wrong", P4_MEM_ALIGNMENT);
    }

    if (!region)
	p4_error("xx_init_shmem: Passed null pointer", 0);

    if (nunits < 2)
	p4_error("xx_init_shmem: Initial region is ridiculously small",
		 (int) nbytes);

    /*
     * Shared memory region is structured as follows
     * 
     * 1) (Header *) freep ... free list pointer 2) (p4_lock_t) shmem_lock ...
     * space to hold lock 3) padding up to alignment boundary 4) First header
     * of free list
     */

    freep = (Header **) region;	/* Free space pointer in first block  */
    shmem_lock = (p4_lock_t *) (freep + 1);	/* Lock still in first block */
    (region + 1)->s.ptr = *freep = region + 1;	/* Data in rest */
    (region + 1)->s.size = nunits - 1;	/* One header consumed already */

#   ifdef SYSV_IPC
    shmem_lock->semid = sysv_semid0;
    shmem_lock->semnum = 0;
#   else
    p4_lock_init(shmem_lock);                /* Initialize the lock */
#   endif

}

char *xx_shmalloc(nbytes)
unsigned nbytes;
{
    Header *p, *prevp;
    char *address = (char *) NULL;
    unsigned nunits;

    /* Force entire routine to be single threaded */
    (P4VOID) p4_lock(shmem_lock);

    nunits = ((nbytes + sizeof(Header) - 1) >> LOG_ALIGN) + 1;

    prevp = *freep;
    for (p = prevp->s.ptr;; prevp = p, p = p->s.ptr)
    {
	if (p->s.size >= nunits)
	{			/* Big enuf */
	    if (p->s.size == nunits)	/* exact fit */
		prevp->s.ptr = p->s.ptr;
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

    /* End critical region */
    (P4VOID) p4_unlock(shmem_lock);

    if (address == NULL)
	p4_dprintf("xx_shmalloc: returning NULL; requested %d bytes\n",nbytes);
    return address;
}

P4VOID xx_shfree(ap)
char *ap;
{
    Header *bp, *p;

    /* Begin critical region */
    (P4VOID) p4_lock(shmem_lock);

    if (!ap)
	return;			/* Do nothing with NULL pointers */

    bp = (Header *) ap - 1;	/* Point to block header */

    for (p = *freep; !(bp > p && bp < p->s.ptr); p = p->s.ptr)
	if (p >= p->s.ptr && (bp > p || bp < p->s.ptr))
	    break;		/* Freed block at start of end of arena */

    if (bp + bp->s.size == p->s.ptr)
    {				/* join to upper neighbour */
	bp->s.size += p->s.ptr->s.size;
	bp->s.ptr = p->s.ptr->s.ptr;
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

    *freep = p;

    /* End critical region */
    (P4VOID) p4_unlock(shmem_lock);
}
#endif

P4VOID get_pipe(end_1, end_2)
int *end_1;
int *end_2;
{
    int p[2];

#   if defined(IPSC860)  ||  defined(CM5)  ||  defined(NCUBE)  ||  defined(SP1_EUI) || defined(SP1_EUIH)
    p4_dprintf("WARNING: get_pipe: socketpair assumed unavailable on this machine\n");
    return;
#   else
    if (socketpair(AF_UNIX, SOCK_STREAM, 0, p) < 0)
	p4_error("get_pipe: socketpair failed ", -1);
    *end_1 = p[0];
    *end_2 = p[1];
#   endif
}

P4VOID setup_conntab()
{
    int i, my_id;

    p4_dprintfl(60, "setup_conntab: myid=%d\n", p4_local->my_id);
    p4_local->conntab = (struct connection *)
	p4_malloc(P4_MAXPROCS * sizeof(struct connection));
    my_id = p4_get_my_id();

    for (i = 0; i < P4_MAXPROCS; i++)
    {
	if (i == my_id)
	    p4_local->conntab[i].type = CONN_ME;
	else if (in_same_cluster(i, my_id))
	    p4_local->conntab[i].type = CONN_LOCAL;
	else
	{
	    p4_local->conntab[i].type = CONN_REMOTE_NON_EST;
	    p4_local->conntab[i].port = -1;
	}
        p4_local->conntab[i].same_data_rep = P4_TRUE;
    }
    p4_dprintfl(60, "conntab after setup_conntab:\n");
    dump_conntab(60);
}

#ifdef SYSV_IPC
P4VOID remove_sysv_ipc()
{
    int i;
    struct p4_global_data *g = p4_global;

    /* ignore -1 return codes below due to multiple processes cleaning
       up the same sysv stuff; commented out "if" used to make sure
       that only the cluster master cleaned up in each cluster
    */
    /* if (p4_local != NULL  &&  p4_get_my_cluster_id() != 0) return; */

    if (sysv_shmid[0] == -1)
	return;
    for (i=0; i < sysv_num_shmids; i++) {
	/* Unmap the addresses - don't do this until you are done with
	   g, since g is inside of the memory */
	/* shmdt( sysv_shmat[i] ); */
	/* Remove the ids */
        shmctl(sysv_shmid[i],IPC_RMID,(struct shmid_ds *)0);
    }
    if (g == NULL)
        return;

    if (sysv_semid0 != -1)
	semctl(sysv_semid0,0,IPC_RMID,arg);  /* delete initial set */

    for (i=1; i < g->sysv_num_semids; i++)  /* delete other sets */
    {
	semctl(g->sysv_semid[i],0,IPC_RMID,arg);
    }
}
#endif

/* This routine is called if the wait fails to complete quickly */
#include <sys/time.h>
#ifndef TIMEOUT_VALUE_WAIT 
#define TIMEOUT_VALUE_WAIT 60
#endif
P4VOID p4_accept_wait_timeout (int);
P4VOID p4_accept_wait_timeout(sigval)
int sigval;
{
    fprintf( stderr, 
"Timeout in waiting for processes to exit.  This may be due to a defective\n\
rsh program (Some versions of Kerberos rsh have been observed to have this\n\
problem).\n\
This is not a problem with P4 or MPICH but a problem with the operating\n\
environment.  For many applications, this problem will only slow down\n\
process termination.\n" );

/* Why is p4_error commented out?  On some systems (like FreeBSD), we
   need to to kill the generated rsh processes.
 */

/*
p4_error( "Timeout in waiting for processes to exit.  This may be due to a defective rsh", 0 );
 */
/* exit(1); */
}

int p4_wait_for_end()
{
    int status;
    int i, n_forked_slaves, pid;
#ifndef THREAD_LISTENER
    struct slave_listener_msg msg;
#endif
#ifdef OLD_EXECER
    char job_filename[64];
#endif

    /* p4_socket_stat is a routine that conditionally prints information
       about the socket status.  -p4sctrl stat=y must be selected */
    p4_socket_stat( stdout );

    ALOG_LOG(p4_local->my_id,END_USER,0,"");
    ALOG_OUTPUT;

#   if defined(IPSC860)
    for (i=0; i < NUMAVAILS; i++)
    {
	struct p4_msg *mptr;
	mptr = p4_global->avail_buffs[i].buff;
	while (mptr)
	{
	    if ((mptr->msg_id != -1) && (!msgdone((long) mptr->msg_id)))
		msgwait((long) mptr->msg_id);
	    mptr = mptr->link;
	}
    }
#   endif

#   if defined(MEIKO_CS2)
    mpsc_fini();
#   endif

    if (p4_get_my_cluster_id() != 0)
	return (0);

    /* Free avail buffers */
    free_avail_buffs();

    /* Wait for all forked processes except listener to die */
    /* Some implementations of RSH can fail to terminate.  To work around
       this, we add a relatively short timeout (note that, at least in
       MPI programs, by the time we reach this point, everyone
       should have started to exit.

       The bug in those rsh version is in NOT using fd_set and the 
       macros for manipluating fd_set in the call to select.  These
       rsh's are assuming that fd's are all <= 31, and silently fail
       when they are not.
       */
    p4_dprintfl(90, "enter wait_for_end nfpid=%d\n",p4_global->n_forked_pids);
    SIGNAL_P4(SIGALRM,p4_accept_wait_timeout);
#ifndef CRAY
    {
    struct itimerval timelimit;
    struct timeval tval;
    struct timeval tzero;
    tval.tv_sec		  = TIMEOUT_VALUE_WAIT;
    tval.tv_usec	  = 0;
    tzero.tv_sec	  = 0;
    tzero.tv_usec	  = 0;
    timelimit.it_interval = tzero;       /* Only one alarm */
    timelimit.it_value	  = tval;
    setitimer( ITIMER_REAL, &timelimit, 0 );
#else
    alarm( TIMEOUT_VALUE_WAIT );
#endif
    /* Note that we are now in this routine (ignore some errors, such as
       failure to write on sockets as we are closing them) */
    p4_local->in_wait_for_exit = 1;

    if (p4_local->listener_fd == (-1))
        n_forked_slaves = p4_global->n_forked_pids;
    else
        n_forked_slaves = p4_global->n_forked_pids - 1;
    for (i = 0; i < n_forked_slaves; i++)
    {
	pid = wait(&status);
	if (pid < 0) {
	    p4_dprintfl( 90, "wait returned error (EINTR?)\n" );
	    break;
	    }
	p4_dprintfl(90, "detected that proc %d died \n", pid);
    }
#ifndef CRAY
    timelimit.it_value	  = tzero;   /* Turn off timer */
    setitimer( ITIMER_REAL, &timelimit, 0 );
    }
#else
    alarm( 0 );
#endif
    SIGNAL_P4(SIGALRM,SIG_DFL);

#   if defined(CAN_DO_SOCKET_MSGS)
    /* Tell all of the established connections that we are going away */
    for (i = 0; i < p4_global->num_in_proctable; i++)
    {
	if (p4_local->conntab[i].type == CONN_REMOTE_EST)
	{
	    /* Check the socket for any remaining messages, including socket 
	       close.  Resets connection type to closed if found */
	    p4_look_for_close( i );
	    /* If it is still open, send the close message */
	    if (p4_local->conntab[i].type == CONN_REMOTE_EST)
	    {
		socket_close_conn( p4_local->conntab[i].port );
		/* We could wait for the partner to close; but this should be
		   enough */
		p4_local->conntab[i].type = CONN_REMOTE_CLOSED; 
	    }
	}
    }
    /* Tell the listener to die and wait for him to do so (only if it is 
       a separate process) */
#ifndef THREAD_LISTENER
    if (p4_local->listener_fd != (-1))
    {
	p4_dprintfl(90, "tell listener to die listpid=%d fd=%d\n",
		    p4_global->listener_pid, p4_local->listener_fd);
	msg.type = p4_i_to_n(DIE);
	msg.from = p4_i_to_n(p4_get_my_id());
	net_send(p4_local->listener_fd, &msg, sizeof(msg), P4_FALSE); 
	/* Make sure that no further reads are possible for the LISTENER
	   on this FD */
	close( p4_local->listener_fd ); 
	/* This wait is potentially an infinite loop.  We can 
	   fix this with either an alarm (to terminate it) or by using
	   a nonblocking wait call and a loop.
	 */
	pid = wait(&status);
	p4_dprintfl(90, "detected that proc %d died \n", pid);

    }
#endif
    /* free listener data structures */
    

#   endif

#ifdef OLD_EXECER
    if (execer_starting_remotes  &&  execer_mynodenum == 0)
    {
	strcpy(job_filename,"/tmp/p4_");
	strcat(job_filename,execer_jobname);
	unlink(job_filename);
    }
#endif

    if (p4_get_my_id())
        p4_dprintfl(20,"process exiting\n");
    p4_dprintfl(90, "exit wait_for_end \n");

    /* free assorted data structures */
    if ( !p4_global->local_communication_only )
	p4_free(listener_info);	/* only allocated in this case */
    if (p4_local->procgroup) 
	p4_free(p4_local->procgroup);
    p4_free(p4_local->conntab);
    p4_shfree((P4VOID *)(p4_local->queued_messages->m.qs));
    p4_free(p4_local->queued_messages);
#ifdef CAN_DO_XDR
    p4_free(p4_local->xdr_buff);
#endif
    p4_free(p4_local);
    free_avail_quels();		/* (in p4_global)  */

    for (i = 0; i < P4_MAX_MSG_QUEUES; i++) 
	p4_shfree((P4VOID *)(p4_global->shmem_msg_queues[i].m.qs));
    p4_shfree((P4VOID *)(p4_global->cluster_barrier.m.qs));
    p4_shfree((P4VOID *)(p4_global));

#   if defined(SYSV_IPC)
    p4_dprintfl(90, "removing SYS V IPCs\n");
    remove_sysv_ipc();
#   endif

#   if defined(SGI)  &&  defined(VENDOR_IPC)
    unlink(p4_sgi_shared_arena_filename);
#   endif

    return (0);
}


/* static variables private to fork_p4 and zap_p4_processes */
static int n_pids = 0;
static int pid_list[P4_MAXPROCS];

int fork_p4()
/*
  Wrapper round fork for sole purpose of keeping track of pids so 
  that can signal error conditions.  See zap_p4_processes.
*/
{
    int pid;

#   if defined(IPSC860)  ||  defined(CM5)  ||  defined(NCUBE)  ||  defined(SP1_EUI) || defined(SP1_EIUH)
    p4_error("fork_p4: nodes cannot fork processes",0);    
#   else
    if (p4_global->n_forked_pids >= P4_MAXPROCS)
	p4_error("forking too many local processes; max = ", P4_MAXPROCS);
    p4_global->n_forked_pids++;

    fflush(stdout);
    pid = fork();

    if (pid > 0)
    {
	/* Parent process */
	pid_list[n_pids++] = pid;
#if defined(SUN_SOLARIS)
/*****	{ processorid_t proc = 0;
	  if(p_online(proc,P_STATUS) != P_ONLINE)
	    printf("Could not bind parent to processor 0\n");
	  else
	    {
	      processor_bind(P_PID,P_MYID,proc, &proc);
	      printf("Bound parent to processor 0 , previous binding was %d\n",
		     proc);
	    }
	}
*****/
#endif
    }
    else if (pid == 0)
    {
	/* Child process */
	pid_list[n_pids++] = getppid();
    }
    else
	p4_error("fork_p4: fork failed", pid);
#   endif

    return pid;
}

P4VOID zap_p4_processes()
{
    int n;
    
    if (p4_global == NULL)
        return;
    n = p4_global->n_forked_pids;
    while (n--)
    {
	p4_dprintfl(30, "killing local process pid %d (pidlist[%d]) of %d\n",
		   pid_list[n], n, p4_global->n_forked_pids );
	if ( pid_list[n] > 0 )
	    kill(pid_list[n], SIGINT);
    }
}

P4VOID zap_remote_p4_processes()
{
    int i;
    int my_id, num_tries;
    struct proc_info *dest_pi;
    char *dest_host;
    int dest_listener;
    int dest_listener_con_fd;
/*
    struct slave_listener_msg msg;
    int connection_fd;
    int new_listener_port, new_listener_fd;
 */

    p4_dprintfl(30,"killing remote processes\n");
    my_id = p4_get_my_id();

    for (i = 0; i < p4_global->num_in_proctable; i++) {
	if (i != my_id) {
	    dest_pi = get_proc_info(i);
	    dest_host = dest_pi->host_name;
	    dest_listener = dest_pi->port;
	    p4_dprintfl(30, "zap: my_id=%d dest_id=%d dest_host=%s dest_listener=%d\n",
			my_id, i, dest_host, dest_listener);

	    p4_dprintfl(30, "zap: enter loop to connect to dest listener %s\n",dest_host);
	    /* Connect to dest listener */
	    num_tries = 1;
	    p4_has_timedout( 0 );
	    while((dest_listener_con_fd = net_conn_to_listener(dest_host,dest_listener,1)) == -1) {
		num_tries++;
		if (p4_has_timedout( 1 )) {
		    p4_error( "Timeout in establishing connection to remote process", 0 );
		}
	    }
	    p4_dprintfl(30, "conn_to_proc_contd: connected after %d tries, dest_listener_con_fd=%d\n",
			num_tries, dest_listener_con_fd);
/*
            send it kill-clients-and-die message
*/
	}
    }
/*   
    kill own listener
*/
}

P4VOID get_qualified_hostname(char *str, int maxlen)
{
    str[maxlen-1] = 0;
#   if (defined(IPSC860)  &&  !defined(IPSC860_SOCKETS))  ||  \
       (defined(CM5)      &&  !defined(CM5_SOCKETS))      ||  \
       (defined(NCUBE)    &&  !defined(NCUBE_SOCKETS))    ||  \
       (defined(SP1_EUI))                                 ||  \
       (defined(SP1_EUIH))
    strncpy(str,"cube_node",maxlen-1);
#   else
#       if defined(SUN_SOLARIS) || defined(MEIKO_CS2)
        if (*str == '\0') {
            if (p4_global)
		strncpy(str,p4_global->my_host_name,maxlen-1);
	    else
		if (sysinfo(SI_HOSTNAME, str, maxlen-1) == -1)
		    p4_error("could not get qualified hostname", getpid());
	    }
#       else
        if (*str == '\0')
	{
            if (p4_global)
                strncpy(str,p4_global->my_host_name,maxlen-1);
            else
                gethostname_p4(str, maxlen);
	}
#       endif
    if (*local_domain != '\0'  &&  !index(str,'.'))
    {
	strncat(str,".",maxlen-1);
	strncat(str,local_domain,maxlen-1);
    }
#endif
}


int getswport(hostname)
char *hostname;
{
#ifdef CAN_DO_SWITCH_MSGS
    char local_host[MAXHOSTNAMELEN];

    if (strcmp(hostname, "local") == 0)
    {
	local_host[0] = '\0';
	get_qualified_hostname(local_host);
	return getswport(local_host);
    }
    if (strcmp(hostname, "hurley") == 0)
	return 1;
    if (strcmp(hostname, "hurley.tcg.anl.gov") == 0)
	return 1;
    if (strcmp(hostname, "hurley.mcs.anl.gov") == 0)
	return 1;
    if (strcmp(hostname, "campus.mcs.anl.gov") == 0)
	return 2;
    if (strcmp(hostname,"mpp1") == 0)
      return 3;
    if (strcmp(hostname,"mpp2") == 0)
      return 28;
    if (strcmp(hostname,"mpp3") == 0)
      return 6;
    if (strcmp(hostname,"mpp4") == 0)
      return 7;
    if (strcmp(hostname,"mpp7") == 0)
      return 14;
    if (strcmp(hostname,"mpp8") == 0)
      return 25;
    if (strcmp(hostname,"mpp9") == 0)
      return 20;
    if (strcmp(hostname,"mpp10") == 0)
      return 11;
#endif

    return -1;
}

P4BOOL same_data_representation(id1,id2)
int id1, id2;
{
    struct proc_info *p1 = &(p4_global->proctable[id1]);
    struct proc_info *p2 = &(p4_global->proctable[id2]);

    return (data_representation(p1->machine_type) == data_representation(p2->machine_type));
}

/* Given rank and places to put the hostname and image names, returns
 * the pid, and fills in the host and image names of the process with
 * the given rank.  Returns 0 if the rank is invalid.
 */
int p4_proc_info(i, hostname, exename)
int i;
char **hostname;
char **exename;
{
  if (((unsigned) i) >= p4_global->num_in_proctable)
    {
	*hostname = 0;
	return(0);
    }
    else
    {
	struct proc_info *p1 = &(p4_global->proctable[i]);
	*hostname = p1->host_name;
 
 	/* Get the executable name from the procgroup */
 	*exename = p4_local->procgroup->entries[i].slave_full_pathname;
 	return (p1->unix_id);
    }
}
	       
#ifdef OLD_EXECER
P4VOID put_execer_port(int port)
{
    int fd;
    char job_filename[64];
    char port_c[16];

    sprintf(port_c,"%d",port);
    strcpy(job_filename,"/tmp/p4_");
    strcat(job_filename,execer_jobname);
    if ((fd = open(job_filename, O_WRONLY | O_CREAT | O_TRUNC, 0600)) < 0)
    {
	p4_error("put_execer_port: open failed ",fd);
    }
    if ((write(fd,port_c,strlen(port_c)+1)) != strlen(port_c)+1)
    {
	p4_error("put_execer_port: write failed ",(-1));
    }
    close(fd);
}

int get_execer_port( char *master_hostname)
{
    int port, num_read, sleep_time, status;
    FILE *fp;
    char cmd[P4_MAX_PGM_LEN];

    sprintf(cmd,"rsh %s cat /tmp/p4_%s",master_hostname,execer_jobname);
    num_read = 0;
    sleep_time = 4;
    while (num_read != 1  &&  sleep_time < 128)
    {
        if ((fp = (FILE *) popen(cmd,"r")) == NULL)
        {
	    wait(&status);  /* for the rsh started by popen */
            sleep(sleep_time);
	    sleep_time *= 2;
        }
	else
	{
	    num_read = fscanf(fp,"%d",&port);
	    pclose(fp);
	}
    }

    if (num_read != 1)
    {
	p4_error("get_execer_port: never got good port",(-1));
    }

    return(port);
}
void p4_clean_execer_port( void )
{
    char job_filename[64];
    if (execer_starting_remotes  &&  execer_mynodenum == 0)
    {
	strncpy(job_filename,"/tmp/p4_",64);
	strncat(job_filename,execer_jobname,64);
	unlink(job_filename);
    }
}
#else
void put_execer_port( int port )
{
}
void p4_clean_execer_port( void )
{
}
#endif

/* high-resolution clock, made out of p4_clock and p4_ustimer */

static int clock_start_ms;
static usc_time_t ustimer_start;
static usc_time_t usrollover;

P4VOID init_usclock()
{
    clock_start_ms = p4_clock();
    ustimer_start  = p4_ustimer();
    usrollover     = usc_rollover_val();
}

double p4_usclock()
{
    int elapsed_ms, q;
    usc_time_t ustimer_end;
    double rc, roll, beginning, end;

    if (usrollover == 0)
	return( .001*p4_clock() );

    elapsed_ms = p4_clock() - clock_start_ms; /* milliseconds */
    ustimer_end = p4_ustimer();               /* terminal segment */

    q  =  elapsed_ms / (int)(usrollover/1000);/* num rollover-sized intervals*/
    /* q+1 is the maximum number of rollovers that could have occurred */

    if (ustimer_start <= ustimer_end)
      q = q - 1;
    /* now q+1 is the number of rollovers that did occur */

    beginning = (double)(usrollover - ustimer_start); /* initial segment */
    end = ustimer_end;                               /* terminal segment */

    roll = (double)(usrollover * 0.000001);           /* rollover in seconds */
    rc = (double) (((beginning + end ) * 0.000001) + (q * roll));

    return(rc);
}

#ifndef p4_CheckSighandler
P4VOID p4_CheckSighandler( sigf )
int (*sigf)();
{
    if (sigf != SIG_IGN && sigf != SIG_DFL && sigf != SIG_ERR) {
	printf( "Replaced a non-default signal in P4\n" );
    }
}
#endif






