#include "mpeconf.h"
#include "mpi.h"
#include "mpe.h"
/*
  This file contains a routine for producing a decomposition of a 1-d array
  when given a number of processors.  It may be used in "direct" product
  decomposition.  The values returned assume a "global" domain in [1:n]
 */
/*@
  MPE_Decomp1d - Compute a balanced decomposition of a 1-D array

  Input Parameters:
+ n  - Length of the array
. size - Number of processors in decomposition
- rank - Rank of this processor in the decomposition (0 <= rank < size)

  Output Parameters:
. s,e - Array indices are s:e, with the original array considered as 1:n.  
@*/
int MPE_Decomp1d( n, size, rank, s, e )
int n, size, rank, *s, *e;
{
    int nlocal, deficit;

    nlocal	= n / size;
    *s	= rank * nlocal + 1;
    deficit	= n % size;
    *s	= *s + ((rank < deficit) ? rank : deficit);
    if (rank < deficit) nlocal++;
    *e      = *s + nlocal - 1;
    if (*e > n || rank == size-1) *e = n;
    return MPI_SUCCESS;
}

