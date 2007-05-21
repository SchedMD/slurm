/*
 *  $Id: initfutil.c,v 1.19 2004/07/26 18:26:13 gropp Exp $
 *
 *  (C) 2000 by Argonne National Laboratory.
 *      See COPYRIGHT in top-level directory.
 */

/* Include the configure definitions now (these may be needed for some
   varieties of stdlib or stdio) */
#ifndef MPICHCONF_INC
#define MPICHCONF_INC
#include "mpichconf.h"
#endif
/*
 * This file contains that routines that support the Fortran interface.
 * In combines routines found in initutil.c and initdte.c
 */
#include "mpi_fortimpl.h"
#include "mpi_fortran.h"

/* The following are needed to define the datatypes.  This needs to 
   be abstracted out of the datatype support */
#define MPIR_HAS_COOKIES
#include "cookie.h"
#include "datatype.h"
#ifndef FPRINTF
#define FPRINTF fprintf
#endif
#ifdef HAVE_STDLIB_H
#include <stdlib.h>
#endif
#include <stdio.h>

/* Use the PMPI names for routines */
#define MPI_BUILD_PROFILING
#include "mpiprof.h"

#define DEBUG(a)

#ifdef F77_NAME_UPPER
#define mpir_init_fcm_   MPIR_INIT_FCM
#define mpir_init_flog_  MPIR_INIT_FLOG
#define mpir_init_bottom_ MPIR_INIT_BOTTOM
#elif defined(F77_NAME_LOWER_2USCORE)
#define mpir_init_fcm_   mpir_init_fcm__
#define mpir_init_flog_  mpir_init_flog__
#define mpir_init_bottom_ mpir_init_bottom__
#elif defined(F77_NAME_LOWER) || defined(F77_NAME_MIXED)
#define mpir_init_fcm_   mpir_init_fcm
#define mpir_init_flog_  mpir_init_flog
#define mpir_init_bottom_ mpir_init_bottom
#endif

/* Datatype sizes */
#ifdef F77_NAME_UPPER
#define mpir_init_fdtes_ MPIR_INIT_FDTES
#define mpir_init_fsize_  MPIR_INIT_FSIZE
#define mpir_get_fsize_   MPIR_GET_FSIZE
#elif defined(F77_NAME_LOWER_2USCORE)
#define mpir_init_fdtes_ mpir_init_fdtes__
#define mpir_init_fsize_  mpir_init_fsize__
#define mpir_get_fsize_   mpir_get_fsize__
#elif defined(F77_NAME_LOWER) || defined(F77_NAME_MIXED)
#define mpir_init_fdtes_ mpir_init_fdtes
#define mpir_init_fsize_  mpir_init_fsize
#define mpir_get_fsize_   mpir_get_fsize
#endif


/* Prototypes for Fortran interface functions */

/* Find the address of MPI_BOTTOM, MPI_STATUS_IGNORE, and 
   MPI_STATUSES_IGNORE */
void mpir_init_fcm_ ( void );
void mpir_init_bottom_ ( void * );

/* The following functions are needed only when cross-compiling */
#ifndef F77_TRUE_VALUE_SET
void mpir_init_flog_ ( MPI_Fint *, MPI_Fint * );
#endif

void mpir_init_fsize_ ( float *, float *, double *, double * );
#if SIZEOF_F77_REAL == 0 || SIZEOF_F77_DOUBLE_PRECISION == 0 
void mpir_get_fsize_ (  void );
#endif

/* Static space for predefined Fortran datatypes */
struct MPIR_DATATYPE        MPIR_I_2INTEGER, 
                            MPIR_I_REAL, MPIR_I_DOUBLE_PRECISION, 
                            MPIR_I_COMPLEX, MPIR_I_DCOMPLEX, 
                            MPIR_I_LOGICAL, MPIR_I_INTEGER;

extern struct MPIR_DATATYPE MPIR_I_2FLOAT, MPIR_I_2DOUBLE;

