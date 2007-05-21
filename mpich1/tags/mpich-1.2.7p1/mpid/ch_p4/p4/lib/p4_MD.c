#include "p4.h"
#include "p4_sys.h"

/* For the SYS_V shared memory functions, we need a unique segment id.
   Originally, this was done by using getpid+i, which wasn't perfect but
   was often good enough.  Later versions of the SYS_V routines support
   IPC_PRIVATE, which gives us a guaranteed unique id.  The following
   macro gives us backward portability while allowing us to use
   IPC_PRIVATE
 */
#ifdef IPC_PRIVATE
#define P4_SHM_GET_UNIQUE_ID(_i) IPC_PRIVATE
#else
#define P4_SHM_GET_UNIQUE_ID(_i) getpid()
#endif

/* --------- Most Machine Dependent Stuff is in this file */

#if defined(ALLIANT) && defined(USE_XX_SHMALLOC)
char *xx_shmalloc();
P4VOID xx_shfree();
P4VOID xx_init_shmalloc();
#endif

#if defined(KSR) && defined(USE_XX_SHMALLOC)
char *xx_shmalloc();
P4VOID xx_shfree();
P4VOID xx_init_shmalloc();
#endif

#ifdef SYSV_IPC
void p4_shmat_errmsg ( int );
void p4_shmat_errmsg( x )
int x;
{
    switch (errno) {
    case EACCES: 
	fprintf( stderr, 
"shmat called failed:\n\
This process is not allowed to create shared memory.n\
See your system administrator\n" );
		break;
		
    case EMFILE:
	fprintf( stderr, 
"shmat called failed:\n\
This process is not allowed to create any more shared memory regions\n\
See your system administrator\n");
	break;
    default:
	perror( "Reason " );
	break;
    }
    p4_error("OOPS: shmat failed ",x);
}
#endif

P4VOID MD_initmem(int memsize)
{
#ifdef TC_2000
    MD_malloc_hint(HEAP_INTERLEAVED | HEAP_UNCACHED, 0);
#endif

#if defined(GP_1000)
    xx_malloc(0, memsize);
#endif

#if defined(KSR) && defined(USE_XX_SHMALLOC)
#define  UNMAPPED  (caddr_t) -1
    caddr_t memory;
    unsigned size = ((memsize + 4095) / 4096) * 4096;

    /*   create shared memory  */
    memory = (char *) mmap( NULL, memsize, PROT_READ | PROT_WRITE,
                               MAP_ANONYMOUS | MAP_VARIABLE |MAP_SHARED,
                               -1,0);
    if ( memory == UNMAPPED)
    {
        p4_error("OOPS: mmap failed",memory);
    }
    xx_init_shmalloc(memory, size);
#endif

#if defined(ALLIANT) && !defined(USE_XX_SHMALLOC)
    xx_malloc(0, memsize);
#endif

#if defined(ALLIANT) && defined(USE_XX_SHMALLOC)
    unsigned size = ((memsize + 4095) / 4096) * 4096;
    char *memory = valloc(size);
    long id;
    if (!memory)
	p4_error("MD_initmem: failed in valloc", size);

    id = (long) getpid();	/* get a unique id */
    if (create_shared_region(id, (char *) memory, size, 0))
	p4_error("MD_init_mem: failed in create_shared_region", size);
    xx_init_shmalloc(memory, size);
#endif

#if defined(SYSV_IPC) && defined(USE_XX_SHMALLOC)
    int i,nsegs;
    unsigned size, segsize = P4_SYSV_SHM_SEGSIZE;
    char *mem, *tmem, *pmem;

    if (memsize  &&  (memsize % P4_SYSV_SHM_SEGSIZE) == 0)
	nsegs = memsize / segsize;
    else
	nsegs = memsize / segsize + 1;

    if (nsegs > P4_MAX_SYSV_SHMIDS) 
	p4_error( "exceeding max num of P4_MAX_SYSV_SHMIDS", P4_MAX_SYSV_SHMIDS );
	
    size = nsegs * segsize;
    /* Try first to get a single section of memeory.  If that doesn't work,
       try to piece it together */
    if ((sysv_shmid[0] = shmget(P4_SHM_GET_UNIQUE_ID(0),size,IPC_CREAT|0600)) >= 0) {
	if ((sysv_shmat[0] = mem = (char *)shmat(sysv_shmid[0],NULL,0)) == (char *)-1) {
	    p4_shmat_errmsg( (int)(sysv_shmid[0]) );
	}
	sysv_num_shmids++;
    }
    else {
	/* Piece it together */
	if ((sysv_shmid[0] = shmget(P4_SHM_GET_UNIQUE_ID(0),segsize,IPC_CREAT|0600)) == -1)
	{
	    p4_error("OOPS: shmget failed",sysv_shmid[0]);
	}
	if ((mem = (char *)shmat(sysv_shmid[0],NULL,0)) == (char *)-1)
	{
	    p4_shmat_errmsg( (int)(sysv_shmid[0]) );
	}
	sysv_shmat[0] = mem;
	sysv_num_shmids++;
	nsegs--;
	
	pmem = mem;
	for (i=1; i <= nsegs; i++)
	{
	    if ((sysv_shmid[i] = shmget(P4_SHM_GET_UNIQUE_ID(i),segsize,IPC_CREAT|0600)) == -1)
	    {
		p4_error("OOPS: shmget failed",sysv_shmid[i]);
	    }
	    if ((tmem = sysv_shmat[i] = 
		 (char *)shmat(sysv_shmid[i],pmem+segsize,0)) == (char *)-1)
	    {
		if ((tmem = sysv_shmat[i] = 
		   (char *)shmat(sysv_shmid[i],pmem-segsize,0)) == (char *)-1)
		{
		    p4_shmat_errmsg( i );
		}
		else
		{
		    mem = tmem;
		}
	    }
	    sysv_num_shmids++;
	    pmem = tmem;
	}
    }
    xx_init_shmalloc(mem,size);
#endif


#if defined(SGI)  &&  defined(VENDOR_IPC)
       
/*
 *   strcpy(p4_sgi_shared_arena_filename,"/tmp/shared_arena_");
 */


    strcpy(p4_sgi_shared_arena_filename,"/usr/tmp/p4_shared_arena_");	/* 7/12/95, bri@sgi.com */

    sprintf(&(p4_sgi_shared_arena_filename[strlen(p4_sgi_shared_arena_filename)]),"%d",getpid());

    if (usconfig(CONF_INITUSERS,P4_MAX_MSG_QUEUES) == -1)
    {
	p4_error("MD_initmem: usconfig failed for users: ",memsize);
    }
    if (usconfig(CONF_INITSIZE,memsize) == -1)
    {
	p4_error("MD_initmem: usconfig failed: cannot map shared arena",memsize);
    }
    p4_sgi_usptr = usinit(p4_sgi_shared_arena_filename);
    if (p4_sgi_usptr == NULL) {	/* try usinit several times */
	int ctr = 0;
	while ((ctr < 3) && (p4_sgi_usptr == NULL)) {
	    ctr++;
	    sleep(2);
	    p4_sgi_usptr = usinit(p4_sgi_shared_arena_filename);
	}
    }
    if (p4_sgi_usptr == NULL) 
	p4_error("MD_initmem: usinit failed: cannot map shared arena",
		 memsize);
#endif

#if defined(USE_XX_SHMALLOC) && defined(SUN_SOLARIS)
    {
	caddr_t p4_start_shared_area;
	static int p4_shared_map_fd;
    
	p4_shared_map_fd = open("/dev/zero", O_RDWR);

	p4_start_shared_area = (char *) mmap((caddr_t) 0, memsize,
					     PROT_READ|PROT_WRITE|PROT_EXEC,
					     MAP_SHARED, 
					     p4_shared_map_fd, (off_t) 0);
	if (p4_start_shared_area == (caddr_t)-1)
	{
	    p4_error("OOPS: mmap failed: cannot map shared memory",memsize);
	}
	xx_init_shmalloc(p4_start_shared_area,memsize);
    }
#endif

#if defined(MULTIMAX)
    share_malloc_init(memsize);
#endif
}

