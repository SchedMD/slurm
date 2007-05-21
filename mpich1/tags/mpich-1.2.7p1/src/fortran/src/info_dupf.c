/* 
 *   $Id: info_dupf.c,v 1.5 2001/12/12 23:36:41 ashton Exp $    
 *
 *   Copyright (C) 1997 University of Chicago. 
 *   See COPYRIGHT notice in top-level directory.
 */

#include "mpi_fortimpl.h"


#if defined(MPI_BUILD_PROFILING) || defined(HAVE_WEAK_SYMBOLS)

#if defined(HAVE_WEAK_SYMBOLS)
#if defined(HAVE_PRAGMA_WEAK)
#if defined(F77_NAME_UPPER)
#pragma weak MPI_INFO_DUP = PMPI_INFO_DUP
void MPI_INFO_DUP (MPI_Fint *, MPI_Fint *, MPI_Fint *);
#elif defined(F77_NAME_LOWER_2USCORE)
#pragma weak mpi_info_dup__ = pmpi_info_dup__
void mpi_info_dup__ (MPI_Fint *, MPI_Fint *, MPI_Fint *);
#elif !defined(F77_NAME_LOWER_USCORE)
#pragma weak mpi_info_dup = pmpi_info_dup
void mpi_info_dup (MPI_Fint *, MPI_Fint *, MPI_Fint *);
#else
#pragma weak mpi_info_dup_ = pmpi_info_dup_
void mpi_info_dup_ (MPI_Fint *, MPI_Fint *, MPI_Fint *);
#endif

#elif defined(HAVE_PRAGMA_HP_SEC_DEF)
#if defined(F77_NAME_UPPER)
#pragma _HP_SECONDARY_DEF PMPI_INFO_DUP  MPI_INFO_DUP
#elif defined(F77_NAME_LOWER_2USCORE)
#pragma _HP_SECONDARY_DEF pmpi_info_dup__  mpi_info_dup__
#elif !defined(F77_NAME_LOWER_USCORE)
#pragma _HP_SECONDARY_DEF pmpi_info_dup  mpi_info_dup
#else
#pragma _HP_SECONDARY_DEF pmpi_info_dup_  mpi_info_dup_
#endif

#elif defined(HAVE_PRAGMA_CRI_DUP)
#if defined(F77_NAME_UPPER)
#pragma _CRI duplicate MPI_INFO_DUP as PMPI_INFO_DUP
#elif defined(F77_NAME_LOWER_2USCORE)
#pragma _CRI duplicate mpi_info_dup__ as pmpi_info_dup__
#elif !defined(F77_NAME_LOWER_USCORE)
#pragma _CRI duplicate mpi_info_dup as pmpi_info_dup
#else
#pragma _CRI duplicate mpi_info_dup_ as pmpi_info_dup_
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
#define mpi_info_dup_ PMPI_INFO_DUP
#elif defined(F77_NAME_LOWER_2USCORE)
#define mpi_info_dup_ pmpi_info_dup__
#elif !defined(F77_NAME_LOWER_USCORE)
#define mpi_info_dup_ pmpi_info_dup
#else
#define mpi_info_dup_ pmpi_info_dup_
#endif

#else

#ifdef F77_NAME_UPPER
#define mpi_info_dup_ MPI_INFO_DUP
#elif defined(F77_NAME_LOWER_2USCORE)
#define mpi_info_dup_ mpi_info_dup__
#elif !defined(F77_NAME_LOWER_USCORE)
#define mpi_info_dup_ mpi_info_dup
#endif
#endif


/* Prototype to suppress warnings about missing prototypes */
FORTRAN_API void FORT_CALL mpi_info_dup_ (MPI_Fint *, MPI_Fint *, MPI_Fint *);

/* Definitions of Fortran Wrapper routines */
FORTRAN_API void FORT_CALL mpi_info_dup_(MPI_Fint *info, MPI_Fint *newinfo, MPI_Fint *__ierr )
{
    MPI_Info info_c, newinfo_c;

    info_c = MPI_Info_f2c(*info);
    *__ierr = MPI_Info_dup(info_c, &newinfo_c);
    if (*__ierr == MPI_SUCCESS) 		     
        *newinfo = MPI_Info_c2f(newinfo_c);
}
