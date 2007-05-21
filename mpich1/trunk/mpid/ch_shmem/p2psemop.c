/* This is the SYSV_IPC from p4.  This is needed under SunOS 4.x, since 
   SunOS has no mutex/msem routines */

/* The shmat code is present because, under AIX 4.x, it
   was suggested that shmat would be faster than mmap */

/* To avoid problems with interrupts delivered while allocated shmat and
   semaphores, we explicitly check for EINTR and retry.  To do this, 
   we need errno.h */
#if defined(HAVE_ERRNO_H) && !defined(INCLUDED_ERRNO_H)
#define INCLUDED_ERRNO_H
#include <errno.h>
#endif

#ifndef P2_MAX_SYSV_SEMIDS 
#define P2_MAX_SYSV_SEMIDS 8
#endif
/* Information on the semaphores is in shared memory */
/* sysv_semid0 is the semaphore id used to manage the assignment of semaphore
   ids in p2p_lock_init, and is the one lock that must be allocated before
   p2p_lock_init is called.  

   A previous version used this same "lock" (semaphore id) to manage the
   xx_shmalloc critical section, thus reducing by one the use of scarce
   Sysv ipc's.  
 */

static int sysv_semid0 = -1;
struct p2_global_data {
    int sysv_num_semids;
    int sysv_semid[P2_MAX_SYSV_SEMIDS];
    int sysv_next_lock;
    p2p_lock_t slave_lock;
} *p2_global;

/* Definition of arg for semctl */
#ifdef FOO
#if defined(MPI_solaris) || defined(MPI_SX_4)
#define SEMCTL_ARG_UNION
union semun {
      int val;
      struct semid_ds *buf;
      ushort *array;
      } arg;
#elif defined(MPI_ibm3090) || defined(MPI_rs6000) ||    \
       defined(MPI_dec5000) ||    \
       defined(MPI_hpux) || defined(MPI_ksr) 
#define SEMCTL_ARG_INT
#elif defined(SEMUN_UNDEFINED)
       /* configure decided that union sumun was undefined */
       union semun {
	   int val;
	   struct semid_ds *buf;
	   unsigned short int *array;
	   struct seminfo *__buf;  /* We may want to comment this line out */
       } arg;
#else
/* A guess.  Configure should determine */
#define SEMCTL_ARG_UNION
#endif
#endif /* FOO */

/* If union semun is required, but not defined, define a value here */
#ifdef SEMCTL_ARG_UNION 
#ifdef SEMUN_UNDEFINED
union semun { int val; };
#endif
#endif

int MD_init_sysv_semset(int);
int MD_init_sysv_semset(int);

/*
  This must be called BEFORE using the shared memory allocator
 */
int MD_init_semop(void)
{
    sysv_semid0 = MD_init_sysv_semset(0);
    return 0;
}

/* This must be called AFTER MD_initmem but before anything else (like
   fork!)  Before MPI_initmem is called, we must initialize the sysv_semid0
   (used for shmat allocation).
 */
int MD_init_sysv_semop(void)
{
    /* Get shared memory.  Since this is called BEFORE any fork,
       we don't need to do any locks but we DO need to get the memory
       from a shared location 
     */
    p2_global = (struct p2_global_data *) 
	p2p_shmalloc( sizeof(struct p2_global_data) );
    if (!p2_global) 
	p2p_error("Could not get p2_global data\n",
		 sizeof(struct p2_global_data));
    p2_global->slave_lock.semid	 = sysv_semid0;
    p2_global->slave_lock.semnum = 1;
    p2_global->sysv_semid[0]	 = sysv_semid0;
    p2_global->sysv_num_semids	 = 1;
    p2_global->sysv_next_lock	 = 2; /* shmem_lock is 0 & slave_lock is 1 */

    return 0;
}

int MD_init_sysv_semset(int setnum)
{
    int i, semid;
#   if defined(SEMCTL_ARG_UNION)
    union semun arg;
    arg.val = 1;
#   else
    int arg = 1;
#   endif

    if ((semid = semget(getpid()+setnum,10,IPC_CREAT|0600)) < 0)
    {
	p2p_error("semget failed for setnum = ",setnum);
    }
    for (i=0; i < 10; i++)
    {
	if (semctl(semid,i,SETVAL,arg) == -1)
	{
	    p2p_error("semctl setval failed",-1);
	}
    }
    return(semid);
}

void p2p_lock_init(p2p_lock_t *L)
{
    int setnum;

    if (p2_global == 0) {
      /* This is a special case.  We are (better be!) in xx_shmalloc_init,
	 and we need to allocate the xx_shmalloc lock.  But we allocate
	 p2_global (global as in shared) with the shared memory allocator
	 (p2p_shmalloc) which may be ... xx_shmalloc!
	 To avoid this vicious cycle, we give semid0 as the lock in this
	 case 
	 */
      if (sysv_semid0 < 0) {
	p2p_error( "Invalid sysv semaphore!",sysv_semid0 );
      }
      L->semid  = sysv_semid0;
      L->semnum = 0;
      return;
    }

    p2p_lock(&(p2_global->slave_lock));
    setnum = p2_global->sysv_next_lock / 10;
    if (setnum > P2_MAX_SYSV_SEMIDS)
    {
	p2p_error("exceeding max num of p4 semids\n",P2_MAX_SYSV_SEMIDS);
    }
    if (p2_global->sysv_next_lock % 10 == 0)
    {
	p2_global->sysv_semid[setnum] = MD_init_sysv_semset(setnum);
	p2_global->sysv_num_semids++;
    }
    L->semid  = p2_global->sysv_semid[setnum];
    L->semnum = p2_global->sysv_next_lock - (setnum * 10);
    p2_global->sysv_next_lock++;
    p2p_unlock(&(p2_global->slave_lock));
}

/* Lock and unlock use the following structures */
static struct sembuf sem_lock[1]   = { { 0, -1, 0 } };
static struct sembuf sem_unlock[1] = { { 0, 1, 0 } };

void p2p_lock(p2p_lock_t *L)
{
    sem_lock[0].sem_num = L->semnum;
    while (semop(L->semid,&sem_lock[0],1) < 0)
    {
	if (errno != EINTR) {
	    p2p_error("OOPS: semop lock failed\n",L->semid);
	    break;   /* The break should be unnecessary, but just in case 
			p2p_error fails to exit */
	}
    }
}

void p2p_unlock( p2p_lock_t *L)
{
    sem_unlock[0].sem_num = L->semnum;
    while (semop(L->semid,&sem_unlock[0],1) < 0) 
    {
	if (errno != EINTR) {
	    p2p_error("OOPS: semop unlock failed\n",L->semid);
	    break;   /* The break should be unnecessary, but just in case 
			p4_error fails to exit */
	}
    }
}

void MD_remove_sysv_sipc( void )
{
    int i;
    struct p2_global_data *g = p2_global;
    /* Define a dummy argument value (some systems (e.g., LINUX Redhat)
       require the fourth argument to be declared and not 0) */
#   if defined(SEMCTL_ARG_UNION)
    union semun arg;
    arg.val = 0;
#   else
    int arg = 0;
#   endif

    /* ignore -1 return codes below due to multiple processes cleaning
       up the same sysv stuff; commented out "if" used to make sure
       that only the cluster master cleaned up in each cluster
    */

    if (g == NULL)
        return;
    for (i=0 /*1*/; i < g->sysv_num_semids; i++)  /* delete other sets */
    {
	semctl(g->sysv_semid[i],0,IPC_RMID,arg);
    }
}