P4VOID MD_initenv( void )
{

    /* next 2 should stay together -> used in MD_clock 
    p4_global->reference_time = 0; 
    p4_global->reference_time = MD_clock();
    */
    MD_set_reference_time();

#if defined(FX2800_SWITCH)
    sw_attach(p4_global->application_id);
#endif

}				/* MD_initenv */

/* MD_malloc_hint variables.  Needs to be initialized appropriately
   for each system. */

#if defined(TC_2000)

static int characteristic = HEAP_INTERLEAVED | HEAP_UNCACHED,	/* "flag" arg to
								 * heapmalloc(3) */
  locality = HEAP_ANYWHERE;	/* "node" arg to heapmalloc(3) */
#endif

P4VOID MD_malloc_hint( int a, int b )
{
#if defined(TC_2000)
    characteristic = a;
    locality = b;
    if (a == -1)
	xx_malloc(2, b);	/* setting mapped filename */
    else if (a == -2)
	xx_malloc(3, 0);	/* doing heapsync() */
#endif
}

char *MD_shmalloc( int size )
{
    char *p;

#if defined(BALANCE) || defined(SYMMETRY) || defined(SYMMETRY_PTX)
    p = shmalloc(size);
#else

#if defined(GP_1000) || defined(TC_2000)
/*   size = (size | 0xff) + 1;	 $$$ Why?  Nuked for now...  -jb */
    p = xx_malloc(1, size);
#  ifdef MALLOC_STATS
      allocated += size;
#  endif
#else

#if defined(KSR) && defined(USE_XX_SHMALLOC)
    p = xx_shmalloc((unsigned) size);
#else

#if defined(ALLIANT) && defined(USE_XX_SHMALLOC)
    p = xx_shmalloc((unsigned) size);
#else
#if defined(ALLIANT) && !defined(USE_XX_SHMALLOC)
    p = xx_malloc(1, size);
#else

#if defined(MULTIMAX)
    p = share_malloc(size);
#else

#if defined(SYSV_IPC) && defined(USE_XX_SHMALLOC)
    p = xx_shmalloc((unsigned) size);

#else

#if defined(SUN_SOLARIS)  && defined(USE_XX_SHMALLOC)
    p = xx_shmalloc(size);

#else

#if defined(SGI)  &&  defined(VENDOR_IPC)
    p = usmalloc(size,p4_sgi_usptr);

#else
    p = (char *) p4_malloc(size);
#endif
#endif
#endif
#endif
#endif
#endif
#endif
#endif
#endif

    return (p);
}

P4VOID MD_shfree(char *ptr)
{

#if defined(BALANCE) || defined(SYMMETRY) || defined(SYMMETRY_PTX)
    shfree(ptr);
#else

#if defined(TC_2000)
    heapfree(ptr);
#else

#if defined(GP_1000)
    p4_dprintf("OOPS: MD_shfree not implemented on bfly1\n");
#else

#if defined(KSR) && defined(USE_XX_SHMALLOC)
    xx_shfree(ptr);
#else

#if defined(ALLIANT) && defined(USE_XX_SHMALLOC)
    xx_shfree(ptr);
#else
#if defined(ALLIANT) && !defined(USE_XX_SHMALLOC)
    p4_dprintf("OOPS: MD_shfree not yet implemented on alliant\n");
#else	/* ALLIANT */

#if defined(MULTIMAX)
    share_free(ptr);
#else

#if defined(SYSV_IPC) && defined(USE_XX_SHMALLOC)
    xx_shfree(ptr);
#else
#if defined(SUN_SOLARIS)  && defined(USE_XX_SHMALLOC)
    xx_shfree(ptr);
#else

#if defined(SGI)  && defined(VENDOR_IPC)
    usfree(ptr,p4_sgi_usptr);

#else
    p4_free(ptr);

#endif
#endif
#endif
#endif
#endif
#endif
#endif
#endif
#endif
#endif

}


#if defined(GP_1000)

P4BOOL simple_lock(int *lock)
{
    register short got_lock = 0;

    while(!got_lock) 
    {
	while(*lock != 0) /* wait till clear */
	    waitspin(7); /* wait 70 microsecs - atomic ops take about 60 */
	if (atomior32(lock, 1) == 0)
	    got_lock = 1;
    }
    return(P4_TRUE);
}

#define wait_factor 13.0                /* appx 10 microseconds */
P4VOID waitspin(n)
register int n;
{
    n = (int) ((double) n * wait_factor);      /* set wait time */
    while (n > 0)
        n--;
}

P4BOOL simple_unlock(lock)
int *lock;
{
    atomand32(lock, 0);
    return(P4_TRUE);
}

#endif


#if defined(TC_2000)

/*
 * simple_lock()
 * This function spins on a "semaphore" (lock location).
 * It spins until we get the requested semaphore.  
 */
P4BOOL simple_lock(lock)
int *lock;
{
    register short got_lock = 0;
    while (!got_lock)
    {
	while (*lock != 0)	/* wait till clear */
	    /* wait 70 microsecs - atomic ops take about 60 */
	    waitspin(7);
	/*
	 * someone else may grab lock before we can - be sure to check what
	 * the atomic op says WAS there
	 */
	if (xmemi(lock, 1) == 0)
	    got_lock = 1;
    }
    return (P4_TRUE);
}

#define wait_factor 13.0	/* appx 10 microseconds */
P4VOID waitspin(n)
register int n;
{
    n = (int) ((double) n * wait_factor);	/* set wait time */
    while (n > 0)
	n--;
}

/*
 * simple_unlock()
 * This function releases the designated semaphore in the set.
 * It is called after a process leaves its critical section.
 */
P4BOOL simple_unlock(lock)
int *lock;
{
    *lock = 0;
    return (P4_TRUE);
}

#endif


#if defined(ALLIANT) && !defined(USE_XX_SHMALLOC)
/*
 * xx_malloc is a memory allocation routine.  It is called in three ways: if typ
 * == 0, then n is the number of bytes to claim as globally-shared memory
 * (from which routines can ask for shared memory).  In this case the routine
 * returns the address of the allocated block (NULL, if an allocation failure
 * occurs).
 *
 * else if typ == 1, then n is taken to be the amount of shared memory
 * requested.  In this case, the routine returns the address of a block of at
 * least n charecters in length.
 *
 * else if typ == 2, then the routine is being asked to return the address of
 * the globally- shared block of memory.
 *
 * The view of shared memory supported by xx_malloc is that a single massive chunk
 * of memory is acquired and handed out by xx_malloc calls with 1 as first
 * argument.
 */

