/*
 *  $Id: ptrcvt.c,v 1.10 2001/10/19 22:01:20 gropp Exp $
 *
 *  (C) 1994 by Argonne National Laboratory and Mississipi State University.
 *      See COPYRIGHT in top-level directory.
 */

/* 
   This file contains routines to convert to and from pointers

   There is another approach that can be used on some systems.  This is
   to identify the 32bit range that is used by pointers, and to do the
   appropriate masks/shifts to make these valid integers.  This requires
   that the pointers actually lie in some 4 GB part of a 64 bit 
   integer, and that this segment is known in advance.  Because ensuring
   that the conditions are upheld requires friendly relations with the
   OS and runtime library developers, we cannot make use of this 
   in the portable system, but it may be valuable for specific ports.

   Thanks to Jim Cownie for this suggestion.

   In order to improve the ability of this code to handle large numbers
   of conversions, I'm switching to the following strategy:

   There is an array PtrToIdxPtr[] of pointers to blocks of size 2**k
   (k = 10 for now).  In addition, the initial block is preallocated.
   When a block runs out of room, we add one to the PtrToIdxPtr[];
   given an index, we use the high bits to find the entry in the array and
   to low bits to index into the block.

   To avoid the extra indirection, we could test for a 0 index and go 
   directly to the preallocated first block.

   A comment on the use of (long) instead of (MPI_Aint) to cast pointers
   for printf output: Unfortunately, printf must be given a length description
   that matches the type.  It isn't possible (in C) to register a conversion
   function for a user-defined type (like MPI_Aint).  Thus, to ensure that
   the printf format specification matches the value passed, we cast
   the pointers to (long).  This will be adequate for most but not all
   systems; those that use long long will need to change both the cast
   and the format specification (we could, of course, edit the format 
   string or write out the pointer with a separate printf, but for this
   code, it really isn't worth the effort).

   As currently organized, each object must be explicitly freed with a 
   call to RmPointer.  A better solution is to make this part of the 
   object's free routine - when an index is registered, the object remembers
   that and uses that to free the index.  This will be necessary for
   implementing the request conversion routine (MPI_Request_c2f/f2c), since
   there is no separate free handle routine.

   Note that, in order to speed up the pointer-to-index lookup, many objects
   already have a "self" field.  That really should be "self_index".
 */

/* mpiimpl.h is needed to get the symbols like HAVE_STDLIB_H ... */
#include "mpiimpl.h"
#include <stdio.h>
#ifdef HAVE_STDLIB_H
#include <stdlib.h>
/* Else we should define malloc somehow... */
#endif
#include "mpiimpl.h"
#include "tr2.h"
#include "ptrcvt.h"

typedef struct _PtrToIdx {
    int idx;
    void *ptr;
    struct _PtrToIdx *next;
    } PtrToIdx;
/* These 4 go together and must be changed consistently */
#define MAX_PTRS 1024
#define PTR_MASK (0x3ff)
#define PTR_IDX(i) ((i) >> 10)
#define BLOCK_IDX(i) ((i) << 10)

#define MAX_BLOCKS 256
/* #define MAX_PTRS 10000 */

static PtrToIdx *(PtrBlocks[MAX_BLOCKS]);
static PtrToIdx PtrArray[MAX_PTRS];
static PtrToIdx *avail=0;
static int      DoInit = 1;
/* Perm_In_use lets us know how many permanent, system owned indices there are.
  */
static int      Perm_In_Use = 0;
static int      PermPtr = 0;

static int      DebugFlag = 0;

void MPIR_PointerPerm( int flag )
{
    PermPtr = flag;
}

void MPIR_PointerOpts( int flag )
{
    DebugFlag = flag;
}

static void MPIR_InitPointer (void)
{
    int  i;

    for (i=0; i<MAX_PTRS-1; i++) {
	PtrArray[i].next = PtrArray + i + 1;
	PtrArray[i].idx  = i;
    }
    PtrArray[MAX_PTRS-1].next = 0;
    for (i=1; i<MAX_BLOCKS; i++) 
	PtrBlocks[i] = 0;
    PtrBlocks[0] = PtrArray;

    /* Map null onto null */
    PtrArray[0].ptr = 0;
    /* Don't start with the first one, whose index is 0. That could
       break some code. */
    /* In fact, start 128 into the array ... */
    avail   = PtrArray + 128;
    PtrArray[127].next = 0;
    /* Loop the end of the loop to the beginning */
    PtrArray[MAX_PTRS-1].next = PtrArray + 1;
    PtrArray[MAX_PTRS-1].idx  = MAX_PTRS - 1;
}

