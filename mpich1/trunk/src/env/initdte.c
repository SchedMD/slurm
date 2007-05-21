/*
 *  $Id: initdte.c,v 1.11 2000/08/10 22:15:34 toonen Exp $
 *
 *  (C) 1996 by Argonne National Laboratory and Mississipi State University.
 *      See COPYRIGHT in top-level directory.
 */


#include "mpiimpl.h"
/* MPIR_Type_xxx routines are prototyped in mpipt2pt.h */
#include "mpipt2pt.h"
#include "sbcnst2.h"
#define MPIR_SBinit MPID_SBinit
#define MPIR_SBalloc MPID_SBalloc
#define MPIR_SBfree MPID_SBfree
#define MPIR_trmalloc MPID_trmalloc

/* #define DEBUG(a) {a}  */
#define DEBUG(a)


/* Global memory management variables for fixed-size blocks */
void *MPIR_dtes;      /* sbcnst datatype elements */

/* Static space for predefined datatypes */
struct MPIR_DATATYPE MPIR_I_CHAR, MPIR_I_SHORT, MPIR_I_INT, MPIR_I_LONG,
                            MPIR_I_UCHAR, MPIR_I_USHORT, MPIR_I_UINT, 
                            MPIR_I_ULONG, MPIR_I_FLOAT, MPIR_I_DOUBLE, 
                            MPIR_I_LONG_DOUBLE, MPIR_I_LONG_LONG_INT, 
                            MPIR_I_BYTE;
/* Derived and special types */
struct MPIR_DATATYPE MPIR_I_PACKED, MPIR_I_LONG_DOUBLE_INT,
                            MPIR_I_UB, MPIR_I_LB,
                            MPIR_I_FLOAT_INT, MPIR_I_DOUBLE_INT, 
                            MPIR_I_LONG_INT, MPIR_I_SHORT_INT, MPIR_I_2INT,
                            MPIR_I_LONG_DOUBLE_INT, 
                            MPIR_I_2FLOAT, MPIR_I_2DOUBLE;


/* Definition for pointer to packed datatype */
struct MPIR_DATATYPE *MPIR_PACKED_PTR = 0;

/* Global pre-assigned datatypes */
void MPIR_Setup_base_datatype (MPI_Datatype, struct MPIR_DATATYPE *, 
					 MPIR_NODETYPE, int);
void MPIR_Setup_complex_datatype (MPI_Datatype, MPI_Datatype, 
					    struct MPIR_DATATYPE * );
void MPIR_Type_contiguous ( int, MPI_Datatype, 
				      struct MPIR_DATATYPE *, MPI_Datatype );

/* C Datatypes for MINLOC and MAXLOC functions. 
   These allow us to construct the structures to match the compiler that
   is used to build MPICH, and, if the user uses the recommended compile
   scripts or makefiles, they will match the compiler that they use.
 */
typedef struct {
  float  var;
  int    loc;
} MPI_FLOAT_INT_struct;
MPI_FLOAT_INT_struct MPI_FLOAT_INT_var;

typedef struct {
  double var;
  int    loc;
} MPI_DOUBLE_INT_struct;
MPI_DOUBLE_INT_struct MPI_DOUBLE_INT_var;

typedef struct {
  long   var;
  int    loc;
} MPI_LONG_INT_struct;
MPI_LONG_INT_struct MPI_LONG_INT_var;

typedef struct {
  short  var;
  int    loc;
} MPI_SHORT_INT_struct;
MPI_SHORT_INT_struct MPI_SHORT_INT_var;

#if defined(HAVE_LONG_DOUBLE)
typedef struct {
  long double   var;
  int           loc;
} MPI_LONG_DOUBLE_INT_struct;
MPI_LONG_DOUBLE_INT_struct MPI_LONG_DOUBLE_INT_var;
#endif