struct mem_blk
{
    char *next;
    int l_mem;
    MD_lock_t MEM;
    int pad;			/* pad out to 8-byte boundary */
};

char *xx_malloc(typ, n)
int typ, n;
{
    static struct mem_blk *glob_mem = (struct mem_blk *) NULL;
    static int l_mem = 0;
    char *rc;
    int i;
    char *c;
    /* alliant stuff */
    long id;
    /* end alliant stuff */

    switch (typ)
    {
      case 0:			/* initialize */
	/* pad & malloc */

	/* round to mult of pg size before allocing on page boundary */
	n = ((n + 4095) / 4096) * 4096;

	if ((glob_mem = (struct mem_blk *) valloc((long) n)) == NULL)
	    p4_error("xx_malloc: failed in valloc", n);

	id = (long) getpid();	/* get a unique id */

	if (create_shared_region(id, (char *) glob_mem, (long) n, 0))
	    p4_error("xx_malloc: failed in create_shared_region", n);

	glob_mem->next = (char *) (glob_mem + sizeof(struct mem_blk));
	glob_mem->l_mem = n;
	rc = glob_mem->next;
	MD_lock_init(&glob_mem->MEM);
	break;

      case 1:
	i = (n + 7) & (~007);
	MD_lock(&glob_mem->MEM);
	if (glob_mem->l_mem < i)
	{
	    p4_dprintf("*** global allocation failure ***\n");
	    p4_dprintf("*** attempted %d bytes, %d left\n",
		       i, glob_mem->l_mem);
	    rc = NULL;
	    MD_unlock(&glob_mem->MEM);
	    p4_error("xx_malloc: global alloc failed", i);
	}
	else
	{
	    rc = glob_mem->next;
	    glob_mem->next += i;
	    glob_mem->l_mem -= i;
	    /*
	     * B printf("allocated %d bytes of shared memory\n",i);
	     * printf("at %x\n",rc); E
	     */
	}
	MD_unlock(&glob_mem->MEM);
	break;

      case 2:
	rc = (char *) glob_mem;
	break;

      default:
	printf("*** illegal call to xx_malloc *** typ=%d\n", typ);
    }
    return (rc);
}

#endif	/* ALLIANT */

#ifdef GP_1000
/*
 * xx_malloc is a memory allocation routine.  It is called in three ways: if typ
 * == 0, then n is the number of bytes to claim as globally-shared memory
 * (from which routines can ask for shared memory).  In this case the routine
 * returns the address of the allocated block (NULL, if an allocation failure
 * occurs).
 *
 * else if typ == 1, then n is taken to be the amount of shared memory
 * requested.  In this case, the routine returns the address of a block of at
 * least n charecters in length.
 *
 * else if typ == 2, then the routine is being asked to return the address of
 * the globally- shared block of memory.
 *
 * The view of shared memory supported by xx_malloc is that a single massive chunk
 * of memory is acquired and handed out by xx_malloc calls with 1 as first
 * argument.
 */

struct mem_blk
{
    char *next;
    int l_mem;
    MD_lock_t MEM;
    int pad;			/* pad out to 8-byte boundary */
};

char *xx_malloc(typ, n)
int typ, n;
{
    static struct mem_blk *glob_mem = (struct mem_blk *) NULL;
    static int l_mem = 0;
    char *rc;
    int i;
    char *c;
    /* bbn stuff */
#define SHMEM_BASE 0x401000
    vm_address_t shmem_seg;
    union cluster_status cl_stat;
    int clus_size;
    int blk_cnt, ok;
    /* end bbn stuff */

    switch (typ)
    {
      case 0:			/* initialize */
	/* pad & malloc */

	/*
	 * printf("clus_size = %d, &clus_size = %d\n",clus_size,&clus_size);
	 */
	cluster_stat(HOME_CLUSTER, GET_NODE_LIST, &cl_stat, &clus_size);
	/*
	 * printf("clus_size = %d, &clus_size = %d\n",clus_size,&clus_size);
	 */

	blk_cnt = ((n / clus_size) / vm_page_size) + 1;

	/* printf("n = %d,page size = %d  ",n,vm_page_size); */
	/*
	 * printf("clus_size = %d, blk_cnt = %d\n", clus_size,blk_cnt);
	 */

	ok = 1;
	for (i = 0; (i < clus_size) && ok; i++)
	{
	    shmem_seg = SHMEM_BASE + (vm_page_size * blk_cnt) * i;
	    if (vm_allocate_and_bind(task_self(), &shmem_seg, blk_cnt * vm_page_size,
				     P4_FALSE, i) != KERN_SUCCESS)
	    {
		printf("vm_allocate_and_bind failed\n");
		ok = 0;
	    }
	    else if (vm_inherit(task_self(), shmem_seg, blk_cnt * vm_page_size,
				VM_INHERIT_SHARE) != KERN_SUCCESS)
	    {
		printf("vm_inherit failed\n");
		ok = 0;
	    }
	}

	if (ok)
	{			/* everything succeeded */
	    glob_mem = (struct mem_blk *) SHMEM_BASE;
	    glob_mem->next = (char *) (SHMEM_BASE + sizeof(struct mem_blk));
	    glob_mem->l_mem = n;
	    rc = glob_mem->next;
	    MD_lock_init(&glob_mem->MEM);
	}
	break;

      case 1:
	i = (n + 7) & (~007);
	MD_lock(&glob_mem->MEM);
	if (glob_mem->l_mem < i)
	{
	    p4_dprintf("*** global allocation failure ***\n");
	    p4_dprintf("*** attempted %d bytes, %d left\n",
		       i, glob_mem->l_mem);
	    rc = NULL;
	    MD_unlock(&glob_mem->MEM);
	    p4_error("xx_malloc: global alloc failed", i);
	}
	else
	{
	    rc = glob_mem->next;
	    glob_mem->next += i;
	    glob_mem->l_mem -= i;
	    /* printf("allocated %d bytes of shared memory at %x\n",i,rc); */
	}
	MD_unlock(&glob_mem->MEM);
	break;

      case 2:
	rc = (char *) glob_mem;
	break;

      default:
	printf("*** illegal call to xx_malloc *** typ=%d\n", typ);
    }
    return (rc);
}

#endif

#if defined(TC_2000)

/*
 * xx_malloc is a memory allocation routine.  It is called in two ways:
 * if typ == 1, then n is taken to be the amount of shared memory
 * requested.  In this case, the routine returns the address of a block of at
 * least n charecters in length.  We round up to cache-line size, since we
 * may have specified cache attributes....
 *
 * else if typ == 2, then n is interpreted as a character pointer, pointing to
 * a string containing a filename that is used for rendesvous as a
 * memory-mapped-file, thus allowing unrelated processes to allocate shared
 * memory with each other.
 *
 * else if typ == 3, then heapsync() is called to help processes avoid doing
 * shared memory map-ins at reference time.
 *
 */

