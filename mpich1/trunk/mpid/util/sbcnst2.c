/*
 *  $Id: sbcnst2.c,v 1.9 2003/03/18 17:17:40 gropp Exp $
 *
 *  (C) 1993 by Argonne National Laboratory and Mississipi State University.
 *      See COPYRIGHT in top-level directory.
 */

#if defined(HAVE_MPICHCONF_H) && !defined(MPICHCONF_INC)
/* This includes the definitions found by configure, and can be found in
   the library directory (lib/$ARCH/$COMM) corresponding to this configuration
 */
#define MPICHCONF_INC
#include "mpichconf.h"
#endif

#include <stdio.h>
#define _SBCNSTDEF
#include "sbcnst2.h"

#ifdef NEEDS_STDLIB_PROTOTYPES
#include "protofix.h"
#endif

/* If you change this, you must change the format spec (%lx) to match */
typedef long PointerInt;

/* Define DEBUG and DEBUG1 to track problems */
/* DEBUG adds sentinals around the blocks and checks for overwrites.  It 
   is always safe to use, but reduces performance */
/* #define DEBUG */
/* DEBUG1 looks for out-of-range pointers.  However, it is only an 
   approximation and can give false positives.  *NEVER* leave this set; always
   remove the definition when you are done debugging */
/* #define DEBUG1 */

/* Needed for MPI_Aint (int that is the size of void *) */
#include "mpi.h"

#ifdef DEBUG
/* Needed for MPI_COMM_WORLD and MPI_ERR_xxx */
#define MPID_ERROR(comm,err,str) fprintf(stderr,str);exit(err)
#endif

/*
   This file contains routines for allocating a number of fixed-sized blocks.
   This is often a good way to improve the performance of dynamic-memory
   codes, at the expense of some additional space.  However, unlike a purely
   static allocation (a fixed maximum), this mechanism allows space to grow.

   The basic interface is

  sb = MPID_SBinit( blocksize, initialnumber, incrementnumber );
  ptr = MPID_SBalloc( sb );
  ...
  MPID_SBfree( sb, ptr );
  ...
  MPID_SBdestroy( sb );

  Still needed is an interface that helps track down blocks allocated
  but not freed.  Code like in tr2.c for keeping track of allocation
  lines and files, or special routine entrance exit code ala PETSc 
  could be used.
 */

#if defined(MPIR_DEBUG_MEM) || defined(MPIR_MEMDEBUG)
#undef MPID_SBinit
#undef MPID_SBalloc
#undef MPID_SBfree
#undef MPID_SBdestroy
#endif

/* Thread definitions, if not present */
#ifndef MPID_THREAD_DS_LOCK_DECLARE
#define MPID_THREAD_DS_LOCK_DECLARE
#define MPID_THREAD_DS_LOCK_INIT(a)
#define MPID_THREAD_DS_LOCK(a)
#define MPID_THREAD_DS_UNLOCK(a)
#endif

/*
    In case of errors, we return null.  The calling code must be
    prepared for that.
 */

/* This is the allocation unit. */
typedef struct _sbialloc {
    struct _sbialloc *next;
    int              nbytes, nballoc;
    int              nbinuse;
    } MPID_SBiAlloc;

/* Blocks are linked together; they are (much) larger than this */
#ifdef DEBUG
typedef struct {
    long sentinal_1;
    char *next;
    long sentinal_2;
    } MPID_SBblock;
#else
typedef struct {
    char *next;
    } MPID_SBblock;
#endif

/* Context for fixed-block allocator */
struct _MPID_SBHeader {
    MPID_THREAD_DS_LOCK_DECLARE  /* Lock variable for thread locking */
    MPID_SBiAlloc *blocks;	 /* allocated storage */
    MPID_SBblock  *avail;        /* fixed blocks (of size sizeb) to provide */
    int     nbfree, nballoc,     /* blocks free and in use */
            sizeb,               /* sizes in bytes */
            sizeincr;            /* # of blocks to allocate when more needed */
    };

void MPID_SBiAllocate ( MPID_SBHeader, int, int );

MPID_SBHeader MPID_SBinit( 
	int bsize, 
	int nb, 
	int nbincr )
{
MPID_SBHeader head;

/* Make sure that the blocksizes are multiples of pointer size */
if (bsize < sizeof(MPID_SBblock)) bsize = sizeof(MPID_SBblock);

head           = NEW(struct _MPID_SBHeader);  
if (!head) {
/*    MPID_ERROR( MPI_COMM_WORLD, MPI_ERR_EXHAUSTED, "Not enough space" ); */
   return 0;
   }
MPID_THREAD_DS_LOCK_INIT(head)
head->nbfree   = 0;
head->nballoc  = 0;
head->sizeb    = bsize;
head->sizeincr = nbincr;
head->avail    = 0;
head->blocks   = 0;
if (nb > 0) {
    MPID_SBiAllocate( head, bsize, nb );
    if (!head->avail) {
	/* MPID_ERROR( MPI_COMM_WORLD, MPI_ERR_EXHAUSTED, 
		    "Failed to allocate space" ); */
	head = 0;
	}
    }

return head;
}

