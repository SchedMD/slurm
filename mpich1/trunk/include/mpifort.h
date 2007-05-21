/*
 * This file contains definitions for interfacing C and Fortran
 */
#ifndef MPIR_TO_FLOG

/* Fortran logical values */
#ifndef _CRAY
extern MPI_Fint MPIR_F_TRUE, MPIR_F_FALSE;
#define MPIR_TO_FLOG(a) ((a) ? MPIR_F_TRUE : MPIR_F_FALSE)
/* 
   Note on true and false.  This code is only an approximation.
   Some systems define either true or false, and allow some or ALL other
   patterns for the other.  This is just like C, where 0 is false and 
   anything not zero is true.  Modify this test as necessary for your
   system.
 */
#define MPIR_FROM_FLOG(a) ( (a) == MPIR_F_TRUE ? 1 : 0 )

#else
/* CRAY Vector processors only; these are defined in /usr/include/fortran.h 
   Thanks to lmc@cray.com */
#define MPIR_TO_FLOG(a) (_btol(a))
#define MPIR_FROM_FLOG(a) ( _ltob(&(a)) )    /*(a) must be a pointer */
#endif

/* MPIR_F_MPI_BOTTOM is the address of the Fortran MPI_BOTTOM value */
extern void *MPIR_F_MPI_BOTTOM;

/* MPIR_F_PTR checks for the Fortran MPI_BOTTOM and provides the value 
   MPI_BOTTOM if found 
   See src/pt2pt/addressf.c for why MPIR_F_PTR(a) is just (a)
*/
/*  #define MPIR_F_PTR(a) (((a)==(MPIR_F_MPI_BOTTOM))?MPI_BOTTOM:a) */
#define MPIR_F_PTR(a) (a)

/*  
 * These are hooks for Fortran characters.
 * MPID_FCHAR_T is the type of a Fortran character argument
 * MPID_FCHAR_LARG is the "other" argument that some Fortran compilers use
 * MPID_FCHAR_STR gives the pointer to the characters
 */
#ifdef MPID_CHARACTERS_ARE_CRAYPVP
typedef <whatever> MPID_FCHAR_T;
#define MPID_FCHAR_STR(a) (a)->characters   <or whatever>
#define MPID_FCHAR_LARG(d) 
#else
typedef char *MPID_FCHAR_T;
#define MPID_FCHAR_STR(a) a
#define MPID_FCHAR_LARG(d) ,d
#endif

#endif
