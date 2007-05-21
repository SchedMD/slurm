/*
 *  $Id: comm_split.c,v 1.13 2002/08/29 14:48:31 gropp Exp $
 *
 *  (C) 1993 by Argonne National Laboratory and Mississipi State University.
 *      See COPYRIGHT in top-level directory.
 */


#include "mpiimpl.h"

#ifdef HAVE_WEAK_SYMBOLS

#if defined(HAVE_PRAGMA_WEAK)
#pragma weak MPI_Comm_split = PMPI_Comm_split
#elif defined(HAVE_PRAGMA_HP_SEC_DEF)
#pragma _HP_SECONDARY_DEF PMPI_Comm_split  MPI_Comm_split
#elif defined(HAVE_PRAGMA_CRI_DUP)
#pragma _CRI duplicate MPI_Comm_split as PMPI_Comm_split
/* end of weak pragmas */
#endif

/* Include mapping from MPI->PMPI */
#define MPI_BUILD_PROFILING
#include "mpiprof.h"
/* Insert the prototypes for the PMPI routines */
#undef __MPI_BINDINGS
#include "binding.h"
#endif
#include "mpimem.h"

/* Also used in comm_util.c */
#define MPIR_Table_color(table,i) table[(i)]
#define MPIR_Table_key(table,i)   table[((i)+size)]
#define MPIR_Table_next(table,i)  table[((i)+(2*size))]