char *xx_malloc(typ, n)
int typ, n;
{
    char *mem_ptr, *mapped_filename;
    int alloc_chunk;

    switch (typ)
    {

      case 1:
	/* pad to multiple of cache-line size */
	alloc_chunk = (n + 17) & (~017);
	/*
	 * B printf("allocated %d bytes of shared memory\n",i); printf("at
	 * %x\n",mem_ptr); E
	 */
	/* gag - refer to our global at the top of the file */
	if ((mem_ptr = heapmalloc(characteristic, locality, alloc_chunk)) == NULL)
	    p4_dprintf("*** global allocation failure - general ***\n");
	/* reset hints to "defaults" */
	characteristic = HEAP_INTERLEAVED;
	locality = HEAP_ANYWHERE;	/* $$$ should be HEAP_SCATTERED */
	break;

      case 2:
	heapfile((char *) n);
	break;

      case 3:
	heapsync();
	break;

      default:
	printf("*** illegal call to xx_malloc *** typ=%d\n", typ);
    }
    return (mem_ptr);
}
#endif


#if defined(IPSC860)

struct p4_msg *MD_i860_recv()
/* Low level ipsc 860 message receive routine.
 *
 * All messages should be of the p4_msg type.
 *
 * Blocks until it receives a message.
 *
 * If the type is "ACK_REQUEST", then send back and "ACK_REPLY", to
 * confirm that the message was received.  If the message is of this
 * type, than it is sent in the struct tmsg form, to encapsulate the
 * user's chosen type of message, which gets ignored in this routine.
 */
{
    long type;
    int proc = NODE_PID, node, alloc_size, msg_size;
    struct p4_msg *m;		/* WARNING: deallocate above  */
    char ack = 'a';

    /*
     * Probe to see how big the incoming message is.  Block until that
     * message comes in.  Allocate it, and receive it.  All the p4_msg
     * information should automatically, since the message is a p4_msg type.
     */
    p4_dprintfl(20, "receiving a msg via i860 crecv\n");
    cprobe(ANY_P4TYPE_IPSC);
    alloc_size = (int) infocount();
    type = (int) infotype();
    node = (int) infonode();
    msg_size = alloc_size - sizeof(struct p4_msg) + sizeof(char *);
    m = alloc_p4_msg(msg_size);
    crecv(ANY_P4TYPE_IPSC, (char *) m, (long) alloc_size);
    p4_dprintfl(10, "received msg via i860 crecv from=%d type=%d \n",m->from,m->type);

    /*
     * If the type is "ACK_REQUEST", fire off the reply. If sender was the
     * host, send it to the procid of the host.
     */
    if (type == ACK_REQUEST_IPSC)
    {
	p4_dprintfl(30, "sending ack to %d\n", m->from);
	csend(ACK_REPLY_IPSC, &ack, sizeof(char), node, proc);
	p4_dprintfl(30, "sent ack to %d\n", m->from);
    }
    return (m);
}


int MD_i860_send(m)
struct p4_msg *m;
/*
 * Send the message, nonblocking, no wait for acknowledgement of receipt.
 */
{
    int proc = NODE_PID, to;
    char buf;			/* buffer for the ack message */
    int len;

    to = p4_local->conntab[m->to].port;

    p4_dprintfl(20, "sending msg of type %d from %d to %d via i860 send\n",m->type,m->from,m->to);
    len = m->len + sizeof(struct p4_msg) - sizeof(char *);
    if (!(m->ack_req & P4_ACK_REQ_MASK))
    {
	m->msg_id = (int) isend((long) NO_TYPE_IPSC, m, (long) len, (long) to, (long) proc);
	(p4_global->cube_msgs_out)++;
	p4_dprintfl(10, "sent msg of type %d from %d to %d via i860 isend\n",m->type,m->from,m->to);
    }
    else
    {
	/* Send a message, asking for an acknowledgement of receipt. */
	csend((long) ACK_REQUEST_IPSC, m, (long) len, (long) to, (long) proc);
	m->msg_id = -1;		/* already waited for by csend */
	/* Wait for the acknowledgement. */
	p4_dprintfl(30, "waiting for ack from %d\n", m->to);
	crecv((long) ACK_REPLY_IPSC, &buf, (long) sizeof(char));
	p4_dprintfl(30, "received ack from %d\n", m->to);
	p4_dprintfl(10, "sent msg of type %d from %d to %d via i860 csend\n",m->type,m->from,m->to);
    }
}

P4BOOL MD_i860_msgs_available()
{
    P4BOOL rc;

    rc = (P4BOOL) iprobe(ANY_P4TYPE_IPSC);
    return (rc);
}


/* endif for ipsc860 */
#endif


#if defined(SP1_EUI)

MD_eui_send(m)
struct p4_msg *m;
{
    int nbytes;
    char ack_msg;
    int send_len,acklen = sizeof(char);
    int ack_reply_type = ACK_REPLY_EUI;

    p4_dprintfl(20,"sending to %d via eui\n",m->to);
    send_len = m->len+sizeof(struct p4_msg)-sizeof(char *);
    mpc_bsend(m,send_len,m->to,m->type);
    p4_dprintfl(10,"sent msg to %d via eui\n",m->to);
    if (m->ack_req & P4_ACK_REQ_MASK)
    {
        acklen = sizeof(char);
	mpc_brecv(&ack_msg,acklen,&m->to,&ack_reply_type,&nbytes);
    }
}

struct p4_msg *MD_eui_recv()
{
    int nbytes,from,type,acklen,acktype,msg_size,alloc_size;
    struct p4_msg hdr, *m;
    char ack_msg;

    from = ANY_P4TYPE_EUI;
    type = ANY_P4TYPE_EUI;

    mpc_probe(&from,&type,&alloc_size);
    msg_size = alloc_size - sizeof(struct p4_msg) + sizeof(char *);
    m = alloc_p4_msg(msg_size);
    mpc_brecv(m,alloc_size,&from,&type,&nbytes);
    if (m->ack_req & P4_ACK_REQ_MASK)
    {
	acklen = sizeof(char);
        acktype = ACK_REPLY_EUI;
	mpc_bsend(&ack_msg,acklen,m->from,ACK_REPLY_EUI);
    }
    return(m);
}

MD_eui_msgs_available()
{
    int numbytes;
    int from = ANY_P4TYPE_EUI;
    int type = ANY_P4TYPE_EUI;

    mpc_probe(&from,&type,&numbytes);
    if (numbytes == -1)
	return P4_FALSE;
    else
	return P4_TRUE;
}

/* end of include for EUI */
#endif

#if defined(SP1_EUIH)

MD_euih_send(m)
struct p4_msg *m;
{
    int nbytes;
    char ack_msg;
    int send_len,acklen = sizeof(char);
    int ack_reply_type = ACK_REPLY_EUIH;

    p4_dprintfl(20,"sending to %d via euih\n",m->to);
    send_len = m->len+sizeof(struct p4_msg)-sizeof(char *);
    mp_bsend(m,&send_len,&(m->to),&(m->type));
    p4_dprintfl(10,"sent msg to %d via euih\n",m->to);
    if (m->ack_req & P4_ACK_REQ_MASK)
    {
	acklen = sizeof(char);
	mp_brecv(&ack_msg,&acklen,&(m->to),&ack_reply_type,&nbytes);
    }
}

