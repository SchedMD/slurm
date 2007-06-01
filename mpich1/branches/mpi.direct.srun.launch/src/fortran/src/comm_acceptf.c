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


int MPI_Comm_accept(char *port_name, 
		    MPI_Info info, 
		    int root, 
		    MPI_Comm comm, 
		    MPI_Comm *newcomm);

#if defined(MPI_BUILD_PROFILING) || defined(HAVE_WEAK_SYMBOLS)

#if defined(HAVE_WEAK_SYMBOLS)
#if defined(HAVE_PRAGMA_WEAK)
#if defined(F77_NAME_UPPER)
#pragma weak MPI_COMM_ACCEPT = PMPI_COMM_ACCEPT
void MPI_COMM_ACCEPT (char *, MPI_Fint *, MPI_Fint *, MPI_Fint *, 
			MPI_Fint *, MPI_Fint *, MPI_Fint *);
#elif defined(F77_NAME_LOWER_2USCORE)
#pragma weak mpi_comm_accept__ = pmpi_comm_accept__
void mpi_open_port__ (char *, MPI_Fint *, MPI_Fint *, MPI_Fint *,
			MPI_Fint *, MPI_Fint *, MPI_Fint *);
#elif !defined(F77_NAME_LOWER_USCORE)
#pragma weak mpi_comm_accept = pmpi_comm_accept
void mpi_comm_accept (char *, MPI_Fint *, MPI_Fint *, MPI_Fint *,
			MPI_Fint *, MPI_Fint *, MPI_Fint *);
#else
#pragma weak mpi_comm_accept_ = pmpi_comm_accept_
void mpi_comm_accept_ (char *, MPI_Fint *, MPI_Fint *, MPI_Fint *,
			MPI_Fint *, MPI_Fint *, MPI_Fint *);
#endif

#elif defined(HAVE_PRAGMA_HP_SEC_DEF)
#if defined(F77_NAME_UPPER)
#pragma _HP_SECONDARY_DEF PMPI_COMM_ACCEPT  MPI_COMM_ACCEPT
#elif defined(F77_NAME_LOWER_2USCORE)
#pragma _HP_SECONDARY_DEF pmpi_comm_accept__  mpi_comm_accept__
#elif !defined(F77_NAME_LOWER_USCORE)
#pragma _HP_SECONDARY_DEF pmpi_comm_accept  mpi_comm_accept
#else
#pragma _HP_SECONDARY_DEF pmpi_comm_accept_  mpi_comm_accept_
#endif

#elif defined(HAVE_PRAGMA_CRI_DUP)
#if defined(F77_NAME_UPPER)
#pragma _CRI duplicate MPI_COMM_ACCEPT as PMPI_COMM_ACCEPT
#elif defined(F77_NAME_LOWER_2USCORE)
#pragma _CRI duplicate mpi_comm_accept__ as pmpi_comm_accept__
#elif !defined(F77_NAME_LOWER_USCORE)
#pragma _CRI duplicate mpi_comm_accept as pmpi_comm_accept
#else
#pragma _CRI duplicate mpi_comm_accept_ as pmpi_comm_accept_
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
#define mpi_comm_accept_ PMPI_COMM_ACCEPT
#elif defined(F77_NAME_LOWER_2USCORE)
#define mpi_comm_accept_ pmpi_comm_accept__
#elif !defined(F77_NAME_LOWER_USCORE)
#define mpi_comm_accept_ pmpi_comm_accept
#else
#define mpi_comm_accept_ pmpi_comm_accept_
#endif

#else

#ifdef F77_NAME_UPPER
#define mpi_comm_accept_ MPI_COMM_ACCEPT
#elif defined(F77_NAME_LOWER_2USCORE)
#define mpi_comm_accept_ mpi_comm_accept__
#elif !defined(F77_NAME_LOWER_USCORE)
#define mpi_comm_accept_ mpi_comm_accept
#endif
#endif


#define LOCAL_MIN(a,b) ((a) < (b) ? (a) : (b))

/*
  MPI_OPEN_PORT 
*/
#ifdef _CRAY
void mpi_comm_accept_(port_name_fcd, info, root, oldcomm, newcomm, ierr)
_fcd port_name_fcd;
int *info;
int *root;
int *oldcomm, *newcomm;
int *ierr;
{
    printf("MPI_Comm_accept not implemented in Fortran on Cray\n");
    *ierr = -1;
}

#else
/* Prototype to suppress warnings about missing prototypes */
FORTRAN_API void FORT_CALL mpi_comm_accept_ (char * FORT_MIXED_LEN_DECL, 
					    MPI_Fint *,  MPI_Fint *,  
					    MPI_Fint *,  MPI_Fint *,  
                                         MPI_Fint * FORT_END_LEN_DECL);

FORTRAN_API void FORT_CALL mpi_comm_accept_(char *name FORT_MIXED_LEN(d), 
	MPI_Fint *info,
	MPI_Fint *root,
	MPI_Fint *intra_comm,
	MPI_Fint *newcomm,
	MPI_Fint *ierr FORT_END_LEN(d))
{
#ifdef HAVE_MPI_COMM_ACCEPT
    char *firstbyte = name;
    char *lastbyte  = name+d-1;
    MPI_Comm l_comm_out;

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
        *ierr = MPI_Comm_accept(firstbyte, 
				(MPI_Info) (*info),
				(int) *root,
				MPI_Comm_f2c(*intra_comm),
				&l_comm_out);
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
        *ierr = MPI_Comm_accept(internal_portname, 
				(MPI_Info) (*info),
				(int) *root,
				MPI_Comm_f2c(*intra_comm),
				&l_comm_out);

    } /* endif */

    if (*ierr == MPI_SUCCESS)
	*newcomm = MPI_Comm_c2f(l_comm_out);
#else
    /* Internal error - unimplemented */
 {
     int mpi_errno;
     mpi_errno = MPIR_Err_setmsg( MPI_ERR_INTERN, MPIR_ERR_DEFAULT, 
				  "MPI_COMM_ACCEPT", (char *)0, (char *)0 );
     *ierr = MPIR_ERROR( MPIR_COMM_WORLD, mpi_errno, "MPI_COMM_ACCEPT" );
 }
#endif

} /* end mpi_comm_accept_() */

#endif