/* 
    MPID_SBfree - return a fixed-sized block to the allocator

    This just adds to the headers free list.
 */    
void MPID_SBfree( 
	MPID_SBHeader sb, 
	void          *ptr)
{
    MPID_THREAD_DS_LOCK(sb)
#ifdef DEBUG1
	/* This test is only valid on systems that don't use the top bit 
	   for some addressing purpose.  HPUX 11, for example, has user
	   addresses with the top bit set */
    if ((MPI_Aint)ptr < 1024) {
	printf( "Suspicious pointer %lx in MPID_SBfree\n", (PointerInt)ptr );
	/* MPID_ERROR( MPI_COMM_WORLD, MPI_ERR_OTHER,  
		    "suspicious pointer in MPID_SBfree" ); */
    }
#endif
    ((MPID_SBblock *)ptr)->next = (char *)(sb->avail);
    sb->avail              = (MPID_SBblock *)ptr;
#ifdef DEBUG
    ((MPID_SBblock *)ptr)->sentinal_1 = 0xdeadbeef;
    ((MPID_SBblock *)ptr)->sentinal_2 = 0xbeeffeed;
#endif
    sb->nbfree++;
    sb->nballoc--;
    MPID_THREAD_DS_UNLOCK(sb)
}

/*
    Internal routine to allocate space
 */
void MPID_SBiAllocate( 
	MPID_SBHeader sb, 
	int bsize, 
	int nb )
{
    char          *p, *p2;
    int           i, headeroffset;
    MPID_SBiAlloc *header;

/* printf( "Allocating %d blocks of size %d\n", nb, bsize ); */
/* Double-align block */
headeroffset    = (sizeof(MPID_SBiAlloc) + sizeof(double) - 1) / sizeof(double);
headeroffset    *= sizeof(double);

sb->avail       = 0;
p               = (char *) MALLOC( bsize * nb + headeroffset );
if (!p) {
    /* MPID_ERROR( MPI_COMM_WORLD, MPI_ERR_EXHAUSTED, "Not enough space" ); */
   return;
   }
/* Initialize the data to an invalid value */
/* Eventually this will be conditional on debugging */
#ifdef DEBUG
 { int n; 
 n = bsize * nb + headeroffset;
 for (i=0; i<n; i++) 
     p[i] = (char)0xea;
 }
#endif

header          = (MPID_SBiAlloc *)p;
/* Place at header for list of allocated blocks */
header->next    = sb->blocks;
sb->blocks      = header;
header->nbytes  = bsize * nb;
header->nballoc = nb;
header->nbinuse = nb;

/* Link the list together */
p2 = p + headeroffset;
for (i=0; i<nb-1; i++) {
    ((MPID_SBblock *)p2)->next = p2 + bsize;
#ifdef DEBUG
    ((MPID_SBblock *)p2)->sentinal_1 = 0xdeadbeef;
    ((MPID_SBblock *)p2)->sentinal_2 = 0xbeeffeed;
#endif
    p2 += bsize;
    }
((MPID_SBblock *)p2)->next = (char *)sb->avail;
#ifdef DEBUG
((MPID_SBblock *)p2)->sentinal_1 = 0xdeadbeef;
((MPID_SBblock *)p2)->sentinal_2 = 0xbeeffeed;
#endif
sb->avail  = (MPID_SBblock *)(p + headeroffset);
sb->nbfree += nb;
}

/* 
    MPID_SBalloc - Gets a block from the fixed-block allocator.

    Input Parameter:
.   sb - Block context (from MPID_SBinit)

    Returns:
    Address of new block.  Allocates more blocks if required.
 */