void MPIR_Init_dtes()
{
    MPI_Datatype   type[3], temptype;
    MPI_Aint       disp[3];
    int            blln[3];

    /* set up pre-defined data types */
    DEBUG(PRINTF("[%d] About to create datatypes\n", MPIR_tid);)

    MPIR_dtes       = MPIR_SBinit( sizeof( struct MPIR_DATATYPE ), 100, 100 );

    MPIR_Setup_base_datatype( MPI_INT, &MPIR_I_INT, MPIR_INT, sizeof(int) );

    MPIR_Setup_base_datatype( MPI_FLOAT, &MPIR_I_FLOAT, 
			      MPIR_FLOAT, sizeof(float) );
    MPIR_Setup_base_datatype( MPI_DOUBLE, &MPIR_I_DOUBLE, 
			      MPIR_DOUBLE, sizeof(double) );
    MPIR_Setup_base_datatype( MPI_LONG, &MPIR_I_LONG, 
			      MPIR_LONG, sizeof(long) );
    MPIR_Setup_base_datatype( MPI_SHORT, &MPIR_I_SHORT, 
			      MPIR_SHORT, sizeof(short) );
    MPIR_Setup_base_datatype( MPI_CHAR, &MPIR_I_CHAR, 
			      MPIR_CHAR, sizeof(char) );
    MPIR_Setup_base_datatype( MPI_BYTE, &MPIR_I_BYTE, 
			      MPIR_BYTE, sizeof(char) );
    MPIR_Setup_base_datatype( MPI_UNSIGNED_CHAR, &MPIR_I_UCHAR, 
			      MPIR_UCHAR, sizeof(char) );
    MPIR_Setup_base_datatype( MPI_UNSIGNED_SHORT, &MPIR_I_USHORT, 
			      MPIR_USHORT, sizeof(short) );
    MPIR_Setup_base_datatype( MPI_UNSIGNED_LONG, &MPIR_I_ULONG, 
			      MPIR_ULONG, sizeof(unsigned long) );
    MPIR_Setup_base_datatype( MPI_UNSIGNED, &MPIR_I_UINT, 
			      MPIR_UINT, sizeof(unsigned int) );
    MPIR_Setup_base_datatype( MPI_PACKED, &MPIR_I_PACKED, MPIR_PACKED, 1 );
    MPIR_PACKED_PTR = &MPIR_I_PACKED;
    MPIR_Setup_base_datatype( MPI_UB, &MPIR_I_UB, MPIR_UB, 0 );
    MPIR_I_UB.align	     = 1;
    MPIR_I_UB.elements	     = 0;
    MPIR_I_UB.count	     = 0;

    MPIR_Setup_base_datatype( MPI_LB, &MPIR_I_LB, MPIR_LB, 0 );
    MPIR_I_LB.align	     = 1;
    MPIR_I_LB.elements	     = 0;
    MPIR_I_LB.count	     = 0;


#if defined(HAVE_LONG_DOUBLE)
    MPIR_Setup_base_datatype( MPI_LONG_DOUBLE, &MPIR_I_LONG_DOUBLE, 
			      MPIR_LONGDOUBLE, sizeof(long double) );
#else
    MPIR_Setup_base_datatype( MPI_LONG_DOUBLE, &MPIR_I_LONG_DOUBLE, 
			      MPIR_LONGDOUBLE, 2*sizeof(double) );
#endif

    /* Initialize C 2int type for MINLOC and MAXLOC */
    MPIR_Type_contiguous( 2, MPI_INT, &MPIR_I_2INT, MPI_2INT );

    /* Initialize C types for MINLOC and MAXLOC */
    /* I'm not sure that this is 100% portable */
    blln[0] = blln[1] = blln[2] = 1;
    type[1] = MPI_INT;   
    type[2] = MPI_UB;
    disp[0] = 0;

    type[0] = MPI_FLOAT;     
    disp[1] = (char *)&MPI_FLOAT_INT_var.loc - 
      (char *)&MPI_FLOAT_INT_var;
    disp[2] = sizeof(MPI_FLOAT_INT_struct);
    MPI_Type_struct ( 3, blln, disp, type, &temptype );
    MPIR_Setup_complex_datatype( temptype, MPI_FLOAT_INT, &MPIR_I_FLOAT_INT );

    type[0] = MPI_DOUBLE;
    disp[1] = (char *)&MPI_DOUBLE_INT_var.loc - 
      (char *)&MPI_DOUBLE_INT_var;
    disp[2] = sizeof(MPI_DOUBLE_INT_struct);
    MPI_Type_struct ( 3, blln, disp, type, &temptype );
    MPIR_Setup_complex_datatype( temptype, MPI_DOUBLE_INT, 
				 &MPIR_I_DOUBLE_INT );

    type[0] = MPI_LONG;
    disp[1] = (char *)&MPI_LONG_INT_var.loc - 
      (char *)&MPI_LONG_INT_var;
    disp[2] = sizeof(MPI_LONG_INT_struct);
    MPI_Type_struct ( 3, blln, disp, type, &temptype );
    MPIR_Setup_complex_datatype( temptype, MPI_LONG_INT, &MPIR_I_LONG_INT );

    type[0] = MPI_SHORT;
    disp[1] = (char *)&MPI_SHORT_INT_var.loc - 
      (char *)&MPI_SHORT_INT_var;
    disp[2] = sizeof(MPI_SHORT_INT_struct);
    MPI_Type_struct ( 3, blln, disp, type, &temptype );
    MPIR_Setup_complex_datatype( temptype, MPI_SHORT_INT, &MPIR_I_SHORT_INT );

#if defined(HAVE_LONG_DOUBLE)
    type[0] = MPI_LONG_DOUBLE;
    disp[1] = (char *)&MPI_LONG_DOUBLE_INT_var.loc - 
      (char *)&MPI_LONG_DOUBLE_INT_var;
    disp[2] = sizeof(MPI_LONG_DOUBLE_INT_struct);
    MPI_Type_struct ( 3, blln, disp, type, &temptype );
    MPIR_Setup_complex_datatype( temptype, MPI_LONG_DOUBLE_INT, 
				 &MPIR_I_LONG_DOUBLE_INT );
#else
    /* use just double if long double not available */
    type[0] = MPI_DOUBLE;
    disp[1] = (char *)&MPI_DOUBLE_INT_var.loc - 
      (char *)&MPI_DOUBLE_INT_var;
    disp[2] = sizeof(MPI_DOUBLE_INT_struct);
    MPI_Type_struct ( 3, blln, disp, type, &temptype );
    MPIR_Setup_complex_datatype( temptype, MPI_LONG_DOUBLE_INT, 
				 &MPIR_I_LONG_DOUBLE_INT );
#endif

#if defined(HAVE_LONG_LONG_INT)
    MPIR_Setup_base_datatype( MPI_LONG_LONG_INT, &MPIR_I_LONG_LONG_INT,
			      MPIR_LONGLONGINT, sizeof(long long) );
#else
    MPIR_Setup_base_datatype( MPI_LONG_LONG_INT, &MPIR_I_LONG_LONG_INT,
			      MPIR_LONGLONGINT, 2*sizeof(long) );
#endif

}

