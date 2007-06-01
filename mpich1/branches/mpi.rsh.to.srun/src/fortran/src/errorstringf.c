/* error_string.c */
/* Custom Fortran interface file */
#include "mpi_fortimpl.h"
#ifdef _CRAY
#include "fortran.h"
#endif


#if defined(MPI_BUILD_PROFILING) || defined(HAVE_WEAK_SYMBOLS)

#if defined(HAVE_WEAK_SYMBOLS)
#if defined(HAVE_PRAGMA_WEAK)
#if defined(F77_NAME_UPPER)
#pragma weak MPI_ERROR_STRING = PMPI_ERROR_STRING
void MPI_ERROR_STRING ( MPI_Fint *, char *, MPI_Fint *, MPI_Fint *, MPI_Fint );
#elif defined(F77_NAME_LOWER_2USCORE)
#pragma weak mpi_error_string__ = pmpi_error_string__
void mpi_error_string__ ( MPI_Fint *, char *, MPI_Fint *, MPI_Fint *, MPI_Fint );
#elif !defined(F77_NAME_LOWER_USCORE)
#pragma weak mpi_error_string = pmpi_error_string
void mpi_error_string ( MPI_Fint *, char *, MPI_Fint *, MPI_Fint *, MPI_Fint );
#else
#pragma weak mpi_error_string_ = pmpi_error_string_
void mpi_error_string_ ( MPI_Fint *, char *, MPI_Fint *, MPI_Fint *, MPI_Fint );
#endif

#elif defined(HAVE_PRAGMA_HP_SEC_DEF)
#if defined(F77_NAME_UPPER)
#pragma _HP_SECONDARY_DEF PMPI_ERROR_STRING  MPI_ERROR_STRING
#elif defined(F77_NAME_LOWER_2USCORE)
#pragma _HP_SECONDARY_DEF pmpi_error_string__  mpi_error_string__
#elif !defined(F77_NAME_LOWER_USCORE)
#pragma _HP_SECONDARY_DEF pmpi_error_string  mpi_error_string
#else
#pragma _HP_SECONDARY_DEF pmpi_error_string_  mpi_error_string_
#endif

#elif defined(HAVE_PRAGMA_CRI_DUP)
#if defined(F77_NAME_UPPER)
#pragma _CRI duplicate MPI_ERROR_STRING as PMPI_ERROR_STRING
#elif defined(F77_NAME_LOWER_2USCORE)
#pragma _CRI duplicate mpi_error_string__ as pmpi_error_string__
#elif !defined(F77_NAME_LOWER_USCORE)
#pragma _CRI duplicate mpi_error_string as pmpi_error_string
#else
#pragma _CRI duplicate mpi_error_string_ as pmpi_error_string_
#endif

/* end of weak pragmas */
#endif

/* Include mapping from MPI->PMPI */
#include "mpiprof.h"
/* Insert the prototypes for the PMPI routines */
#undef __MPI_BINDINGS
#include "binding.h"
#endif

#ifdef F77_NAME_UPPER
#define mpi_error_string_ PMPI_ERROR_STRING
#elif defined(F77_NAME_LOWER_2USCORE)
#define mpi_error_string_ pmpi_error_string__
#elif !defined(F77_NAME_LOWER_USCORE)
#define mpi_error_string_ pmpi_error_string
#else
#define mpi_error_string_ pmpi_error_string_
#endif

#else

#ifdef F77_NAME_UPPER
#define mpi_error_string_ MPI_ERROR_STRING
#elif defined(F77_NAME_LOWER_2USCORE)
#define mpi_error_string_ mpi_error_string__
#elif !defined(F77_NAME_LOWER_USCORE)
#define mpi_error_string_ mpi_error_string
#endif
#endif


#define LOCAL_MIN(a,b) ((a) < (b) ? (a) : (b))

#ifdef _CRAY
void mpi_error_string_( errorcode, string_fcd, resultlen, __ierr )
int*errorcode, *resultlen;
_fcd string_fcd;
int *__ierr;
{
  char cres[MPI_MAX_ERROR_STRING];
  *__ierr = MPI_Error_string(*errorcode,cres,resultlen);
 
  /* Assign the result to the Fortran string doing blank padding as required */
  MPIR_cstr2fstr(_fcdtocp(string_fcd), _fcdlen(string_fcd), cres);
  
  *resultlen = LOCAL_MIN(_fcdlen(string_fcd), *resultlen);
}
#else

/* Prototype to suppress warnings about missing prototypes */
/*
FORTRAN_API void FORT_CALL mpi_error_string_ ( MPI_Fint *, char *, MPI_Fint *, 
                                   MPI_Fint *, MPI_Fint );

FORTRAN_API void FORT_CALL mpi_error_string_( MPI_Fint *errorcode, char *string, MPI_Fint *resultlen, MPI_Fint *__ierr, MPI_Fint d )
*/
/* Prototype to suppress warnings about missing prototypes */
FORTRAN_API void FORT_CALL mpi_error_string_ ( MPI_Fint *, char * FORT_MIXED_LEN_DECL, MPI_Fint *, 
                                   MPI_Fint * FORT_END_LEN_DECL );

FORTRAN_API void FORT_CALL mpi_error_string_( MPI_Fint *errorcode, char *string FORT_MIXED_LEN(d), MPI_Fint *resultlen, MPI_Fint *__ierr FORT_END_LEN(d))
{
  char cres[MPI_MAX_ERROR_STRING];
  int l_resultlen;

  *__ierr = MPI_Error_string((int)*errorcode, cres, &l_resultlen);

  if (*__ierr != MPI_SUCCESS) return;
  /* Assign the result to the Fortran string doing blank padding as required */
  MPIR_cstr2fstr(string, (int)d, cres);
  *resultlen = LOCAL_MIN((MPI_Fint)l_resultlen, (int)d);
}
#endif
