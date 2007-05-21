/* getgrank.c */
/* Fortran interface file */
#include <stdio.h>
#include "mpeconf.h"
#include "mpe.h"

#ifdef MPI_BUILD_PROFILING
#ifdef F77_NAME_UPPER
#define mpe_comm_global_rank_ PMPE_COMM_GLOBAL_RANK
#elif defined(F77_NAME_LOWER_2USCORE)
#define mpe_comm_global_rank_ pmpe_comm_global_rank__
#elif !defined(F77_NAME_LOWER_USCORE)
#define mpe_comm_global_rank_ pmpe_comm_global_rank
#else
#define mpe_comm_global_rank_ pmpe_comm_global_rank_
#endif
#else
#ifdef F77_NAME_UPPER
#define mpe_comm_global_rank_ MPE_COMM_GLOBAL_RANK
#elif defined(F77_NAME_LOWER_2USCORE)
#define mpe_comm_global_rank_ mpe_comm_global_rank__
#elif !defined(F77_NAME_LOWER_USCORE)
#define mpe_comm_global_rank_ mpe_comm_global_rank
#endif
#endif

void mpe_comm_global_rank_ ( MPI_Comm *, int *, int *, int * );
void  mpe_comm_global_rank_( comm, rank, grank, __ierr )
MPI_Comm *comm;
int*rank, *grank;
int *__ierr;
{
    MPE_Comm_global_rank( *comm,*rank,grank);
}
