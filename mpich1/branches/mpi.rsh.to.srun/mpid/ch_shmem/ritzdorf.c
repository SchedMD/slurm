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
