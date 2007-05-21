/*
 *  $Id: comm_namegetf.c,v 1.5 2001/12/12 23:36:30 ashton Exp $
 *
 *  (C) 1996 by Argonne National Laboratory and Mississipi State University.
 *      See COPYRIGHT in top-level directory.
 */
/* Update log
 *
 * Nov 29 1996 jcownie@dolphinics.com: Implement MPI-2 Fortran binding for 
 *        communicator naming function.
 */

#include "mpi_fortimpl.h"
#ifdef _CRAY
#include "fortran.h"
#endif


#if defined(MPI_BUILD_PROFILING) || defined(HAVE_WEAK_SYMBOLS)

#if defined(HAVE_WEAK_SYMBOLS)
#if defined(HAVE_PRAGMA_WEAK)
#if defined(F77_NAME_UPPER)
#pragma weak MPI_COMM_GET_NAME = PMPI_COMM_GET_NAME
void MPI_COMM_GET_NAME ( MPI_Fint *, char *, MPI_Fint *, MPI_Fint *, MPI_Fint );
#elif defined(F77_NAME_LOWER_2USCORE)
#pragma weak mpi_comm_get_name__ = pmpi_comm_get_name__
void mpi_comm_get_name__ ( MPI_Fint *, char *, MPI_Fint *, MPI_Fint *, MPI_Fint );
#elif !defined(F77_NAME_LOWER_USCORE)
#pragma weak mpi_comm_get_name = pmpi_comm_get_name
void mpi_comm_get_name ( MPI_Fint *, char *, MPI_Fint *, MPI_Fint *, MPI_Fint );
#else
#pragma weak mpi_comm_get_name_ = pmpi_comm_get_name_
void mpi_comm_get_name_ ( MPI_Fint *, char *, MPI_Fint *, MPI_Fint *, MPI_Fint );
#endif

#elif defined(HAVE_PRAGMA_HP_SEC_DEF)
#if defined(F77_NAME_UPPER)
#pragma _HP_SECONDARY_DEF PMPI_COMM_GET_NAME  MPI_COMM_GET_NAME
#elif defined(F77_NAME_LOWER_2USCORE)
#pragma _HP_SECONDARY_DEF pmpi_comm_get_name__  mpi_comm_get_name__
#elif !defined(F77_NAME_LOWER_USCORE)
#pragma _HP_SECONDARY_DEF pmpi_comm_get_name  mpi_comm_get_name
#else
#pragma _HP_SECONDARY_DEF pmpi_comm_get_name_  mpi_comm_get_name_
#endif

#elif defined(HAVE_PRAGMA_CRI_DUP)
#if defined(F77_NAME_UPPER)
#pragma _CRI duplicate MPI_COMM_GET_NAME as PMPI_COMM_GET_NAME
#elif defined(F77_NAME_LOWER_2USCORE)
#pragma _CRI duplicate mpi_comm_get_name__ as pmpi_comm_get_name__
#elif !defined(F77_NAME_LOWER_USCORE)
#pragma _CRI duplicate mpi_comm_get_name as pmpi_comm_get_name
#else
#pragma _CRI duplicate mpi_comm_get_name_ as pmpi_comm_get_name_
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
#define mpi_comm_get_name_ PMPI_COMM_GET_NAME
#elif defined(F77_NAME_LOWER_2USCORE)
#define mpi_comm_get_name_ pmpi_comm_get_name__
#elif !defined(F77_NAME_LOWER_USCORE)
#define mpi_comm_get_name_ pmpi_comm_get_name
#else
#define mpi_comm_get_name_ pmpi_comm_get_name_
#endif

#else

#ifdef F77_NAME_UPPER
#define mpi_comm_get_name_ MPI_COMM_GET_NAME
#elif defined(F77_NAME_LOWER_2USCORE)
#define mpi_comm_get_name_ mpi_comm_get_name__
#elif !defined(F77_NAME_LOWER_USCORE)
#define mpi_comm_get_name_ mpi_comm_get_name
#endif
#endif


#ifdef _CRAY

void mpi_comm_get_name_( comm, string_fcd, nml, __ierr )
MPI_Comm *comm;
_fcd string_fcd;
int *nml;
int *__ierr;
{
  char cres [MPI_MAX_NAME_STRING];

  *__ierr = MPI_Comm_get_name( *comm, cres, nml);
  if (*nml > _fcdlen(string_fcd))
    *nml = _fcdlen(string_fcd);

  /* Assign the result to the Fortran string doing blank padding as required */
  MPIR_cstr2fstr(_fcdtocp(string_fcd), _fcdlen(string_fcd), cres);
}
#else

/* Prototype to suppress warnings about missing prototypes */
/*
FORTRAN_API void FORT_CALL mpi_comm_get_name_ ( MPI_Fint *, char *, MPI_Fint *, 
                                    MPI_Fint *, MPI_Fint );

FORTRAN_API void FORT_CALL mpi_comm_get_name_( MPI_Fint *comm, char *string, MPI_Fint *reslen, MPI_Fint *__ierr, MPI_Fint d )
*/
FORTRAN_API void FORT_CALL mpi_comm_get_name_ ( MPI_Fint *, char * FORT_MIXED_LEN_DECL, MPI_Fint *, 
                                    MPI_Fint * FORT_END_LEN_DECL );

FORTRAN_API void FORT_CALL mpi_comm_get_name_( MPI_Fint *comm, char *string FORT_MIXED_LEN(d), MPI_Fint *reslen, MPI_Fint *__ierr FORT_END_LEN(d) )
{
  char cres [MPI_MAX_NAME_STRING];
  int l_reslen;

  *__ierr = MPI_Comm_get_name(MPI_Comm_f2c(*comm), cres, &l_reslen);
  if (*__ierr != MPI_SUCCESS) return;
  *reslen = l_reslen;

  if (*reslen > (long)d)
    *reslen = (long)d;

  /* Assign the result to the Fortran string doing blank padding as required */
  MPIR_cstr2fstr(string,(long)d,cres);

}
#endif
