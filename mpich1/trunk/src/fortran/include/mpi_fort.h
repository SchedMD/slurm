#ifndef MPI_FORT
#define MPI_FORT

/* Define the internal values needed for Fortran support */

/* Fortran logicals */

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

#ifndef MPIR_ERROR
#include <stdio.h>
#define MPIR_ERROR(a,b,c) fprintf(stderr, "%s\n", c )
#endif

#ifndef MPIR_FALLOC
#define MPIR_FALLOC(ptr,expr,a,b,c) \
    if (! (ptr = (expr))) { MPIR_ERROR(a,b,c); }
#endif

#ifdef HAVE_STDLIB_H
#include <stdlib.h>
#endif

#ifndef MALLOC
#define MALLOC malloc
#define FREE free
#endif

#ifndef HAS_MPIR_ERR_SETMSG
#define MPIR_Err_setmsg(a,b) a
#endif

#ifndef MPIR_USE_LOCAL_ARRAY
#define MPIR_USE_LOCAL_ARRAY 32
#endif

#ifndef HAVE_MPI_F2C
#define MPI_Comm_c2f(comm) (MPI_Fint)(comm)
#define MPI_Comm_f2c(comm) (MPI_Comm)(comm)
#define MPI_Type_c2f(datatype) (MPI_Fint)(datatype)
#define MPI_Type_f2c(datatype) (MPI_Datatype)(datatype)
#define MPI_Group_c2f(group) (MPI_Fint)(group)
#define MPI_Group_f2c(group) (MPI_Group)(group)
#define MPI_Request_c2f(request) (MPI_Fint)(request)
#define MPI_Request_f2c(request) (MPI_Request)(request)
#define MPI_Op_c2f(op) (MPI_Fint)(op)
#define MPI_Op_f2c(op) (MPI_Op)(op)
#define MPI_Errhandler_c2f(errhandler) (MPI_Fint)(errhandler)
#define MPI_Errhandler_f2c(errhandler) (MPI_Errhandler)(errhandler)
#define MPI_Status_f2c(f_status,c_status) memcpy(c_status,f_status,sizeof(MPI_Status))
#define MPI_Status_c2f(c_status,f_status) memcpy(f_status,c_status,sizeof(MPI_Status))
#endif

#endif