/*
 * Free any space allocated by the pointer routines
 */
void MPIR_DestroyPointer (void)
{
    int blocknum;
    for (blocknum=1; blocknum<MAX_BLOCKS; blocknum++) {
	if (!PtrBlocks[blocknum]) break;
	FREE( PtrBlocks[blocknum] );
	PtrBlocks[blocknum] = 0;
    }
}

void *MPIR_ToPointer( int idx )
{
    int blockidx, blocknum;

    if (DoInit) {
	DoInit = 0;
	MPIR_InitPointer();
    }

    /* Handle the case of the first block separately and faster 
       (this code should eventually go into a macro) 
     */
    if (idx >= 0 && idx < MAX_PTRS)
	return PtrArray[idx].ptr;

    /* Here is the general case; we've run out of the initial set of
       pointers, and we've allocated "extensions".  Get the extension
       and check that it is legal  

       (Erroneous idx values will land us here as well)
     */
    blocknum = PTR_IDX(idx);
    blockidx = idx & PTR_MASK;
    if (blocknum < 0 || blocknum >= MAX_BLOCKS ||
	blockidx < 0 || blockidx >= MAX_PTRS || !PtrBlocks[blocknum]) {
	int mpi_errno;
	/* Errors here are fatal */
	mpi_errno = MPIR_Err_setmsg( MPI_ERR_OTHER, MPIR_ERR_BAD_INDEX,
				     (char *)0, (char *)0, (char *)0, idx );
	MPIR_COMM_WORLD->use_return_handler = 0;
	/* Error in MPI Object */
	MPIR_ERROR( MPIR_COMM_WORLD, mpi_errno, (char *)0 );
	return (void *)0;
    }
    if (blocknum == 0 && blockidx == 0) return (void *)0;

    if (DebugFlag) {
	PRINTF( "ToPointer is %d for pointer %lx in block %d\n", 
		idx, (long)PtrBlocks[blocknum][blockidx].ptr, blocknum );
    }
    return PtrBlocks[blocknum][blockidx].ptr;
}

/* Create an index from a pointer */
int MPIR_FromPointer( void *ptr )
{
    int      idx, blocknum, i;
    PtrToIdx *new;

    if (DoInit) {
	DoInit = 0;
	MPIR_InitPointer();
    }
    if (PermPtr) Perm_In_Use++;

    if (!ptr) return 0;
    if (avail) {
	new		  = avail;
	avail		  = avail->next;
	new->next	  = 0;
	idx		  = new->idx;
	new->ptr          = ptr;
/*
	blocknum = PTR_IDX(idx);
	PtrBlocks[blocknum][idx&PTR_MASK].ptr = ptr;
	PtrArray[idx].ptr = ptr;
 */
	if (DebugFlag) {
	    PRINTF( "Pointer %lx has idx %d from avail list\n", 
		    (long)ptr, idx );
	}
	return idx;
    }

    /* Allocate a new block and add it to the PtrBlock array */
    for (blocknum=1; blocknum<MAX_BLOCKS; blocknum++) {
	if (!PtrBlocks[blocknum]) break;
    }
    if (blocknum == MAX_BLOCKS) {
	/* This isn't the right thing to do, but it isn't too bad */
	/* Errors here are fatal */
	MPIR_COMM_WORLD->use_return_handler = 0;
	(void) MPIR_ERROR( MPIR_COMM_WORLD, 
	    MPIR_ERRCLASS_TO_CODE(MPI_ERR_OTHER,MPIR_ERR_INDEX_EXHAUSTED), 
			   (char *)0 );
    }
    PtrBlocks[blocknum] = (PtrToIdx *)MALLOC( sizeof(PtrToIdx) * MAX_PTRS );
    if (!PtrBlocks[blocknum]) {
	/* Errors here are fatal */
	MPIR_COMM_WORLD->use_return_handler = 0;
	(void) MPIR_ERROR( MPIR_COMM_WORLD,
	    MPIR_ERRCLASS_TO_CODE(MPI_ERR_OTHER,MPIR_ERR_INDEX_EXHAUSTED), 
			   (char *)0 );
    }
    for (i=0; i<MAX_PTRS-1; i++) {
	PtrBlocks[blocknum][i].next = &PtrBlocks[blocknum][i+1];
	PtrBlocks[blocknum][i].idx  = BLOCK_IDX(blocknum) | i;
    }
    PtrBlocks[blocknum][MAX_PTRS-1].next = 0;
    PtrBlocks[blocknum][MAX_PTRS-1].idx  = BLOCK_IDX(blocknum) | (MAX_PTRS-1);
    new = &PtrBlocks[blocknum][0];
    new->next	  = 0;
    idx		  = new->idx;
    new->ptr      = ptr;
    avail = &PtrBlocks[blocknum][1];

    if (DebugFlag) {
	PRINTF( "Pointer %lx has idx %d from new block %d at %d\n",
		(long)ptr, idx, blocknum, 1 );
    }
    return idx;
}