void *MPID_SBalloc( 
	MPID_SBHeader sb)
{
MPID_SBblock *p;

MPID_THREAD_DS_LOCK(sb)
if (!sb->avail) {
    MPID_SBiAllocate( sb, sb->sizeb, sb->sizeincr );   /* nbincr instead ? */
    if (!sb->avail) {
	/*MPID_ERROR( MPI_COMM_WORLD, MPI_ERR_EXHAUSTED, "Not enough space" );*/
	MPID_THREAD_DS_UNLOCK(sb)
	return 0;
	}
    }
p         = sb->avail;
#ifdef DEBUG
if (p->sentinal_1 != 0xdeadbeef) {
    MPID_ERROR( MPI_COMM_WORLD, MPI_ERR_OTHER, "Corrupted memory (1)!" );
    }
if (p->sentinal_2 != 0xbeeffeed) {
    MPID_ERROR( MPI_COMM_WORLD, MPI_ERR_OTHER, "Corrupted memory (2)!" );
    }
#endif
sb->avail = (MPID_SBblock *)(p->next);
sb->nballoc++;
sb->nbfree--;
/* printf( "Allocating a block at address %x\n", (char *)p ); */
    MPID_THREAD_DS_UNLOCK(sb)
    return (void *)p;
}	

/* 
    MPID_SBPrealloc - Insure that at least nb blocks are available

    Input Parameters:
.   sb - Block header
.   nb - Number of blocks that should be preallocated

    Notes:
    This routine insures that nb blocks are available, not that an
    additional nb blocks are allocated.  This is appropriate for the common
    case where the preallocation is being used to insure that enough space
    is available for a new object (e.g., a sparse matrix), reusing any
    available blocks.
 */
void MPID_SBPrealloc( 
	MPID_SBHeader sb,
	int           nb)
{
if (sb->nbfree < nb) {
    MPID_SBiAllocate( sb, sb->sizeb, nb - sb->nbfree );
    }	
}

/* 
    MPID_SBdestroy - Destroy a fixed-block allocation context

 */
void MPID_SBdestroy( 
	MPID_SBHeader sb)
{
MPID_SBiAlloc *p, *pn;

MPID_THREAD_DS_LOCK(sb)
p = sb->blocks;
while (p) {
    pn = p->next;
    FREE( p );
    p = pn;
    }
MPID_THREAD_DS_UNLOCK(sb)
FREE( sb );
}

/* Decrement the use count for the block containing p */
void MPID_SBrelease( 
	MPID_SBHeader sb,
	void          *ptr)
{
char *p = (char *)ptr;
MPID_SBiAlloc *b;
char *first, *last;

MPID_THREAD_DS_LOCK(sb)
b = sb->blocks;
/* printf( "Releasing a block at address %x\n", (char *)ptr ); */
while (b) {
    first = ((char *)b) + sizeof(MPID_SBiAlloc) - 1;
    last  = first + b->nbytes - 1;
    if (p >= first && p <= last) {
	b->nbinuse--;
	break;
	}
    b = b->next;
    }
MPID_THREAD_DS_UNLOCK(sb)
}

/* Release any unused chuncks */
void MPID_SBFlush( 
	MPID_SBHeader sb)
{
MPID_SBiAlloc *b, *bnext, *bprev = 0;

MPID_THREAD_DS_LOCK(sb)
b = sb->blocks;
while (b) {
    bnext = b->next;
    if (b->nbinuse == 0) {
	if (bprev) bprev->next = bnext;
	else       sb->blocks  = bnext;
	sb->nballoc -= b->nballoc;
	FREE( b );
	}
    else 
	bprev = b;
    b = bnext;
    }
MPID_THREAD_DS_UNLOCK(sb)
}

/* Print the allocated blocks */
void MPID_SBDump( 
	FILE          *fp,
	MPID_SBHeader sb)
{
MPID_SBiAlloc *b = sb->blocks;

while (b) {
    fprintf( fp, "Block %lx of %d bytes and %d chuncks in use\n", 
	     (PointerInt)(char *)b, b->nbytes, b->nbinuse );
    b = b->next;
    }
}

void MPID_SBReleaseAvail( 
	MPID_SBHeader sb)
{
MPID_SBblock *p, *pnext;
	
MPID_THREAD_DS_LOCK(sb)
p         = sb->avail;
while (p) {
    pnext = (MPID_SBblock *)(p->next);
    sb->avail = pnext;
    sb->nbfree--;
    MPID_SBrelease( sb, (void *)p );
    p     = pnext;
    }
MPID_THREAD_DS_UNLOCK(sb)
}

#ifdef DEBUG
/* Check that the sb space remains valid ... */
void MPID_SBvalid( 
	MPID_SBHeader sb)
{
MPID_SBblock *p;
	
p         = sb->avail;
while (p) {
    if (p->sentinal_1 != 0xdeadbeef) {
	MPID_ERROR( MPI_COMM_WORLD, MPI_ERR_OTHER, "Corrupted memory (3)!" );
	}
    if (p->sentinal_2 != 0xbeeffeed) {
	MPID_ERROR( MPI_COMM_WORLD, MPI_ERR_OTHER, "Corrupted memory (4)!" );
	}
    p     = (MPID_SBblock *)(p->next);
    }
}
#endif
