#include "mpi_fortimpl.h"
#ifdef _CRAY
#include <fortran.h>
#endif

#ifdef HAVE_STDLIB_H
#include <stdlib.h>
#endif
#ifdef HAVE_STRING_H
/* for strlen */
#include <string.h>
#endif

int MPI_Open_port(MPI_Info, char *);

#if defined(MPI_BUILD_PROFILING) || defined(HAVE_WEAK_SYMBOLS)

#if defined(HAVE_WEAK_SYMBOLS)
#if defined(HAVE_PRAGMA_WEAK)
#if defined(F77_NAME_UPPER)
#pragma weak MPI_OPEN_PORT = PMPI_OPEN_PORT
void MPI_OPEN_PORT (MPI_Fint *,  char *, MPI_Fint *, MPI_Fint *);
#elif defined(F77_NAME_LOWER_2USCORE)
#pragma weak mpi_open_port__ = pmpi_open_port__
void mpi_open_port__ (MPI_Fint *, char *, MPI_Fint *, MPI_Fint *);
#elif !defined(F77_NAME_LOWER_USCORE)
#pragma weak mpi_open_port = pmpi_open_port
void mpi_open_port ( MPI_Fint *, char *, MPI_Fint *, MPI_Fint *);
#else
#pragma weak mpi_open_port_ = pmpi_open_port_
void mpi_open_port_ ( MPI_Fint *, char *, MPI_Fint *, MPI_Fint *);
#endif

#elif defined(HAVE_PRAGMA_HP_SEC_DEF)
#if defined(F77_NAME_UPPER)
#pragma _HP_SECONDARY_DEF PMPI_OPEN_PORT  MPI_OPEN_PORT
#elif defined(F77_NAME_LOWER_2USCORE)
#pragma _HP_SECONDARY_DEF pmpi_open_port__  mpi_open_port__
#elif !defined(F77_NAME_LOWER_USCORE)
#pragma _HP_SECONDARY_DEF pmpi_open_port  mpi_open_port
#else
#pragma _HP_SECONDARY_DEF pmpi_open_port_  mpi_open_port_
#endif

#elif defined(HAVE_PRAGMA_CRI_DUP)
#if defined(F77_NAME_UPPER)
#pragma _CRI duplicate MPI_OPEN_PORT as PMPI_OPEN_PORT
#elif defined(F77_NAME_LOWER_2USCORE)
#pragma _CRI duplicate mpi_open_port__ as pmpi_open_port__
#elif !defined(F77_NAME_LOWER_USCORE)
#pragma _CRI duplicate mpi_open_port as pmpi_open_port
#else
#pragma _CRI duplicate mpi_open_port_ as pmpi_open_port_
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
#define mpi_open_port_ PMPI_OPEN_PORT
#elif defined(F77_NAME_LOWER_2USCORE)
#define mpi_open_port_ pmpi_open_port__
#elif !defined(F77_NAME_LOWER_USCORE)
#define mpi_open_port_ pmpi_open_port
#else
#define mpi_open_port_ pmpi_open_port_
#endif

#else

#ifdef F77_NAME_UPPER
#define mpi_open_port_ MPI_OPEN_PORT
#elif defined(F77_NAME_LOWER_2USCORE)
#define mpi_open_port_ mpi_open_port__
#elif !defined(F77_NAME_LOWER_USCORE)
#define mpi_open_port_ mpi_open_port
#endif
#endif


#define LOCAL_MIN(a,b) ((a) < (b) ? (a) : (b))

/*
  MPI_OPEN_PORT 
*/
#ifdef _CRAY
void mpi_open_port_(info, port_name_fcd, ierr)
int *info,
_fcd port_name_fcd;
int *ierr;
{
    printf("MPI_Open_port not implemented in Fortran on Cray\n");
    *ierr = -1;
}

#else
/* Prototype to suppress warnings about missing prototypes */
FORTRAN_API void FORT_CALL mpi_open_port_ (MPI_Fint *,  
					    char * FORT_MIXED_LEN_DECL, 
                                         MPI_Fint * FORT_END_LEN_DECL);

FORTRAN_API void FORT_CALL mpi_open_port_(MPI_Fint *info,
	char *name FORT_MIXED_LEN(d), 
	MPI_Fint *ierr FORT_END_LEN(d))
{
#ifdef HAVE_MPI_OPEN_PORT
    char internal_portname[MPI_MAX_PORT_NAME];
    *ierr = MPI_Open_port((MPI_Info) (*info), internal_portname);
    /* This handles blank padding required by Fortran */
    MPIR_cstr2fstr( name, (int)d, internal_portname );
#else
    /* Internal error - unimplemented */
 {
     int mpi_errno;
     mpi_errno = MPIR_Err_setmsg( MPI_ERR_INTERN, MPIR_ERR_DEFAULT, 
				  "MPI_OPEN_PORT", (char *)0, (char *)0 );
     *ierr = MPIR_ERROR( MPIR_COMM_WORLD, mpi_errno, "MPI_OPEN_PORT" );
 }
#endif

} /* end mpi_open_port_() */

#endif