struct p4_msg *MD_euih_recv()
{
    int nbytes,from,type,acklen,acktype,msg_size,alloc_size;
    struct p4_msg hdr, *m;
    char ack_msg;

    from = ANY_P4TYPE_EUIH;
    type = ANY_P4TYPE_EUIH;

    mp_probe(&from,&type,&alloc_size);
    msg_size = alloc_size - sizeof(struct p4_msg) + sizeof(char *);
    m = alloc_p4_msg(msg_size);
    mp_brecv(m,&alloc_size,&from,&type,&nbytes);
    if (m->ack_req & P4_ACK_REQ_MASK)
    {
	acklen = sizeof(char);
	acktype = ACK_REPLY_EUIH;
	mp_bsend(&ack_msg,&acklen,&(m->from),&acktype);
    }
    return(m);
}

MD_euih_msgs_available()
{
    int numbytes;
    int from = ANY_P4TYPE_EUIH;
    int type = ANY_P4TYPE_EUIH;

    mp_probe(&from,&type,&numbytes);
    if (numbytes == -1)
	return P4_FALSE;
    else
	return P4_TRUE;
}

/* end of include for SP1_EUIH */
#endif

#if defined(CM5)

struct p4_msg *MD_CM5_recv()
/* Low level CM-5 message receive routine.
 *
 * All messages should be of the p4_msg type.
 *
 * Blocks until it receives a message.
 *
 * If the type is "ACK_REQUEST", then send back and "ACK_REPLY", to
 * confirm that the message was received.  If the message is of this
 * type, than it is sent in the struct tmsg form, to encapsulate the
 * user's chosen type of message, which gets ignored in this routine.
 */
{
    int type, node, alloc_size, msg_size;
    struct p4_msg *m;		/* WARNING: deallocate above  */
    char ack = 'a';

    /*
     * Probe to see how big the incoming message is.  Block until that
     * message comes in.  Allocate it, and receive it.  All the p4_msg
     * information should automatically, since the message is a p4_msg type.
     */
    p4_dprintfl(20, "receiving a msg via cm-5 recv\n");
    CMMD_msg_pending(CMMD_ANY_NODE, CMMD_ANY_TAG);
    alloc_size = CMMD_bytes_received();
    type = CMMD_msg_tag();
    node = CMMD_msg_sender();
    msg_size = alloc_size - sizeof(struct p4_msg) + sizeof(char *);
    m = alloc_p4_msg(msg_size);
    CMMD_receive(node, type, (void *) m, alloc_size);
    p4_dprintfl(10, "received msg via cm-5 recv from=%d type=%d \n",m->from,m->type);

    if (type == ACK_REQUEST_CM5)
    {
	p4_dprintfl(30, "sending ack to %d\n", m->from);
	CMMD_send_noblock(m->from,ACK_REPLY_CM5,&ack,sizeof(char));
	p4_dprintfl(30, "sent ack to %d\n", m->from);
    }
    return (m);
}


int MD_CM5_send(m)
struct p4_msg *m;
{
    int to,len;
    char buf;			/* buffer for the ack message */

    to = p4_local->conntab[m->to].port;

    p4_dprintfl(20, "sending msg of type %d from %d to %d via cm5 send\n",m->type,m->from,m->to);
    len = m->len + sizeof(struct p4_msg) - sizeof(char *);
    if (!(m->ack_req & P4_ACK_REQ_MASK))
    {
	CMMD_send_noblock(to, NO_TYPE_CM5, (void *) m, len);
	p4_dprintfl(10, "sent msg of type %d from %d to %d via cm5 send\n",m->type,m->from,m->to);
    }
    else
    {
	/* Send a message, asking for an acknowledgement of receipt. */
	CMMD_send_noblock(to, ACK_REQUEST_CM5, (void *) m, len);
	/* Wait for the acknowledgement. */
	p4_dprintfl(30, "waiting for ack from %d\n", m->to);
        CMMD_receive(to, ACK_REPLY_CM5, (void *) &buf, sizeof(char));
	p4_dprintfl(30, "received ack from %d\n", m->to);
	p4_dprintfl(10, "sent msg of type %d from %d to %d via cm5 csend\n",m->type,m->from,m->to);
    }
}

P4BOOL MD_CM5_msgs_available()
{
    P4BOOL rc;

    rc = CMMD_msg_pending(CMMD_ANY_NODE,CMMD_ANY_TAG);
    return (rc);
}

/* endif for cm5 */
#endif


#if defined(NCUBE)

struct p4_msg *MD_NCUBE_recv()
/* Low level NCUBE message receive routine.
 *
 * All messages should be of the p4_msg type.
 *
 * Blocks until it receives a message.
 *
 * If the type is "ACK_REQUEST", then send back and "ACK_REPLY", to
 * confirm that the message was received.  If the message is of this
 * type, than it is sent in the struct tmsg form, to encapsulate the
 * user's chosen type of message, which gets ignored in this routine.
 */
{
    int type, node, alloc_size, msg_size, unused_flag;
    struct p4_msg *m;		/* WARNING: deallocate above  */
    char ack = 'a';

    /*
     * Probe to see how big the incoming message is.  Block until that
     * message comes in.  Allocate it, and receive it.  All the p4_msg
     * information should automatically, since the message is a p4_msg type.
     */
    p4_dprintfl(20, "receiving a msg via ncube recv\n");
    node = NCUBE_ANY_NODE;
    type = NCUBE_ANY_TAG;
    alloc_size = -1;
    while (alloc_size < 0)
    {
        alloc_size = ntest(&node,&type);
    }
    msg_size = alloc_size - sizeof(struct p4_msg) + sizeof(char *);
    m = alloc_p4_msg(msg_size);
    nread(m, alloc_size, &node,  &type, &unused_flag);

    p4_dprintfl(10, "received msg via ncube recv from=%d type=%d \n",m->from,m->type);

    if (type == ACK_REQUEST_NCUBE)
    {
	p4_dprintfl(30, "sending ack to %d\n", m->from);
	nwrite(&ack, sizeof(char), m->from, ACK_REPLY_NCUBE, &unused_flag);
	p4_dprintfl(30, "sent ack to %d\n", m->from);
    }
    return (m);
}


int MD_NCUBE_send(m)
struct p4_msg *m;
{
    int rc,to,len,type,unused_flag;
    char buf;			/* buffer for the ack message */

    if (m->to == 0xffff)  /* NCUBE broadcast */
        to = 0xffff;
    else
	to = p4_local->conntab[m->to].port;