/* Fortran logical values */
#ifdef F77_TRUE_VALUE_SET
MPI_Fint MPIR_F_TRUE = F77_TRUE_VALUE, MPIR_F_FALSE = F77_FALSE_VALUE;
#else
MPI_Fint MPIR_F_TRUE, MPIR_F_FALSE;
#endif

/* 
 Location of the Fortran marker for MPI_BOTTOM.  The Fortran wrappers
 must detect the use of this address and replace it with MPI_BOTTOM.
 This is done by the macro MPIR_F_PTR.
 */
void *MPIR_F_MPI_BOTTOM = 0;
/* Here are the special status ignore values in MPI-2 */
void *MPIR_F_STATUS_IGNORE = 0;
void *MPIR_F_STATUSES_IGNORE = 0;

/* Fortran datatypes */
/* MPI_Datatype MPIR_logical_dte; */
struct MPIR_DATATYPE MPIR_int1_dte, MPIR_int2_dte, MPIR_int4_dte, 
		MPIR_real4_dte, MPIR_real8_dte;
/* FORTRAN Datatypes for MINLOC and MAXLOC functions */
struct MPIR_DATATYPE MPIR_I_2REAL, MPIR_I_2DOUBLE_PRECISION, 
                     MPIR_I_2COMPLEX, MPIR_I_2DCOMPLEX;

/* Sizes of Fortran types; computed when initialized if necessary. */
/* static int MPIR_FSIZE_C = 0;  */                          /* Characters */
static int MPIR_FSIZE_R = SIZEOF_F77_REAL;              /* Reals */
static int MPIR_FSIZE_D = SIZEOF_F77_DOUBLE_PRECISION;  /* Doubles */

void MPIR_Setup_base_datatype (MPI_Datatype, struct MPIR_DATATYPE *, 
					 MPIR_NODETYPE, int);
void MPIR_Type_contiguous ( int, MPI_Datatype, 
				      struct MPIR_DATATYPE *, MPI_Datatype );

#ifdef MPID_NO_FORTRAN
int MPIR_InitFortran( void )
{
    return 0;
}
int MPIR_InitFortranDatatypes( void )
{
    return 0;
}
void MPIR_Free_Fortran_dtes( void )
{
}
#else
int MPIR_InitFortran( void )
{
    int *attr_ptr, flag, i;
    MPI_Aint attr_val;
    /* Create the attribute values */

    /* Do the Fortran versions - Pass the actual value.  Note that these
       use MPIR_Keyval_create with the "is_fortran" flag set. 
       If you change these; change the removal in finalize.c. */
#define NULL_COPY (MPI_Copy_function *)0
#define NULL_DEL  (MPI_Delete_function*)0
        i = MPIR_TAG_UB;
    MPIR_Keyval_create( NULL_COPY, NULL_DEL, &i, (void *)0, 1 );
        i = MPIR_HOST;
    MPIR_Keyval_create( NULL_COPY, NULL_DEL, &i, (void *)0, 1 );
        i = MPIR_IO;
    MPIR_Keyval_create( NULL_COPY, NULL_DEL, &i, (void *)0, 1 );
        i = MPIR_WTIME_IS_GLOBAL;
    MPIR_Keyval_create( NULL_COPY, NULL_DEL, &i, (void *)0, 1 );

    /* We need to switch this to the MPI-2 version to handle different
       word lengths */
    /* Attr_get needs to be referenced from MPI_Init so that we can
       use it here */
    MPI_Attr_get( MPI_COMM_WORLD, MPI_TAG_UB, &attr_ptr, &flag );
    attr_val = (MPI_Aint) *attr_ptr;
    MPI_Attr_put( MPI_COMM_WORLD, MPIR_TAG_UB, (void*)attr_val );
    MPI_Attr_get( MPI_COMM_WORLD, MPI_HOST, &attr_ptr, &flag );
    attr_val = (MPI_Aint) *attr_ptr;
    MPI_Attr_put( MPI_COMM_WORLD, MPIR_HOST,   (void*)attr_val );
    MPI_Attr_get( MPI_COMM_WORLD, MPI_IO, &attr_ptr, &flag );
    attr_val = (MPI_Aint) *attr_ptr;
    MPI_Attr_put( MPI_COMM_WORLD, MPIR_IO,     (void*)attr_val );
    MPI_Attr_get( MPI_COMM_WORLD, MPI_WTIME_IS_GLOBAL, &attr_ptr, &flag );
    attr_val = (MPI_Aint) *attr_ptr;
    MPI_Attr_put( MPI_COMM_WORLD, MPIR_WTIME_IS_GLOBAL, (void*)attr_val );

    MPIR_Attr_make_perm( MPIR_TAG_UB );
    MPIR_Attr_make_perm( MPIR_HOST );
    MPIR_Attr_make_perm( MPIR_IO );
    MPIR_Attr_make_perm( MPIR_WTIME_IS_GLOBAL );

#ifndef F77_TRUE_VALUE_SET
    mpir_init_flog_( &MPIR_F_TRUE, &MPIR_F_FALSE );
#endif
    /* fcm sets MPI_BOTTOM */
    mpir_init_fcm_( );

    return 0;
}
void MPIR_Free_Fortran_keyvals( void )
{
    int tmp;
    tmp = MPIR_TAG_UB;
    MPI_Keyval_free( &tmp );
    tmp = MPIR_HOST;
    MPI_Keyval_free( &tmp );
    tmp = MPIR_IO;
    MPI_Keyval_free( &tmp );
    tmp = MPIR_WTIME_IS_GLOBAL;
    MPI_Keyval_free( &tmp );
}

