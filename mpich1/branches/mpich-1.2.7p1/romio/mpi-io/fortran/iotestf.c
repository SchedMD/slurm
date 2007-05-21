/* -*- Mode: C; c-basic-offset:4 ; -*- */
/* 
 *
 *   Copyright (C) 1997 University of Chicago. 
 *   See COPYRIGHT notice in top-level directory.
 */

#include "adio.h"
#include "mpio.h"

#if defined(MPIO_BUILD_PROFILING) || defined(HAVE_WEAK_SYMBOLS)

#if defined(HAVE_WEAK_SYMBOLS)
#if defined(HAVE_PRAGMA_WEAK)
#if defined(FORTRANCAPS)
FORTRAN_API void FORT_CALL MPIO_TEST(MPI_Fint *request,int *flag,MPI_Status *status, int *ierr );
#pragma weak MPIO_TEST = PMPIO_TEST
#elif defined(FORTRANDOUBLEUNDERSCORE)
FORTRAN_API void FORT_CALL mpio_test__(MPI_Fint *request,int *flag,MPI_Status *status, int *ierr );
#pragma weak mpio_test__ = pmpio_test__
#elif !defined(FORTRANUNDERSCORE)
FORTRAN_API void FORT_CALL mpio_test(MPI_Fint *request,int *flag,MPI_Status *status, int *ierr );
#pragma weak mpio_test = pmpio_test
#else
FORTRAN_API void FORT_CALL mpio_test_(MPI_Fint *request,int *flag,MPI_Status *status, int *ierr );
#pragma weak mpio_test_ = pmpio_test_
#endif

#elif defined(HAVE_PRAGMA_HP_SEC_DEF)
#if defined(FORTRANCAPS)
#pragma _HP_SECONDARY_DEF PMPIO_TEST MPIO_TEST
#elif defined(FORTRANDOUBLEUNDERSCORE)
#pragma _HP_SECONDARY_DEF pmpio_test__ mpio_test__
#elif !defined(FORTRANUNDERSCORE)
#pragma _HP_SECONDARY_DEF pmpio_test mpio_test
#else
#pragma _HP_SECONDARY_DEF pmpio_test_ mpio_test_
#endif

#elif defined(HAVE_PRAGMA_CRI_DUP)
#if defined(FORTRANCAPS)
#pragma _CRI duplicate MPIO_TEST as PMPIO_TEST
#elif defined(FORTRANDOUBLEUNDERSCORE)
#pragma _CRI duplicate mpio_test__ as pmpio_test__
#elif !defined(FORTRANUNDERSCORE)
#pragma _CRI duplicate mpio_test as pmpio_test
#else
#pragma _CRI duplicate mpio_test_ as pmpio_test_
#endif

/* end of weak pragmas */
#endif
/* Include mapping from MPI->PMPI */
#include "mpioprof.h"
#endif

#ifdef FORTRANCAPS
#define mpio_test_ PMPIO_TEST
#elif defined(FORTRANDOUBLEUNDERSCORE)
#define mpio_test_ pmpio_test__
#elif !defined(FORTRANUNDERSCORE)
#if defined(HPUX) || defined(SPPUX)
#pragma _HP_SECONDARY_DEF pmpio_test pmpio_test_
#endif
#define mpio_test_ pmpio_test
#else
#if defined(HPUX) || defined(SPPUX)
#pragma _HP_SECONDARY_DEF pmpio_test_ pmpio_test
#endif
#define mpio_test_ pmpio_test_
#endif

#else

#ifdef FORTRANCAPS
#define mpio_test_ MPIO_TEST
#elif defined(FORTRANDOUBLEUNDERSCORE)
#define mpio_test_ mpio_test__
#elif !defined(FORTRANUNDERSCORE)
#if defined(HPUX) || defined(SPPUX)
#pragma _HP_SECONDARY_DEF mpio_test mpio_test_
#endif
#define mpio_test_ mpio_test
#else
#if defined(HPUX) || defined(SPPUX)
#pragma _HP_SECONDARY_DEF mpio_test_ mpio_test
#endif
#endif
#endif

/* Prototype to keep compiler happy */
FORTRAN_API void FORT_CALL mpio_test_(MPI_Fint *request,MPI_Fint *flag,MPI_Status *status, MPI_Fint *ierr );

FORTRAN_API void FORT_CALL mpio_test_(MPI_Fint *request,MPI_Fint *flag,MPI_Status *status, MPI_Fint *ierr )
{
    MPIO_Request req_c;
    
    req_c = MPIO_Request_f2c(*request);
    *ierr = MPIO_Test(&req_c,flag,status);
    *request = MPIO_Request_c2f(req_c);
}