/*
 * Having initialized the datatype, we also need to free them
 */
void MPIR_Free_dtes()
{
    /* We can't use MPI_Type_free, because it checks for predefined
       types. */
       MPIR_Free_perm_type( MPI_INT );
       MPIR_Free_perm_type( MPI_DOUBLE );
       MPIR_Free_perm_type( MPI_FLOAT );
       MPIR_Free_perm_type( MPI_LONG );
       MPIR_Free_perm_type( MPI_SHORT );
       MPIR_Free_perm_type( MPI_CHAR );
       MPIR_Free_perm_type( MPI_BYTE );
       MPIR_Free_perm_type( MPI_UNSIGNED_CHAR );
       MPIR_Free_perm_type( MPI_UNSIGNED_SHORT );
       MPIR_Free_perm_type( MPI_UNSIGNED_LONG );
       MPIR_Free_perm_type( MPI_UNSIGNED );
       MPIR_Free_perm_type( MPI_PACKED );
       MPIR_Free_perm_type( MPI_UB );
       MPIR_Free_perm_type( MPI_LB );
       MPIR_Free_perm_type( MPI_LONG_DOUBLE );
       MPIR_Free_perm_type( MPI_2INT );
       MPIR_Free_perm_type( MPI_FLOAT_INT );
       MPIR_Free_perm_type( MPI_DOUBLE_INT );
       MPIR_Free_perm_type( MPI_LONG_INT );
       MPIR_Free_perm_type( MPI_SHORT_INT );
       MPIR_Free_perm_type( MPI_LONG_DOUBLE_INT );
       MPIR_Free_perm_type( MPI_LONG_LONG_INT );
    /* Free the parts of the structure types */

#if defined(HAVE_LONG_DOUBLE)
/*    {MPI_Datatype t = MPI_LONG_DOUBLE_INT;
    MPI_Type_free( &t );} */
/*     MPI_Type_free( &MPI_LONG_DOUBLE ); */
#endif
#if defined(HAVE_LONG_LONG_INT)
  /*  MPI_Type_free( &MPI_LONG_LONG_INT ); */
#endif
}