    p4_dprintfl(20, "sending msg of type %d from %d to %d via NCUBE send\n",
		m->type,m->from,m->to);
    len = m->len + sizeof(struct p4_msg) - sizeof(char *);
    if (!(m->ack_req & P4_ACK_REQ_MASK))
    {
	rc = nwrite(m, len, to, NO_TYPE_NCUBE, &unused_flag);
        if (rc < 0)
        {
            p4_dprintf("nwrite failed for msg of length %d from %d to %d \n",
                        len,m->from,m->to);
            p4_error("exiting due to failed nwrite",rc);
        }
	p4_dprintfl(10, "sent msg of type %d from %d to %d via NCUBE send\n",
		    m->type,m->from,m->to);
    }
    else
    {
	/* Send a message, asking for an acknowledgement of receipt. */
	rc = nwrite(m, len, to, ACK_REQUEST_NCUBE, &unused_flag);
        if (rc < 0)
        {
            p4_dprintf("nwrite failed for msg of length %d from %d to %d \n",
                        len,m->from,m->to);
            p4_error("exiting due to failed nwrite",rc);
        }
	/* Wait for the acknowledgement. */
	p4_dprintfl(30, "waiting for ack from %d\n", m->to);
        type = ACK_REPLY_NCUBE;
        nread(&buf, sizeof(char), &to,  &type, &unused_flag);
	p4_dprintfl(30, "received ack from %d\n", m->to);
	p4_dprintfl(10, "sent msg of type %d from %d to %d via NCUBE csend\n",m->type,m->from,m->to);
    }
}

P4BOOL MD_NCUBE_msgs_available()
{
    P4BOOL rc;
    int from, type;

    from = NCUBE_ANY_NODE;
    type = NCUBE_ANY_TAG;
    rc = ntest(&from, &type);
    if (rc == -1)
        rc = 0;
    return (rc);
}

/* endif for ncube */
#endif


#if defined(IPSC860)  ||  defined(CM5)  || defined(NCUBE) \
                      ||  defined(SP1_EUI) || defined(SP1_EUIH)

int ns_start(argc, argv)
int *argc;
char **argv;
{
    char *s;
    char ns_host[100];
    struct bm_rm_msg bm_msg;
    int from, type, len, to, unused_flag;

    sprintf(whoami_p4, "ns_%d_%d", MYNODE(), getpid());

#   if defined(IPSC860)
    crecv(SYNC_MSG, &bm_msg, (long) sizeof(struct bm_rm_msg));
#   endif 
#   if defined(CM5)
    CMMD_receive(CMMD_ANY_NODE, CMMD_ANY_TAG, (void *) &bm_msg, sizeof(struct bm_rm_msg));
    if (CMMD_msg_tag() != SYNC_MSG) /* should be a DIE, message, otherwise */
        exit(0);
#   endif 
#   if defined(NCUBE)
    from = NCUBE_ANY_NODE;
    type = NCUBE_ANY_TAG;
    nread(&bm_msg, sizeof(struct bm_rm_msg), &from,  &type, &unused_flag);
    if (type != SYNC_MSG)  /* should be a DIE, message, otherwise */
        exit(0);
#   endif 
#   if defined(SP1_EUI)
    from = ANY_P4TYPE_EUI;
    type = ANY_P4TYPE_EUI;
    mpc_brecv(&bm_msg, sizeof(struct bm_rm_msg), &from,  &type, &unused_flag);
    if (type != SYNC_MSG)  /* should be a DIE, message, otherwise */
        exit(0);
#   endif 
#   if defined(SP1_EUIH)
    from = ANY_P4TYPE_EUIH;
    type = ANY_P4TYPE_EUIH;
    len  = sizeof(struct bm_rm_msg);
    mp_brecv(&bm_msg, &len, &from,  &type, &unused_flag);
    if (type != SYNC_MSG)  /* should be a DIE, message, otherwise */
        exit(0);
#   endif 

    /* Send off my info to my rm for forwarding to bm */
    bm_msg.type = p4_i_to_n(REMOTE_SLAVE_INFO);
    bm_msg.slave_idx = p4_i_to_n(MYNODE());
    bm_msg.slave_pid = p4_i_to_n(getpid());
    bm_msg.switch_port = p4_i_to_n(-1);
    ns_host[0] = '\0';
    get_qualified_hostname(ns_host,100);
    strcpy(bm_msg.host_name,ns_host);

#   if defined(IPSC860)
    csend((long) INITIAL_INFO, &bm_msg, (long) sizeof(struct bm_rm_msg), (long) 0, (long) NODE_PID);
    crecv(INITIAL_INFO, &bm_msg, (long) sizeof(struct bm_rm_msg));
#   endif 
#   if defined(CM5)
    CMMD_send_noblock(0, INITIAL_INFO, &bm_msg, sizeof(struct bm_rm_msg));
    CMMD_receive(CMMD_ANY_NODE, INITIAL_INFO, (void *) &bm_msg, sizeof(struct bm_rm_msg));
#   endif 
#   if defined(NCUBE)
    nwrite(&bm_msg, sizeof(struct bm_rm_msg), 0, INITIAL_INFO, &unused_flag);
    from = NCUBE_ANY_NODE;
    type = NCUBE_ANY_TAG;
    nread(&bm_msg, sizeof(struct bm_rm_msg), &from,  &type, &unused_flag);
#   endif 
#   if defined(SP1_EUI)
    mpc_bsend(&bm_msg, sizeof(struct bm_rm_msg), 0, INITIAL_INFO);
    from = ANY_P4TYPE_EUI;
    type = ANY_P4TYPE_EUI;
    mpc_brecv(&bm_msg, sizeof(struct bm_rm_msg), &from,  &type, &unused_flag);
#   endif 
#   if defined(SP1_EUIH)
    type = INITIAL_INFO;
    to   = 0;
    len  = sizeof(struct bm_rm_msg);
    mp_bsend(&bm_msg, &len, &to, &type);
    from = ANY_P4TYPE_EUIH;
    type = ANY_P4TYPE_EUIH;
    len  = sizeof(struct bm_rm_msg);
    mp_brecv(&bm_msg, &len, &from,  &type, &unused_flag);
#   endif 

    if (strcmp(bm_msg.version,P4_PATCHLEVEL) != 0)
    {
	p4_dprintf("my version is %s\n",P4_PATCHLEVEL);
	p4_error("version does not match master",0);
    }
    if ((s = (char *) rindex(bm_msg.pgm,'/'))  !=  NULL)
    {
	*s = '\0';  /* chg to directory name only */
	chdir(bm_msg.pgm);
    }
    globmemsize = p4_n_to_i(bm_msg.memsize);
    logging_flag = p4_n_to_i(bm_msg.logging_flag);
    if (logging_flag)
	ALOG_ENABLE;
    else
	ALOG_DISABLE;

    MD_initmem(globmemsize);
    alloc_global();  /* sets p4_global */
    p4_local = alloc_local_rm();
    p4_global->num_in_proctable = p4_n_to_i(bm_msg.numinproctab);
    p4_global->local_slave_count = p4_n_to_i(bm_msg.numslaves);
    p4_debug_level = p4_n_to_i(bm_msg.debug_level);
    strcpy(p4_global->application_id, bm_msg.application_id);

#   if defined(IPSC860)
    crecv(INITIAL_INFO, p4_global->proctable, (long) sizeof(p4_global->proctable));
#   endif 
#   if defined(CM5)
    CMMD_receive(CMMD_ANY_NODE, INITIAL_INFO, (void *) p4_global->proctable, sizeof(p4_global->proctable));
#   endif 
#   if defined(NCUBE)
    from = NCUBE_ANY_NODE;
    type = INITIAL_INFO;
    nread((void *) p4_global->proctable, sizeof(p4_global->proctable), &from,  &type, &unused_flag);
#   endif 
#   if defined(SP1_EUI)
    from = ANY_P4TYPE_EUI;
    type = INITIAL_INFO;
    mpc_brecv(p4_global->proctable, sizeof(p4_global->proctable), &from,  &type, &unused_flag);
#   endif 
#   if defined(SP1_EUIH)
    from = ANY_P4TYPE_EUIH;
    type = INITIAL_INFO;
    len  = sizeof(p4_global->proctable);
    mp_brecv(p4_global->proctable, &len, &from, &type, &unused_flag);
#   endif 

    p4_local = alloc_local_slave();
    p4_local->listener_fd = -1;
    p4_local->my_id = p4_get_my_id_from_proc();
    sprintf(whoami_p4, "p%d_%d", p4_get_my_id(), getpid());

    setup_conntab();

    usc_init();
    init_usclock();
    ALOG_SETUP(p4_local->my_id,ALOG_TRUNCATE);
    ALOG_LOG(p4_local->my_id,BEGIN_USER,0,"");
    return(0);
}

