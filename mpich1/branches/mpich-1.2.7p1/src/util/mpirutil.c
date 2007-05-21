/*
 *  $Id: mpirutil.c,v 1.7 2001/10/19 22:01:20 gropp Exp $
 *
 *  (C) 1993 by Argonne National Laboratory and Mississipi State University.
 *      See COPYRIGHT in top-level directory.
 */


/* mpir helper routines
*/

#include "mpiimpl.h"

int MPIR_Tab ( int );

/* Old comments on queueing messages ... */
/*
   Queueing unexpected messages and searching for them requires particular
   care because of the three cases:
   tag              source            Ordering
   xxx              xxx               Earliest message in delivery order
   MPI_ANY_TAG      xxx               Earliest with given source, not 
                                      lowest in tag value
   xxx              MPI_ANY_SOURCE    Earliest with given tag, not lowest
                                      in source.

   Note that only the first case is explicitly required by the MPI spec.
   However, there is a requirement on progress that requires SOME mechanism;
   the one here is easy to articulate and matches what many users expect.
   
   There are many comprimises that must be considered when deciding how to
   provide for these different criteria.  The code here optimizes for the
   case when both tag and source are specified.  An additional set of links,
   providing raw delivery order, allows a sequential search of the list 
   for the other two cases.  This isn't optimal, particularly for the
   case where the tag is specified and the source is MPI_ANY_SOURCE, but
   will work.

   An enhancment of this approach is to include a delivery-order sequence 
   number in the queue elements; 

   The current system does a linear search through the entire list, and 
   thus will always give the earliest in delivery order AS RECEIVED BY THE
   ADI.  We've had trouble with message-passing systems that the ADI is
   using not providing a fair delivers (starving some sources); this 
   should be fixed by the ADI or underlying message-passing system rather
   than by this level of MPI routine.

   There are no longer any send queues, and hence only receive queues
   are of interest.  We'll leave this in in case we need it for cancel (!)
 */

int MPIR_dump_dte( 
	MPI_Datatype  dte,
	int indent)
{
    int i;
    struct MPIR_DATATYPE *dtype_ptr;

    dtype_ptr   = MPIR_GET_DTYPE_PTR(dte);

    switch (dtype_ptr->dte_type)
    {
    case MPIR_INT:
	MPIR_Tab( indent );
	PRINTF( "int\n" );
	break;
    case MPIR_UINT:
	MPIR_Tab( indent );
	PRINTF( "unsigned\n" );
	break;
    case MPIR_FLOAT:
	MPIR_Tab( indent );
	PRINTF( "float\n" );
	break;
    case MPIR_DOUBLE:
	MPIR_Tab( indent );
	PRINTF( "double\n" );
	break;
    case MPIR_BYTE:
	MPIR_Tab( indent );
	PRINTF( "byte\n" );
	break;
    case MPIR_PACKED:
	MPIR_Tab( indent );
	PRINTF( "packed\n" );
	break;
    case MPIR_CHAR:
	MPIR_Tab( indent );
	PRINTF( "char\n" );
	break;
    case MPIR_UCHAR:
	MPIR_Tab( indent );
	PRINTF( "unsigned char\n" );
	break;
    case MPIR_ULONG:
	MPIR_Tab( indent );
	PRINTF( "unsigned long\n" );
	break;
    case MPIR_LONG:
	MPIR_Tab( indent );
	PRINTF( "long\n" );
	break;
    case MPIR_SHORT:
	MPIR_Tab( indent );
	PRINTF( "short\n" );
	break;
    case MPIR_USHORT:
	MPIR_Tab( indent );
	PRINTF( "unsigned short\n" );
	break;
    case MPIR_CONTIG:
	MPIR_Tab( indent );
	PRINTF( "contig, count = %d\n", dtype_ptr->count );
	MPIR_dump_dte( dtype_ptr->old_type->self, indent + 2 );
	break;
    case MPIR_VECTOR:
	MPIR_Tab( indent );
	PRINTF( "vector, count = %d, stride = %ld, blocklen = %d\n",
		dtype_ptr->count, (unsigned long)dtype_ptr->stride, 
		dtype_ptr->blocklen );
	MPIR_dump_dte( dtype_ptr->old_type->self, indent + 2 );
	break;
    case MPIR_HVECTOR:
	MPIR_Tab( indent );
	PRINTF( "hvector, count = %d, stride = %ld, blocklen = %d\n",
		dtype_ptr->count, (unsigned long)dtype_ptr->stride, 
		dtype_ptr->blocklen );
	MPIR_dump_dte( dtype_ptr->old_type->self, indent + 2 );
	break;
    case MPIR_INDEXED:
	MPIR_Tab( indent );
	PRINTF( "indexed, count = %d\n", dtype_ptr->count );
	MPIR_dump_dte( dtype_ptr->old_type->self, indent + 2 );
	for ( i = 0; i < dtype_ptr->count; i++)
	{
	    MPIR_Tab( indent + 4 );
	    PRINTF("index = %ld, blocklen = %d\n",
		   (unsigned long)dtype_ptr->indices[i],
		   dtype_ptr->blocklens[i] );
	}
	break;
    case MPIR_HINDEXED:
	MPIR_Tab( indent );
	PRINTF( "hindexed, count = %d\n", dtype_ptr->count );
	MPIR_dump_dte( dtype_ptr->old_type->self, indent + 2 );
	for ( i = 0; i < dtype_ptr->count; i++)
	{
	    MPIR_Tab( indent + 4 );
	    PRINTF("index = %ld, blocklen = %d\n",
		   (unsigned long)dtype_ptr->indices[i], 
		   dtype_ptr->blocklens[i] );
	}
	break;
    case MPIR_STRUCT:
	MPIR_Tab( indent );
	PRINTF( "struct, count = %d\n", dtype_ptr->count );
	for ( i = 0; i < dtype_ptr->count; i++)
	{
	    MPIR_Tab( indent + 2 );
	    PRINTF("index = %ld, blocklen = %d\n",
		   (unsigned long)dtype_ptr->indices[i], 
		   dtype_ptr->blocklens[i] );
	    MPIR_dump_dte( dtype_ptr->old_types[i]->self, indent + 2 );
	}
	break;
    case MPIR_COMPLEX:
	MPIR_Tab( indent );
	PRINTF( "complex\n" );
	break;
    case MPIR_DOUBLE_COMPLEX:
	MPIR_Tab( indent );
	PRINTF( "double complex\n" );
	break;
    case MPIR_LONGDOUBLE:
	MPIR_Tab( indent );
	PRINTF( "long double\n" );
	break;
    case MPIR_LONGLONGINT:
	MPIR_Tab( indent );
	PRINTF( "long long\n" );
	break;
    case MPIR_LOGICAL:
	MPIR_Tab( indent );
	PRINTF( "LOGICAL (Fortran)\n" );
	break;
    case MPIR_FORT_INT:
	MPIR_Tab( indent );
	PRINTF( "INTEGER (Fortran)\n" );
	break;
    case MPIR_UB:
	MPIR_Tab( indent );
	PRINTF( "UB\n" );
	break;
    case MPIR_LB:
	MPIR_Tab( indent );
	PRINTF( "LB\n" );
	break;
    }
return MPI_SUCCESS;
}