/* 
   This routine is CALLED by MPIR_init_fcm to provide the address of 
   the Fortran MPI_BOTTOM to C 
 */ 
void mpir_init_bottom_( void *p )
{
    MPIR_F_MPI_BOTTOM	   = p;
    MPIR_F_STATUS_IGNORE   = ((MPI_Fint*)p) + 1;
    MPIR_F_STATUSES_IGNORE = ((MPI_Fint*)p) + 2;
}

int MPIR_InitFortranDatatypes( void )
{
    MPIR_NODETYPE  nodetype;
    /* 
       Fortran requires that integers be the same size as 
       REALs, which are half the size of doubles.  Note that
       logicals must be the same size as integers.  Note that
       much of the code does not know about MPIR_LOGICAL or MPIR_FORT_INT
       yet. 

       We still need a FORT_REAL and FORT_DOUBLE type for some systems
     */
#if SIZEOF_F77_REAL == 0 || SIZEOF_F77_DOUBLE_PRECISION == 0 
    mpir_get_fsize_();
#endif
    /* Rather than try to duplicate the Fortran types (e.g., 
       MPI_INTEGER = MPI_INT), we just generate new types
     */
    nodetype = MPIR_FORT_INT;
    if (sizeof(int) == MPIR_FSIZE_R) nodetype = MPIR_INT;
    else if (sizeof(long) == MPIR_FSIZE_R) nodetype = MPIR_LONG;
#ifdef HAVE_LONG_LONG
    else if (sizeof(long long) == MPIR_FSIZE_R) nodetype = MPIR_LONGLONGINT;
#endif
    MPIR_Setup_base_datatype( MPI_INTEGER, &MPIR_I_INTEGER, nodetype, 
			      MPIR_FSIZE_R );
    MPIR_Setup_base_datatype( MPI_LOGICAL, &MPIR_I_LOGICAL, MPIR_LOGICAL, 
			      MPIR_FSIZE_R );
    MPIR_Setup_base_datatype( MPI_COMPLEX, &MPIR_I_COMPLEX, 
			      MPIR_COMPLEX, 2 * MPIR_FSIZE_R );
    MPIR_I_COMPLEX.align  = MPIR_FSIZE_R;

    /* Hunt for Fortran real size */
    /* The original code here depended on Fortran being correctly 
       implemented.  Unfortunately, some vendors (e.g., Cray for the T3x)
       choose to have REAL = 8 bytes but don't want to have DOUBLE PRECISION
       = 16 bytes.  This is SPECIFICALLY prohibited by the Fortran standard
       (which requires that double precision be exactly twice the size of
       real).  
     */
    if (sizeof(float) == MPIR_FSIZE_R) {
	MPIR_Setup_base_datatype( MPI_REAL, &MPIR_I_REAL, MPIR_FLOAT,
				  MPIR_FSIZE_R );
	MPIR_Type_contiguous( 2, MPI_FLOAT, &MPIR_I_2FLOAT, MPI_2REAL );
	}
    else if (sizeof(double) == MPIR_FSIZE_R) {
	MPIR_Setup_base_datatype( MPI_REAL, &MPIR_I_REAL, MPIR_DOUBLE,
				  MPIR_FSIZE_R );
	MPIR_Type_contiguous( 2, MPI_DOUBLE, &MPIR_I_2DOUBLE, MPI_2REAL );
	}
    else {
	/* This won't be right */
	MPIR_Setup_base_datatype( MPI_REAL, &MPIR_I_REAL, MPIR_FLOAT,
				  MPIR_FSIZE_R );
	MPIR_Type_contiguous( 2, MPI_FLOAT, &MPIR_I_2FLOAT, MPI_2REAL );
	}

    /* Note that dcomplex is needed for src/pt2pt/pack_size.c */
    if (sizeof(double) == MPIR_FSIZE_D) {
	MPIR_Setup_base_datatype( MPI_DOUBLE_PRECISION, 
				  &MPIR_I_DOUBLE_PRECISION, MPIR_DOUBLE,
				  MPIR_FSIZE_D );
	MPIR_Setup_base_datatype( MPI_DOUBLE_COMPLEX, &MPIR_I_DCOMPLEX, 
				  MPIR_DOUBLE_COMPLEX, 2 * MPIR_FSIZE_D );
	MPIR_I_DCOMPLEX.align = MPIR_FSIZE_D;

	MPIR_Type_contiguous( 2, MPI_DOUBLE, &MPIR_I_2DOUBLE, 
			      MPI_2DOUBLE_PRECISION );
	}
#if defined(HAVE_LONG_DOUBLE)
    else if (sizeof(long double) == MPIR_FSIZE_D) {
	MPIR_Setup_base_datatype( MPI_DOUBLE_PRECISION, 
				  &MPIR_I_DOUBLE_PRECISION, MPIR_LONGDOUBLE, 
				  MPIR_FSIZE_D );
	/* These aren't correct (we need a ldcomplex datatype in 
	   global_ops.c */
	MPIR_Setup_base_datatype( MPI_DOUBLE_COMPLEX, &MPIR_I_DCOMPLEX, 
				  MPIR_DOUBLE_COMPLEX, 2 * MPIR_FSIZE_D );
	MPIR_I_DCOMPLEX.align = MPIR_FSIZE_D;

	MPIR_Type_contiguous( 2, MPI_DOUBLE_PRECISION, &MPIR_I_2DOUBLE, 
			      MPI_2DOUBLE_PRECISION );
	}
#endif
    else {
	/* we'll have a problem with the reduce/scan ops */
	MPIR_Setup_base_datatype( MPI_DOUBLE_PRECISION, 
				  &MPIR_I_DOUBLE_PRECISION, MPIR_DOUBLE, 
				  MPIR_FSIZE_D );
	MPIR_Setup_base_datatype( MPI_DOUBLE_COMPLEX, &MPIR_I_DCOMPLEX, 
				  MPIR_DOUBLE_COMPLEX, 2 * MPIR_FSIZE_D );
	MPIR_I_DCOMPLEX.align = MPIR_FSIZE_D;

	MPIR_Type_contiguous( 2, MPI_DOUBLE, &MPIR_I_2FLOAT, 
			      MPI_2DOUBLE_PRECISION );
	}

    /* Initialize FORTRAN types for MINLOC and MAXLOC */
    MPIR_Type_contiguous( 2, MPI_COMPLEX, &MPIR_I_2COMPLEX, MPI_2COMPLEX );
    MPIR_Type_contiguous( 2, MPI_DOUBLE_COMPLEX, &MPIR_I_2DCOMPLEX, 
			  MPI_2DOUBLE_COMPLEX );

    /* Note Fortran requires sizeof(INTEGER) == sizeof(REAL) */
    MPIR_Type_contiguous( 2, MPI_INTEGER, &MPIR_I_2INTEGER, MPI_2INTEGER );

    /* Set the values of the Fortran versions */
    /* Logical and character aren't portable in the code below */

    DEBUG(PRINTF("[%d] About to setup Fortran datatypes\n", MPIR_tid);)
    /* Try to generate int1, int2, int4, real4, and real8 datatypes */
#ifdef FOO
    MPIR_int1_dte  = 0;
    MPIR_int2_dte  = 0;
    MPIR_int4_dte  = 0;
    MPIR_real4_dte = 0;
    MPIR_real8_dte = 0;
    /* If these are changed to create new types, change the code in
       finalize.c to free the created types */
    if (sizeof(char) == 1)   MPIR_int1_dte = MPI_CHAR;
    if (sizeof(short) == 2)  MPIR_int2_dte = MPI_SHORT;
    if (sizeof(int) == 4)    MPIR_int4_dte = MPI_INT;
    if (sizeof(float) == 4)  MPIR_real4_dte = MPI_FLOAT;
    if (sizeof(double) == 8) MPIR_real8_dte = MPI_DOUBLE;
#endif
    /* These need to be converted into legal integer values for systems
       with 64 bit pointers */
    /* values are all PARAMETER in the Fortran. */
    
    return 0;
}