/* endif for ifdef ipsc860 or cm5 */
#endif 

P4VOID MD_set_reference_time( void )
{
/* We want MD_clock to deal with small numbers */

#if defined(SYMMETRY_PTX)
/* reference time will be in seconds */
    struct timespec tp;
    getclock(TIMEOFDAY,&tp);
    p4_global->reference_time = tp.tv_sec;
#endif

#if defined(SUN)   || defined(RS6000)     || defined(DEC5000) \
 || defined(NEXT)  || defined(KSR)        || defined(CM5)     \
 || defined(SYMMETRY) || defined(BALANCE) || defined(LINUX)   \
 || defined(GP_1000)  || defined(TC_2000) || defined(CRAY)    \
 || defined(TITAN)    || defined(ALLIANT) || defined(SGI)     \
 || defined(NCUBE)    || defined(SP1_EUI) || defined(SP1_EUIH)\
 || defined(MULTIMAX) || defined(IBM3090) || defined(FREEBSD) \
 || defined(NETBSD) \
 || (defined(HP)  &&  !defined(SUN_SOLARIS))

/* reference time will be in seconds */
    struct timeval tp;
    struct timezone tzp;

    gettimeofday(&tp, &tzp);
    p4_global->reference_time = tp.tv_sec;
#endif

#if defined(SUN_SOLARIS)
#if defined(USE_WIERDGETTIMEOFDAY)
    struct timeval tp;

    gettimeofday(&tp);
#else
    struct timeval tp;

    gettimeofday(&tp,(void *)0);
#endif
    p4_global->reference_time = tp.tv_sec;
#endif

#if defined(IPSC860)  &&  !defined(MEIKO_CS2)
/* reference time will be in milliseconds */
    p4_global->reference_time = (unsigned long) (mclock());
#endif
    
}

int MD_clock( void )
{
    /* returns value in milleseconds */
    int i = 0;

#if defined(SYMMETRY_PTX)
    struct timespec tp;
    getclock(TIMEOFDAY,&tp);
    i = (int) (tp.tv_sec - p4_global->reference_time);
    i *= 1000;
    i += (int) (tp.tv_nsec / 1000000); /* On PTX the second field is nanosec */
#endif

#if defined(SUN)   || defined(RS6000)     || defined(DEC5000) \
 || defined(NEXT)  || defined(KSR)        || defined(CM5)     \
 || defined(SYMMETRY) || defined(BALANCE) || defined(LINUX)   \
 || defined(GP_1000)  || defined(TC_2000) || defined(CRAY)    \
 || defined(TITAN)    || defined(ALLIANT) || defined(SGI)     \
 || defined(NCUBE)    || defined(SP1_EUI) || defined(SP1_EUIH)\
 || defined(MULTIMAX) || defined(IBM3090) || defined(FREEBSD) \
 || defined(NETBSD) \
 || (defined(HP)  &&  !defined(SUN_SOLARIS))

    struct timeval tp;
    struct timezone tzp;

    gettimeofday(&tp, &tzp);
    i = (int) (tp.tv_sec - p4_global->reference_time);
    i *= 1000;
    i += (int) (tp.tv_usec / 1000);
#endif

#if defined(SUN_SOLARIS)
    struct timeval tp;
#if defined(USE_WIERDGETTIMEOFDAY)
    gettimeofday(&tp);
#else
    gettimeofday(&tp,(void *)0);
#endif
    i = (int) (tp.tv_sec - p4_global->reference_time);
    i *= 1000;
    i += (int) (tp.tv_usec / 1000);

#endif

#if defined(IPSC860)  &&  !defined(MEIKO_CS2)
    i = (int) (mclock() - p4_global->reference_time);
#endif

    return (i);
}

#ifdef DELTA
int myhost()
{
    return (0);
}
#endif


#ifdef SYSV_IPC

int init_sysv_semset(int setnum)
{
    int i, semid;
#   if defined(SUN_SOLARIS)
    union semun{
      int val;
      struct semid_ds *buf;
      ushort *array;
      } arg;
#   else
#    if defined(SEMCTL_ARG_UNION)
#       if !defined(SEMUN_UNDEFINED)
       union semun arg;
#       else
       union semun {
	   int val;
	   struct semid_ds *buf;
	   unsigned short int *array;
	   struct seminfo *__buf;
       } arg;
#       endif /* SEMUN_UNDEFINED */
#   else
#   if defined(IBM3090) || defined(RS6000) ||    \
       defined(TITAN)  || defined(DEC5000) ||    \
       defined(HP) || defined(KSR)  
    int arg;
#   else
#   if defined(SEMUN_UNDEFINED)    
       union semun {
	   int val;
	   struct semid_ds *buf;
	   unsigned short int *array;
	   struct seminfo *__buf;
       } arg;
#   else
       union semun arg;
#      endif
#   endif
#endif
#endif

#   if defined(SUN_SOLARIS) || defined(SEMCTL_ARG_UNION)
    arg.val = 1;
#   else
#   if defined(IBM3090) || defined(RS6000) ||    \
       defined(TITAN)  || defined(DEC5000) ||    \
       defined(HP) || defined(KSR) 
    arg = 1;
#   else
    arg.val = 1;
#   endif
#   endif

    if ((semid = semget(P4_SHM_GET_UNIQUE_ID(setnum),10,IPC_CREAT|0600)) < 0)
    {
	p4_error("semget failed for setnum",setnum);
    }
    for (i=0; i < 10; i++)
    {
	if (semctl(semid,i,SETVAL,arg) == -1)
	{
	    p4_error("semctl setval failed",-1);
	}
    }
    return(semid);
}