/*@

MPI_Comm_split - Creates new communicators based on colors and keys

Input Parameters:
+ comm - communicator (handle) 
. color - control of subset assignment (nonnegative integer).  Processes 
  with the same color are in the same new communicator 
- key - control of rank assigment (integer)

Output Parameter:
. newcomm - new communicator (handle) 

Notes:
  The 'color' must be non-negative or 'MPI_UNDEFINED'.

.N fortran

Algorithm:

The current algorithm used has quite a few (read: a lot of) inefficiencies 
that can be removed.  Here is what we do for now

.vb
 1) A table is built of colors, and keys (has a next field also).
 2) The tables of all processes are merged using 'MPI_Allreduce'.
 3) Two contexts are allocated for all the comms to be created.  These
     same two contexts can be used for all created communicators since
     the communicators will not overlap.
 4) If the local process has a color of 'MPI_UNDEFINED', it can return
     a 'NULL' comm. 
 5) The table entries that match the local process color are sorted 
     by key/rank. 
 6) A group is created from the sorted list and a communicator is created
     with this group and the previously allocated contexts.
.ve

.N Errors
.N MPI_SUCCESS
.N MPI_ERR_COMM
.N MPI_ERR_EXHAUSTED

.seealso: MPI_Comm_free
@*/
int MPI_Comm_split ( MPI_Comm comm, int color, int key, MPI_Comm *comm_out )
{
  int           size, rank, head, new_size, i;
  int          *table, *table_in;
  MPIR_CONTEXT  context;
  int          *group_list;
  MPI_Group     comm_group, group;
  struct MPIR_COMMUNICATOR *comm_ptr, *new_comm;
  struct MPIR_GROUP *group_ptr;
  int           mpi_errno = MPI_SUCCESS;
  MPIR_ERROR_DECL;
  static char myname[] = "MPI_COMM_SPLIT";

  TR_PUSH(myname);
  comm_ptr = MPIR_GET_COMM_PTR(comm);
  MPIR_TEST_MPI_COMM(comm,comm_ptr,comm_ptr,myname);

  /* If we don't have a communicator we don't have anything to do */
  if ( ( (comm_ptr->comm_type == MPIR_INTER) && (mpi_errno = MPI_ERR_COMM) ) ) {
    (*comm_out) = MPI_COMM_NULL;
    return MPIR_ERROR( comm_ptr, mpi_errno, myname );
  }

  /* Create and initialize split table. */
  (void) MPIR_Comm_size ( comm_ptr, &size );
  (void) MPIR_Comm_rank ( comm_ptr, &rank );
  MPIR_ALLOC(table,(int *) CALLOC ( 2 * 3 * size, sizeof(int) ),
	     comm_ptr,MPI_ERR_EXHAUSTED,"MPI_COMM_SPLIT" );
  
  table_in = table + (3 * size);
  MPIR_Table_color(table_in,rank) = color;
  MPIR_Table_key(table_in,rank)   = key;

  MPIR_ERROR_PUSH(comm_ptr);

  /* Combine the split table. I only have to combine the colors and keys */
  mpi_errno = PMPI_Allreduce(table_in, table, size * 2, MPI_INT, MPI_SUM, comm);

  /* Allocate 2 contexts */
  mpi_errno = MPIR_Context_alloc( comm_ptr, 2, &context );

  /* If my color is MPI_UNDEFINED, then I'm not in a comm and can */
  /* stop here since there are no more communications with others */
  /* I'll even go ahead and free the 2 contexts I allocated above */
  if ( MPIR_Table_color(table,rank) == MPI_UNDEFINED ) {
      mpi_errno = MPID_CommInit( comm_ptr, NULL );
      MPIR_ERROR_POP(comm_ptr);
      FREE(table);
      (void) MPIR_Context_dealloc( comm_ptr, 2, context );
      (*comm_out) = MPI_COMM_NULL;
      TR_POP;
      return (mpi_errno);
  }

  /* Sort the table */
  (void) MPIR_Sort_split_table ( size, rank, table, &head, &new_size );
     
  /* Create group of processes that share my color */
  MPIR_ALLOC(group_list,(int *) MALLOC ( new_size * sizeof(int) ),
	     comm_ptr,MPI_ERR_EXHAUSTED,myname);
  for ( i=0; i<new_size; i++, head=MPIR_Table_next(table,head) )
    group_list[i] = head;
  MPIR_CALL_POP(MPI_Comm_group ( comm, &comm_group ),comm_ptr, myname );
  MPIR_CALL_POP(MPI_Group_incl ( comm_group, new_size, group_list, &group ),
		comm_ptr, myname );
  MPIR_CALL_POP(MPI_Group_free ( &comm_group ),comm_ptr,myname);
  FREE(table);
  FREE(group_list);

  MPIR_ERROR_POP(comm_ptr);

  group_ptr = MPIR_GET_GROUP_PTR(group);
/*  MPIR_TEST_MPI_GROUP(group,group_ptr,MPIR_COMM_WORLD,myname); */
#ifndef MPIR_NO_ERROR_CHECKING
  MPIR_TEST_GROUP(group_ptr);
    if (mpi_errno)
	return MPIR_ERROR(comm_ptr, mpi_errno, myname );
#endif

  /* Make communicator using contexts allocated */
  MPIR_ALLOC(new_comm,NEW(struct MPIR_COMMUNICATOR),comm_ptr, MPI_ERR_EXHAUSTED, 
	     myname );
  MPIR_Comm_init( new_comm, comm_ptr, MPIR_INTRA );
  *comm_out = new_comm->self;

  new_comm->group         = group_ptr;
  MPIR_Group_dup ( group_ptr, &(new_comm->local_group) );
  new_comm->local_rank	   = new_comm->local_group->local_rank;
  new_comm->lrank_to_grank = new_comm->group->lrank_to_grank;
  new_comm->np             = new_comm->group->np;
  new_comm->send_context   = new_comm->recv_context = context;
  new_comm->comm_name	   = 0;
  (void) MPIR_Attr_create_tree ( new_comm );
  /* CommInit may need lrank_to_grank, etc */
  if ((mpi_errno = MPID_CommInit( comm_ptr, new_comm )) )
      return MPIR_ERROR(comm_ptr,mpi_errno,myname);
  (void) MPIR_Comm_make_coll( new_comm, MPIR_INTRA );

  /* Remember it for the debugger */
  MPIR_Comm_remember ( new_comm );

  TR_POP;
  return (mpi_errno);
}


