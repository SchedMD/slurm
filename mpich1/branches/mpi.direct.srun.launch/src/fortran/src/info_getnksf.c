/* 
 *   $Id: info_getnksf.c,v 1.4 2001/12/12 23:36:42 ashton Exp $    
 *
 *   Copyright (C) 1997 University of Chicago. 
 *   See COPYRIGHT notice in top-level directory.
 */

#include "mpi_fortimpl.h"


#if defined(MPI_BUILD_PROFILING) || defined(HAVE_WEAK_SYMBOLS)

#if defined(HAVE_WEAK_SYMBOLS)
#if defined(HAVE_PRAGMA_WEAK)
#if defined(F77_NAME_UPPER)
#pragma weak MPI_INFO_GET_NKEYS = PMPI_INFO_GET_NKEYS
void MPI_INFO_GET_NKEYS (MPI_Fint *, MPI_Fint *, MPI_Fint *);
#elif defined(F77_NAME_LOWER_2USCORE)
#pragma weak mpi_info_get_nkeys__ = pmpi_info_get_nkeys__
void mpi_info_get_nkeys__ (MPI_Fint *, MPI_Fint *, MPI_Fint *);
#elif !defined(F77_NAME_LOWER_USCORE)
#pragma weak mpi_info_get_nkeys = pmpi_info_get_nkeys
void mpi_info_get_nkeys (MPI_Fint *, MPI_Fint *, MPI_Fint *);
#else
#pragma weak mpi_info_get_nkeys_ = pmpi_info_get_nkeys_
void mpi_info_get_nkeys_ (MPI_Fint *, MPI_Fint *, MPI_Fint *);
#endif

#elif defined(HAVE_PRAGMA_HP_SEC_DEF)
#if defined(F77_NAME_UPPER)
#pragma _HP_SECONDARY_DEF PMPI_INFO_GET_NKEYS  MPI_INFO_GET_NKEYS
#elif defined(F77_NAME_LOWER_2USCORE)
#pragma _HP_SECONDARY_DEF pmpi_info_get_nkeys__  mpi_info_get_nkeys__
#elif !defined(F77_NAME_LOWER_USCORE)
#pragma _HP_SECONDARY_DEF pmpi_info_get_nkeys  mpi_info_get_nkeys
#else
#pragma _HP_SECONDARY_DEF pmpi_info_get_nkeys_  mpi_info_get_nkeys_
#endif

#elif defined(HAVE_PRAGMA_CRI_DUP)
#if defined(F77_NAME_UPPER)
#pragma _CRI duplicate MPI_INFO_GET_NKEYS as PMPI_INFO_GET_NKEYS
#elif defined(F77_NAME_LOWER_2USCORE)
#pragma _CRI duplicate mpi_info_get_nkeys__ as pmpi_info_get_nkeys__
#elif !defined(F77_NAME_LOWER_USCORE)
#pragma _CRI duplicate mpi_info_get_nkeys as pmpi_info_get_nkeys
#else
#pragma _CRI duplicate mpi_info_get_nkeys_ as pmpi_info_get_nkeys_
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
#define mpi_info_get_nkeys_ PMPI_INFO_GET_NKEYS
#elif defined(F77_NAME_LOWER_2USCORE)
#define mpi_info_get_nkeys_ pmpi_info_get_nkeys__
#elif !defined(F77_NAME_LOWER_USCORE)
#define mpi_info_get_nkeys_ pmpi_info_get_nkeys
#else
#define mpi_info_get_nkeys_ pmpi_info_get_nkeys_
#endif

#else

#ifdef F77_NAME_UPPER
#define mpi_info_get_nkeys_ MPI_INFO_GET_NKEYS
#elif defined(F77_NAME_LOWER_2USCORE)
#define mpi_info_get_nkeys_ mpi_info_get_nkeys__
#elif !defined(F77_NAME_LOWER_USCORE)
#define mpi_info_get_nkeys_ mpi_info_get_nkeys
#endif
#endif


/* Prototype to suppress warnings about missing prototypes */
FORTRAN_API void FORT_CALL mpi_info_get_nkeys_ (MPI_Fint *, MPI_Fint *, MPI_Fint *);

/* Definitions of Fortran Wrapper routines */
FORTRAN_API void FORT_CALL mpi_info_get_nkeys_(MPI_Fint *info, MPI_Fint *nkeys, MPI_Fint *__ierr )
{
    MPI_Info info_c;
    int l_nkeys;
    
    info_c = MPI_Info_f2c(*info);
    *__ierr = MPI_Info_get_nkeys(info_c, &l_nkeys);
    *nkeys = l_nkeys;
}
