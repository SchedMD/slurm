/*
 *  $Id: group_util.c,v 1.5 1999/08/20 02:26:43 ashton Exp $
 *
 *  (C) 1993 by Argonne National Laboratory and Mississipi State University.
 *      See COPYRIGHT in top-level directory.
 */

#include "mpiimpl.h"
#include "mpimem.h"


struct MPIR_GROUP *MPIR_CreateGroup( int np )
{
    struct MPIR_GROUP *new;
    int        i;

    TR_PUSH("MPIR_CreateGroup");

    new = NEW(struct MPIR_GROUP);
    if (!new) return 0;
    MPIR_SET_COOKIE(new,MPIR_GROUP_COOKIE)
    new->np             = np;
    if (np > 0) {
	new->lrank_to_grank = (int *) MALLOC( np * sizeof(int) );
	if (!new->lrank_to_grank) return 0;
	}
    else 
	new->lrank_to_grank = 0;
    new->set_mark   = (int *)0;
    new->local_rank = MPI_UNDEFINED;
    new->ref_count  = 1;
    new->permanent  = 0;
    MPIR_Powers_of_2 ( np, &(new->N2_next), &(new->N2_prev) );

    for (i=0; i<np; i++) 
	new->lrank_to_grank[i] = -1;

    TR_POP;
    return new;
}

void MPIR_FreeGroup( 
	struct MPIR_GROUP *group)
{
    TR_PUSH("MPIR_FreeGroup");

    if (group->lrank_to_grank) {
	FREE( group->lrank_to_grank );
    }
    if ( group->set_mark ) {
	FREE( group->set_mark );
    }
    MPIR_CLR_COOKIE( group );
    MPIR_RmPointer( group->self );
    FREE( group );
    TR_POP;
}

void MPIR_SetToIdentity( 
	struct MPIR_GROUP *g)
{
  int np, i;

  TR_PUSH("MPIR_SetToIdentity");
  np = g->np;
  for (i=0; i<np; i++) 
    g->lrank_to_grank[i] = i;

  g->local_rank = MPID_MyWorldRank;
  if (g->local_rank >= np)
    g->local_rank = MPI_UNDEFINED;
  TR_POP;
}

#ifndef MPIR_Group_dup
/*+

MPIR_Group_dup -

+*/
void MPIR_Group_dup( 
	struct MPIR_GROUP *group, 
	struct MPIR_GROUP **new_group)
{
  (*new_group) = group;
  if ( group != MPI_GROUP_NULL ) {
      MPIR_REF_INCR(group);
  }
}
#endif

/*+

MPIR_Dump_group - dump group information

+*/
int MPIR_Dump_group ( 
	struct MPIR_GROUP *group)
{
  int i, rank;
  (void)MPIR_Comm_rank ( MPIR_COMM_WORLD, &rank );

  /* see ptrcvt.c for why (long) instead of (MPI_Aint) is used */
  PRINTF ( "\t[%d] group       = %ld\n", rank, (long)group );
  if (group != NULL) {
    PRINTF ( "\t[%d] np          = %d\n", rank, group->np );
    PRINTF ( "\t[%d] local rank  = %d\n", rank, group->local_rank );
    PRINTF ( "\t[%d] local rank -> global rank mapping\n", rank );
    for ( i=0; i<group->np; i++ )
      PRINTF ( "\t [%d]   %d             %d\n", rank, i, group->lrank_to_grank[i] );
  }
  return MPI_SUCCESS;
}

/*+

MPIR_Dump_ranks - dump an array of ranks

+*/
int MPIR_Dump_ranks ( 
	int n, 
	int *ranks)
{
  int i;

  PRINTF ( "\tnumber of ranks = %d\n", n );
  PRINTF ( "\t n     rank\n" );
  for ( i=0; i<n; i++ )
    PRINTF ( "\t %d      %d\n", i, ranks[i] );
  return MPI_SUCCESS;
}

/*+

MPIR_Dump_ranges - dump an array of ranges

+*/
int MPIR_Dump_ranges ( 
	int n, 
	int *ranges)
{
  int i;

  PRINTF ( "\tnumber of ranges = %d\n", n );
  PRINTF ( "\t first    last    stride\n" );
  for ( i=0; i<n; i++ )
  PRINTF ( "\t %d      %d        %d       %d\n", i, ranges[i*3],
          ranges[(i*3)+1], ranges[(i*3)+2] );
  return MPI_SUCCESS;
}


/*+

MPIR_Powers_of_2 - given a number N, determine the previous and next
                   powers of 2

+*/
int MPIR_Powers_of_2 ( 
	int  N,
	int *N2_next, 
	int *N2_prev)
{
  int high     = 131072;
  int low      = 1;

  TR_PUSH("MPIR_Powers_of_2");

  while( (high > N) && (low < N) ) {
    high >>= 1; low  <<= 1;
  }

  if(high <= N) {
    if(high == N)   /* no defect, power of 2! */
      (*N2_next) = N;
	else
      (*N2_next) = high << 1;
  }
  else {/* condition low >= N satisfied */
    if(low == N)	/* no defect, power of 2! */
      (*N2_next) = N;
	else
      (*N2_next) = low;
  }

  if( N == (*N2_next) ) /* power of 2 */
	(*N2_prev) = N;
  else
	(*N2_prev) = (*N2_next) >> 1;

  TR_POP;
  return (MPI_SUCCESS);
}

/*+

MPIR_Group_N2_prev - retrieve greatest power of two < size of group.

+*/
int MPIR_Group_N2_prev ( 
	struct MPIR_GROUP *group,
	int       *N2_prev)
{
  (*N2_prev) = group->N2_prev;
  return (MPI_SUCCESS);
}






