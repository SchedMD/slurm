#ifndef MPICH_FORTRAN
#define MPICH_FORTRAN

/* Prototypes for Fortran interface module routines that are called by the 
   MPICH initialize and finalize routines */

int MPIR_InitFortran( void );
int MPIR_InitFortranDatatypes( void );
void MPIR_Free_Fortran_dtes( void );
void MPIR_Free_Fortran_keyvals( void );
#endif
