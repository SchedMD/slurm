#include "mpi_fortimpl.h"

/* Fortran logical values */
MPI_Fint MPIR_F_TRUE, MPIR_F_FALSE;

/* 
 Location of the Fortran marker for MPI_BOTTOM.  The Fortran wrappers
 must detect the use of this address and replace it with MPI_BOTTOM.
 This is done by the macro MPIR_F_PTR.
 */
void *MPIR_F_MPI_BOTTOM = 0;
/* Here are the special status ignore values in MPI-2 */
void *MPIR_F_STATUS_IGNORE = 0;
void *MPIR_F_STATUSES_IGNORE = 0;

#ifdef USE_GCC_G77_DECLS
/* These can help shared library support when gcc/g77 are used.  They are only 
   valid in that case */
void getarg_ (long *n, char *s, short ls) __attribute__ ((weak));
void getarg_ (long *n, char *s, short ls) {} 
int f__xargc __attribute__ ((weak)) = -1;
#endif

void MPIR_Init_f77( void )
{
#ifdef F77_TRUE_VALUE_SET
    MPIR_F_TRUE  = F77_TRUE_VALUE;
    MPIR_F_FALSE = F77_FALSE_VALUE;
#else
    mpir_init_flog_( &MPIR_F_TRUE, &MPIR_F_FALSE );
#endif

#ifndef USE_POINTER_FOR_BOTTOM
    /* fcm sets MPI_BOTTOM */
    mpir_init_fcm_( );
#endif
}