void MPIR_Free_Fortran_dtes( void )
{
       MPIR_Free_perm_type( MPI_INTEGER );
       MPIR_Free_perm_type( MPI_LOGICAL );
       MPIR_Free_perm_type( MPI_COMPLEX );
       MPIR_Free_perm_type( MPI_REAL );
       MPIR_Free_perm_type( MPI_2REAL );
       MPIR_Free_perm_type( MPI_DOUBLE_PRECISION );
       MPIR_Free_perm_type( MPI_DOUBLE_COMPLEX );
       MPIR_Free_perm_type( MPI_2DOUBLE_PRECISION );
       MPIR_Free_perm_type( MPI_2COMPLEX );
       MPIR_Free_perm_type( MPI_2DOUBLE_COMPLEX );
       if (MPI_2INT != MPI_2INTEGER)
	   MPIR_Free_perm_type( MPI_2INTEGER );
}

/* 
   This routine computes the sizes of the Fortran data types.  It is 
   called from a Fortran routine that passes consequtive elements of 
   an array of the three Fortran types (character, real, double).
   Note that Fortran REQUIRES that integers have the same size as reals.
 */
#if SIZEOF_F77_REAL == 0 || SIZEOF_F77_DOUBLE_PRECISION == 0 
void mpir_init_fsize_( float *r1, float *r2, double *d1, double *d2 )
{
    /* MPIR_FSIZE_C = (int)(c2 - c1); */
    /* Because of problems in passing characters, we pick the most likely size
       for now */
    /* MPIR_FSIZE_C = sizeof(char); */
    MPIR_FSIZE_R = (int)( (char*)r2 - (char*)r1 );
    MPIR_FSIZE_D = (int)( (char*)d2 - (char*)d1 );
}
#else
/* This is need to satisfy an external reference from initfdte.f when using
   shared libraries.  Eventually, we should remove initfdte and this when
   using shared libraries *and* when not cross-compiling */
void mpir_init_fsize_( float *r1, float *r2, double *d1, double *d2 )
{
    return;
}
#endif


#endif /* MPID_NO_FORTRAN */

