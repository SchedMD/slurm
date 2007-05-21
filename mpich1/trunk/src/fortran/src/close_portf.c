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

int MPI_Close_port(char *);
int PMPI_Close_port(char *);

#if defined(MPI_BUILD_PROFILING) || defined(HAVE_WEAK_SYMBOLS)

#if defined(HAVE_WEAK_SYMBOLS)
#if defined(HAVE_PRAGMA_WEAK)
#if defined(F77_NAME_UPPER)
#pragma weak MPI_CLOSE_PORT = PMPI_CLOSE_PORT
void MPI_CLOSE_PORT (char *, MPI_Fint *, MPI_Fint *);
#elif defined(F77_NAME_LOWER_2USCORE)
#pragma weak mpi_close_port__ = pmpi_close_port__
void mpi_close_port__ (char *, MPI_Fint *, MPI_Fint *);
#elif !defined(F77_NAME_LOWER_USCORE)
#pragma weak mpi_close_port = pmpi_close_port
void mpi_close_port ( char *, MPI_Fint *, MPI_Fint *);
#else
#pragma weak mpi_close_port_ = pmpi_close_port_
void mpi_close_port_ ( char *, MPI_Fint *, MPI_Fint *);
#endif

#elif defined(HAVE_PRAGMA_HP_SEC_DEF)
#if defined(F77_NAME_UPPER)
#pragma _HP_SECONDARY_DEF PMPI_CLOSE_PORT  MPI_CLOSE_PORT
#elif defined(F77_NAME_LOWER_2USCORE)
#pragma _HP_SECONDARY_DEF pmpi_close_port__  mpi_close_port__
#elif !defined(F77_NAME_LOWER_USCORE)
#pragma _HP_SECONDARY_DEF pmpi_close_port  mpi_close_port
#else
#pragma _HP_SECONDARY_DEF pmpi_close_port_  mpi_close_port_
#endif

#elif defined(HAVE_PRAGMA_CRI_DUP)
#if defined(F77_NAME_UPPER)
#pragma _CRI duplicate MPI_CLOSE_PORT as PMPI_CLOSE_PORT
#elif defined(F77_NAME_LOWER_2USCORE)
#pragma _CRI duplicate mpi_close_port__ as pmpi_close_port__
#elif !defined(F77_NAME_LOWER_USCORE)
#pragma _CRI duplicate mpi_close_port as pmpi_close_port
#else
#pragma _CRI duplicate mpi_close_port_ as pmpi_close_port_
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
#define mpi_close_port_ PMPI_CLOSE_PORT
#elif defined(F77_NAME_LOWER_2USCORE)
#define mpi_close_port_ pmpi_close_port__
#elif !defined(F77_NAME_LOWER_USCORE)
#define mpi_close_port_ pmpi_close_port
#else
#define mpi_close_port_ pmpi_close_port_
#endif

#else

#ifdef F77_NAME_UPPER
#define mpi_close_port_ MPI_CLOSE_PORT
#elif defined(F77_NAME_LOWER_2USCORE)
#define mpi_close_port_ mpi_close_port__
#elif !defined(F77_NAME_LOWER_USCORE)
#define mpi_close_port_ mpi_close_port
#endif
#endif


#define LOCAL_MIN(a,b) ((a) < (b) ? (a) : (b))

/*
  MPI_OPEN_PORT 
*/
#ifdef _CRAY
void mpi_close_port_(port_name_fcd, ierr)
_fcd port_name_fcd;
int *ierr;
{
    printf("MPI_Close_port not implemented in Fortran on Cray\n");
    *ierr = -1;
}

#else

/* Prototype to suppress warnings about missing prototypes */
FORTRAN_API void FORT_CALL mpi_close_port_ (char * FORT_MIXED_LEN_DECL, 
                                         MPI_Fint * FORT_END_LEN_DECL);

FORTRAN_API void FORT_CALL mpi_close_port_(char *name FORT_MIXED_LEN(d), 
					    MPI_Fint *ierr FORT_END_LEN(d))
{
#ifdef HAVE_MPI_CLOSE_PORT
    char *firstbyte = name; 
    char *lastbyte  = name+d-1;

    /* strip leading blanks */
    while (firstbyte < lastbyte && *firstbyte == ' ') 
	firstbyte ++;
    /* stripping trailing blanks */
    while (lastbyte > firstbyte && *lastbyte == ' ')
	lastbyte --;

    if (lastbyte < name+d-1)
    {
	/* I have room in app's buffer to put '\0' char at lastbyte+1 */
	/* ... we know that the only way there can be room in the app's */
	/* buffer is if *(lastbyte+1) == ' ' so there's no need to "save" */
	/* what was there before overwriting it with '\0'                 */

	*(lastbyte+1) = '\0';
	*ierr = MPI_Close_port(firstbyte);
	*(lastbyte+1) = ' ';
    }
    else
    {
	/* lastbyte+1 is NOT in the app's buffer so I cannot simply */
	/* place '\0' there ... I need to make a copy of name */

	char internal_portname[MPI_MAX_PORT_NAME];
	int nbytes = LOCAL_MIN((MPI_MAX_PORT_NAME-1),(lastbyte-firstbyte+1));
	int i;

	for (i = 0; i < nbytes; i ++) 
	    internal_portname[i] = lastbyte[i];
	internal_portname[nbytes] = '\0';
	*ierr = MPI_Close_port(internal_portname);

    } /* endif */
#else
    /* Internal error - unimplemented */
 {
     int mpi_errno;
     mpi_errno = MPIR_Err_setmsg( MPI_ERR_INTERN, MPIR_ERR_DEFAULT, 
				  "MPI_CLOSE_PORT", (char *)0, (char *)0 );
     *ierr = MPIR_ERROR( MPIR_COMM_WORLD, mpi_errno, "MPI_CLOSE_PORT" );
 }
#endif


} /* end mpi_close_port_() */

#endif