void MPIR_RmPointer( int idx )
{
    int      blocknum, blockidx;
    PtrToIdx *ptridx;

    if (DoInit) {
	DoInit = 0;
	MPIR_InitPointer();
    }

    blocknum = PTR_IDX(idx);
    blockidx = idx & PTR_MASK;
    if (blocknum < 0 || blocknum >= MAX_BLOCKS ||
	blockidx < 0 || blockidx >= MAX_PTRS || !PtrBlocks[blocknum]) {
	int mpi_errno;
	/* Errors here are fatal */
	mpi_errno = MPIR_Err_setmsg( MPI_ERR_OTHER, MPIR_ERR_BAD_INDEX,
				     (char *)0, (char *)0, (char *)0, idx );
	MPIR_COMM_WORLD->use_return_handler = 0;
	MPIR_ERROR( MPIR_COMM_WORLD, mpi_errno, (char *)0 );
	return;
    }

/* #define DEBUG_NULL_IDX */
#ifdef DEBUG_NULL_IDX
    if (idx == 0) {
	/* Errors here are fatal */
	mpi_errno = MPIR_Err_setmsg( MPI_ERR_OTHER, MPIR_ERR_BAD_INDEX,
				     (char *)0, (char *)0, (char *)0, idx );
	MPIR_COMM_WORLD->use_return_handler = 0;
	MPIR_ERROR( MPIR_COMM_WORLD, mpi_errno, (char *)0 );
	return;
    }
#endif
    /* Just skip null pointers */
    if (blocknum == 0 && blockidx == 0) return;

    ptridx = PtrBlocks[blocknum];
    if (ptridx[blockidx].next) {
	int mpi_errno;
	/* In-use pointers NEVER have next set */
	/* Errors here are fatal */
	mpi_errno = MPIR_Err_setmsg( MPI_ERR_OTHER, MPIR_ERR_INDEX_FREED,
				     (char *)0, (char *)0, (char *)0, idx );
	MPIR_COMM_WORLD->use_return_handler = 0;
	MPIR_ERROR( MPIR_COMM_WORLD, mpi_errno, 
		    "Error in MPI object - already freed" );
	return;
    }

    ptridx[blockidx].next = avail;
    ptridx[blockidx].ptr  = 0;
    avail		  = &ptridx[blockidx];
    if (DebugFlag) {
	PRINTF( "Removed idx %d in block %d\n", idx, blocknum );
    }
}

/*
 * Produce information on the pointer conversions in use on the specified file.
 */
int MPIR_UsePointer( 
	FILE *fp)
{
    int      count;
    int      in_use, allocated_blocks;
    PtrToIdx *p;
    
    if (DoInit) return 0;

    /* Find the number of blocks */
    for (allocated_blocks=1; allocated_blocks < MAX_BLOCKS; 
	 allocated_blocks++) 
	if (!PtrBlocks[allocated_blocks]) break;
   
   /* Find out how many are not in use */
    count	= 0;
    p		= avail;
    while (p) {
	count++;
	p = p->next;
	/* This test protects against a corrupted list */
	if (count > MAX_PTRS * allocated_blocks + 1) break;
    }
    /* The number in use is MAX_PTRS * allocated_blocks - count - 1 
       (The -1 is because index 0 is never made available) */
    if (count > MAX_PTRS * allocated_blocks) {
	FPRINTF( fp, "# Pointer conversions corrupted!\n" );
	return count;
    }
    in_use = MAX_PTRS * allocated_blocks - count - 1 - Perm_In_Use;
    if (in_use > 0 && fp) {
	FPRINTF( fp, 
	 "# There are %d pointer conversions in use\n", in_use );
    }
    return in_use;
}