#ifdef FOO
/* adds to singly-linked list of flat datatype elements, returns pointer to
   head and to "next" pointer in last element, for appending.  Updates 
   current displacement.
 */
int MPIR_flatten_dte( 
	MPI_Datatype dte, 
	MPIR_FDTEL **fdte, 
	MPIR_FDTEL ***tailptr, 
	int *disp )
{
    int i;
    MPIR_FDTEL *p, **q, **r;

/*
    PRINTF("entering flatten dte, dte type = %d, count = %d\n",
	   dte->dte_type, dte->count);
*/
    switch (dte->dte_type)
    {
      case MPIR_INT:
	p         = (MPIR_FDTEL *) MPIR_SBalloc( MPIR_fdtels );
	p->disp   = *disp;
	p->type   =  MPIR_INT;
	*disp     += sizeof( MPIR_INT );
	*tailptr  = &(p->next);
	*fdte     = p;
	break;
      case MPIR_FLOAT:
	p         = (MPIR_FDTEL *) MPIR_SBalloc( MPIR_fdtels );
	p->disp   = *disp;
	p->type   =  MPIR_FLOAT;
	*disp     += sizeof( MPIR_FLOAT );
	*tailptr  = &(p->next);
	*fdte     = p;
	break;
      case MPIR_DOUBLE:
	p         = (MPIR_FDTEL *) MPIR_SBalloc( MPIR_fdtels );
	p->disp   = *disp;
	p->type   = MPIR_DOUBLE;
	*disp    += sizeof( MPIR_DOUBLE );
	*tailptr  = &(p->next);
	*fdte     = p;
	break;
      case MPIR_CONTIG:
	r = &p;
	for (i = 0; i < dte->count; i++)
	{
	    MPIR_flatten_dte(dte->old_type, r, &q, disp );
	    if ( i == 0 )
		*fdte = p;	/* remember the first one */
	    r = q;
	}
	*tailptr = r;
	break;
      default:
	PRINTF("mpir_flatten not implemented yet for type %d\n",dte->dte_type);
    }
    return MPI_SUCCESS;
}

int MPIR_dump_flat_dte( MPIR_FDTEL *fdte )
{
    MPIR_FDTEL *p;

    p = fdte;
    while ( p )
    {
	PRINTF("(%d,%d),", p->type, p->disp );
	p = p->next;
    }
    PRINTF("\n");
    return MPI_SUCCESS;
}

#endif

int MPIR_Tab( int n  )
{
    int i;

    for (i = 0; i < n; i++)
	putchar(' ');
    return MPI_SUCCESS;
}