/* This routine sets up a datatype that is specified as a small integer (val)
   and has a local reference
 */
void MPIR_Setup_base_datatype( 
	MPI_Datatype         val,
	struct MPIR_DATATYPE *lval,
	MPIR_NODETYPE        type,
	int                  size)
{
    MPIR_SET_COOKIE(lval,MPIR_DATATYPE_COOKIE);
    lval->dte_type       = type;
    lval->committed      = 1;
    lval->is_contig      = 1;
    lval->lb             = 0;
    lval->ub             = size;
    lval->extent         = size;
    lval->size           = size;
    lval->align          = size;
    lval->stride         = size;
    lval->elements       = 1;
    lval->count          = 1;
    lval->blocklen       = 1;
    lval->basic          = 1;
    lval->permanent      = 1;
    lval->old_type       = lval;
    lval->ref_count      = 1;
    MPIR_RegPointerIdx( val, lval );
    lval->self           = val;
#   if defined(MPID_HAS_TYPE_PERMANENT_SETUP)
    {
	MPID_Type_permanent_setup(val);
    }
#   endif
}

/* 
   This takes a datatype created with the datatype routines, copies the
   data into the "newtype" structure, and frees the old datatype storage 
 */
void MPIR_Setup_complex_datatype( 
	MPI_Datatype oldtype, 
	MPI_Datatype newtype,
	struct MPIR_DATATYPE *newtype_ptr)
{
    struct MPIR_DATATYPE *oldtype_ptr;
    oldtype_ptr = MPIR_ToPointer( oldtype );
    memcpy( newtype_ptr, oldtype_ptr, sizeof(struct MPIR_DATATYPE) );
    /* Should this be MPI_Type_free( &oldtype->self )? */
    MPIR_RmPointer( oldtype );
    MPIR_SBfree ( MPIR_dtes, oldtype_ptr );
    MPIR_Type_permanent ( newtype_ptr );
    MPIR_RegPointerIdx( newtype, newtype_ptr );
    newtype_ptr->self = newtype;
#   if defined(MPID_HAS_TYPE_PERMANENT_SETUP)
    {
	MPID_Type_permanent_setup(newtype);
    }
#   endif
    MPI_Type_commit( &newtype );
}

void MPIR_Type_contiguous( 
	int cnt, 
	MPI_Datatype old_type, 
	struct MPIR_DATATYPE *newtype_ptr, 
	MPI_Datatype newtype )
{
    MPI_Datatype tmp_type;
    MPI_Type_contiguous( cnt, old_type, &tmp_type );
    MPIR_Setup_complex_datatype( tmp_type, newtype, newtype_ptr );
    /* MPI_Type_free( &tmp_type ); */
}