/* 
 *  Given a predetermined idx (and corresponding pointer), place in the
 *  index to pointer table.  For simplicity, predefined values MUST be
 *  in the initial block.
 */
void MPIR_RegPointerIdx( 
	int  idx,
	void *ptr)
{
    PtrToIdx *new;

    if (DoInit) {
	DoInit = 0;
	MPIR_InitPointer();
    }
    if (PermPtr) Perm_In_Use++;

    if (idx > MAX_PTRS) {
	fprintf( stderr, "Internal error! Predefined index %d too large!\n",
		 idx );
	/* Errors here are fatal */
	MPIR_COMM_WORLD->use_return_handler = 0;
	MPIR_ERROR( MPIR_COMM_WORLD, MPI_ERR_INTERN, 
		    "Handle value too large" );
	return;
    }

    PtrArray[idx].ptr  = ptr;
    PtrArray[idx].idx  = idx;
    /* Find the index block and link around it */
    if (avail == PtrArray + idx) {
	avail = PtrArray[idx].next;
    }
    else {
	new = avail;
	while (new && new->next != PtrArray + idx) new = new->next;
	if (new) 
	    new->next = PtrArray[idx].next;
	else {
	    /* If we didn't find the entry, we have a problem ... */
	    fprintf( stderr, "Internal Error: Index %d is a duplicate\n", 
		     idx );
	    /* Errors here are fatal */
	    MPIR_COMM_WORLD->use_return_handler = 0;
	    MPIR_ERROR( MPIR_COMM_WORLD, MPI_ERR_INTERN, 
			"Handle value is a duplicate" );
	    avail = 0;
	}
    }
    /* In use pointers have next cleared */
    PtrArray[idx].next = 0;

    if (DebugFlag) {
	PRINTF( "Registered index %d with pointer %lx\n", idx, (long)ptr );
    }
}

/* For each block, dump the indices and pointers of the objects mapped */
typedef struct { unsigned long val; char name[12]; } cookie_def;
#define N_COOKIE 9
#define MPIR_HBT_COOKIE 0x03b7c007
#define MPIR_ATTR_COOKIE 0xa774c003
#define MPIR_HBT_NODE_COOKIE 0x03b740de
#define MPIR_OP_COOKIE 0xca01beaf

static cookie_def cookie[N_COOKIE] = {
    { 	   MPIR_HBT_COOKIE, "HBT" },
    { 	   MPIR_ATTR_COOKIE, "ATTR" },
    { 	   MPIR_HBT_NODE_COOKIE, "HBT NODE" },
    {	   MPIR_GROUP_COOKIE, "GROUP" },
    {	   MPIR_COMM_COOKIE, "COMM" },
    {	   MPIR_OP_COOKIE, "MPI_Op" },
    {	   MPIR_REQUEST_COOKIE, "REQUEST" },
    {	   MPIR_DATATYPE_COOKIE, "DATATYPE" },
    {	   MPIR_ERRHANDLER_COOKIE, "ERRHANDLER" } };

void MPIR_DumpPointers( 
	FILE *fp )
{
    PtrToIdx *new;
    int blocknum, idx;
    int *header, found_cookie, i, j;

    /* 
       First, mark all avail blocks with ptr == 0
     */
    new = avail;
    while (new) {
	new->ptr = 0;
	new = new->next;
    }
    for (blocknum=0; blocknum < MAX_BLOCKS; blocknum++) {
	if (PtrBlocks[blocknum] == 0) break;

	new = PtrBlocks[blocknum];
	for (idx = 0; idx < MAX_PTRS; idx++) {
	    if (new[idx].ptr) {
		FPRINTF( fp, "Index %d in use for pointer %lx",
			 new[idx].idx, (long) new[idx].ptr );
		/* We to print the object */
		header = (int *)new[idx].ptr;
		found_cookie = 0;
		for (i=0; i<2; i++) {
		    for (j=0; j<N_COOKIE; j++) {
			if (header[i] == (int)cookie[j].val) {
			    FPRINTF( fp, " %s\n", cookie[j].name );
			    found_cookie = 1;
			    j = N_COOKIE; i = 3;
			}
			else if (header[i] == (int)cookie[j].val + 1) {
			    FPRINTF( fp, " %s <deleted>\n", cookie[j].name );
			    found_cookie = 1;
			    j = N_COOKIE; i = 3;
			}
		    }
		}
		if (!found_cookie) {
		    FPRINTF( fp, " %x %x \n", header[0], header[1] );
		}
		
	    }
	}
    }
}

