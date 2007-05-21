#include "mpeconf.h"
#include "mpi.h"
#include "mpeexten.h"
#include <stdio.h>

/*
   This file contains routines for examinine some of the internal objects in 
   the MPICH implementation.  
 */

/* These are defined ONLY for MPICH */
int MPIR_PrintDatatypePack ( FILE *, int, MPI_Datatype, long, long );
int MPIR_PrintDatatypeUnpack ( FILE *, int, MPI_Datatype, long, long );

/*@
  MPE_Print_datatype_unpack_action - Prints the operations performed in an 
  unpack of a datatype

  Input Parameters:
+ fp  - FILE pointer for output
. count - Count of datatype
. type - MPI Datatype
- in_offset,out_offset - offsets for input and output buffer.  Should be
  0 for most uses.

  Notes:
  This prints on the selected file the operations that the MPICH 
  implementation will take when unpacking a buffer.
@*/
int MPE_Print_datatype_unpack_action( fp, count, type, in_offset, out_offset )
FILE         *fp;
int          count;
MPI_Datatype type;
int          in_offset, out_offset;
{
return MPIR_PrintDatatypeUnpack( fp, count, type, in_offset, out_offset );
}

/*@
  MPE_Print_datatype_pack_action - Prints the operations performed in an 
  pack of a datatype

  Input Parameters:
+ fp  - FILE pointer for output
. count - Count of datatype
. type - MPI Datatype
- in_offset,out_offset - offsets for input and output buffer.  Should be
  0 for most uses.

  Notes:
  This prints on the selected file the operations that the MPICH 
  implementation will take when packing a buffer.
@*/
int MPE_Print_datatype_pack_action( fp, count, type, in_offset, out_offset )
FILE         *fp;
int          count;
MPI_Datatype type;
int          in_offset, out_offset;
{
return MPIR_PrintDatatypePack ( fp, count, type, in_offset, out_offset );
}


/* Fortran interfaces to these */

#ifdef F77_NAME_UPPER
#define mpe_print_datatype_unpack_action_ MPE_PRINT_DATATYPE_UNPACK_ACTION
#define mpe_print_datatype_pack_action_   MPE_PRINT_DATATYPE_PACK_ACTION
#elif defined(F77_NAME_LOWER_2USCORE)
#define mpe_print_datatype_unpack_action_ mpe_print_datatype_unpack_action__
#define mpe_print_datatype_pack_action_   mpe_print_datatype_pack_action__
#elif defined(F77_NAME_LOWER)
#define mpe_print_datatype_unpack_action_ mpe_print_datatype_unpack_action
#define mpe_print_datatype_pack_action_   mpe_print_datatype_pack_action
#endif

void mpe_print_datatype_unpack_action_ ( int *, int *, MPI_Datatype *,
						   int *, int *, int * );
void mpe_print_datatype_unpack_action_( fp, count, type, 
				        in_offset, out_offset, __ierr )
int *fp;
int *count;
MPI_Datatype *type;
int          *in_offset, *out_offset, *__ierr;
{
*__ierr = MPE_Print_datatype_unpack_action( stdout, *count,*type,
					   *in_offset, *out_offset);
}

void mpe_print_datatype_pack_action_ ( int *, int *, MPI_Datatype *,
						 int *, int *, int * );
void mpe_print_datatype_pack_action_( fp, count, type, 
				        in_offset, out_offset, __ierr )
int *fp;
int *count;
MPI_Datatype *type;
int          *in_offset, *out_offset, *__ierr;
{
*__ierr = MPE_Print_datatype_pack_action( stdout, *count,*type, 
					   *in_offset, *out_offset);
}