P4VOID MD_lock_init(MD_lock_t *L)
{
int setnum;

    MD_lock(&(p4_global->slave_lock));
    setnum = p4_global->sysv_next_lock / 10;
    if (setnum > P4_MAX_SYSV_SEMIDS)
    {
	p4_error("exceeding max num of p4 semids",P4_MAX_SYSV_SEMIDS);
    }
    if (p4_global->sysv_next_lock % 10 == 0)
    {
	p4_global->sysv_semid[setnum] = init_sysv_semset(setnum);
	p4_global->sysv_num_semids++;
    }
    L->semid  = p4_global->sysv_semid[setnum];
    L->semnum = p4_global->sysv_next_lock - (setnum * 10);
    p4_global->sysv_next_lock++;
    MD_unlock(&(p4_global->slave_lock));
}


P4VOID MD_lock(MD_lock_t *L)
{
    sem_lock[0].sem_num = L->semnum;
    /* An EINTR is ok; other errors are not */
    while (semop(L->semid,&sem_lock[0],1) < 0)
    {
	/* Use -1 so that we get the value from perror for the call */
	if (errno != EINTR) {
	    p4_error("OOPS: semop lock failed",-1); /* (int)L->semid); */
	    break;   /* The break should be unnecessary, but just in case 
			p4_error fails to exit */
	}
    }
}

P4VOID MD_unlock(MD_lock_t *L)
{
    sem_unlock[0].sem_num = L->semnum;
    while (semop(L->semid,&sem_unlock[0],1) < 0)
    {
	if (errno != EINTR) {
	    p4_error("OOPS: semop unlock failed",(int)L->semid);
	    break;   /* The break should be unnecessary, but just in case 
			p4_error fails to exit */
	}
    }
}
#endif
 

#if defined(SGI)  &&  defined(VENDOR_IPC)

/* MD_lock and MD_unlock are defined in p4_MD.h for SGI */

/* this is the spinlock method */
P4VOID MD_lock_init(MD_lock_t *L) 
{ 
    (*L) = usnewlock(p4_sgi_usptr);
}

/* this is the semaphore method */
/**********
P4VOID MD_lock_init(MD_lock_t *L) 
{ 
    (*L) = usnewsema(p4_sgi_usptr,1); 
}

**********/
#endif


#if defined(TCMP)

P4BOOL MD_tcmp_msgs_available(req_type,req_from)
int *req_from,*req_type;
{
    char *msg = NULL;
    int len_rcvd;
    tcmpfunp matcher;
    tcmp_status tcmpstat;

    if (*req_type == -1)
	if (*req_from == -1)
	    matcher = TCMP_MATCH_ANY;
	else
	    matcher = TCMP_MATCH_SENDER;
    else if (*req_from == -1)
	matcher = TCMP_MATCH_TYPE;
    else
	matcher = TCMP_MATCH_BOTH;
    tcmpstat = tcmp_receive(matcher, req_from, req_type,
			    TCMP_NOBLOCK | TCMP_NOCOPY | TCMP_NODEQUEUE,
			    &len_rcvd, &msg);
    if (tcmpstat == TCMP_SUCCESS)
	return(P4_TRUE);
    else
	return(P4_FALSE);
}

int MD_tcmp_send(type, from, to, msg, len, data_type, ack_req)
int type, from, to, len, data_type, ack_req;
char *msg;
{
    int sendflags;
    tcmp_status tcmpstat;

    if (ack_req & P4_ACK_REQ_MASK)
	sendflags = 0;
    else
	sendflags = TCMP_NOBLOCK;
    tcmpstat = tcmp_send(to, type, sendflags, len, msg);
    if (tcmpstat != TCMP_SUCCESS)
	p4_error("bad status on tcmp_send = ",tcmpstat);
    return(0);
}

struct p4_msg *MD_tcmp_recv()
{
    int type, from, len;
    struct p4_msg *msg;
    tcmp_status tcmpstat;

    tcmpstat = tcmp_receive(TCMP_MATCH_ANY,&from,&type,TCMP_NOCOPY,&len,&msg);
    if (tcmpstat != TCMP_SUCCESS)
	p4_error("bad tcmp status on receive = ", tcmpstat);

    return (msg);
}

#endif

int data_representation( char *machine_type )
{
    if (strcmp(machine_type, "SUN") == 0)             return 1;
    if (strcmp(machine_type, "HP") == 0)              return 1;
    if (strcmp(machine_type, "RS6000") == 0)          return 1;
    if (strcmp(machine_type, "SGI") == 0)             return 1;
    if (strcmp(machine_type, "NEXT") == 0)            return 1;
    if (strcmp(machine_type, "CM5") == 0)             return 1;
    if (strcmp(machine_type, "SYMMETRY") == 0)        return 2;
    if (strcmp(machine_type, "SYMMETRY_PTX") == 0)    return 2;
    if (strcmp(machine_type, "SUN386I") == 0)         return 2;
#ifdef WORDS_BIGENDIAN
    /* These are ported to some non-IA32 system */
    if (strcmp(machine_type, "LINUX") == 0)           return 21;
    if (strcmp(machine_type, "FREEBSD") == 0)         return 22;
    if (strcmp(machine_type, "NETBSD") == 0)          return 23;
#else
    if (strcmp(machine_type, "LINUX") == 0)           return 2;
    if (strcmp(machine_type, "FREEBSD") == 0)         return 2;
    if (strcmp(machine_type, "NETBSD") == 0)          return 2;
#endif
    if (strcmp(machine_type, "I86_SOLARIS") == 0)     return 2;
    if (strcmp(machine_type, "DEC5000") == 0)         return 3;
    if (strcmp(machine_type, "IBM3090") == 0)         return 4;
    if (strcmp(machine_type, "TITAN") == 0)           return 5;
    if (strcmp(machine_type, "FX8") == 0)             return 6;
    if (strcmp(machine_type, "FX2800") == 0)          return 7;
    if (strcmp(machine_type, "FX2800_SWITCH") == 0)   return 7;
    if (strcmp(machine_type, "IPSC860") == 0)         return 8;
    if (strcmp(machine_type, "IPSC860_SOCKETS") == 0) return 8;
    if (strcmp(machine_type, "DELTA") == 0)           return 8;
    if (strcmp(machine_type, "BALANCE") == 0)         return 12;
    if (strcmp(machine_type, "MULTIMAX") == 0)        return 15;
    if (strcmp(machine_type, "CRAY") == 0)            return 16;
    if (strcmp(machine_type, "GP_1000") == 0)         return 17;
    if (strcmp(machine_type, "TC_2000") == 0)         return 18;
    if (strcmp(machine_type, "TC_2000_TCMP") == 0)    return 18;
    if (strcmp(machine_type, "KSR") == 0)             return 19;
    if (strcmp(machine_type, "NCUBE") == 0)           return 20;

    if (strcmp(machine_type, "LINUX_PPC") == 0)       return 24;
    if (strcmp(machine_type, "LINUX_ALPHA") == 0)     return 25;
    if (strcmp(machine_type, "FREEBSD_PPC") == 0)     return 26;
    p4_dprintf("invalid machine type=:%s:\n",machine_type);
    p4_error("data_representation: invalid machine type",0);
    return(-1);
}

#if defined(FREEBSD) && !defined(HAVE_XDR_FLOAT)
/* As of release 1.1, FreeBSD leaves out the xdr_float.c files from
   /usr/lib/libc.a.  This is the source code from FreeBSD 1.1.  This
   has been tested, and seems to work well on all but NaN's */
#include "xdr_float.c"
#endif


