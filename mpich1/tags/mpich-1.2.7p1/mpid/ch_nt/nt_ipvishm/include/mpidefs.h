#ifndef MPIDEFS_H
#define MPIDEFS_H

#define CONFIGURE_ARGS_CLEAN "the default Windows settings"
#define MPIR_MAX_DATATYPE_ARRAY 256
#define MPIR_HAS_COOKIES
#define HAVE_MPI_F2C

/* MPI_STATUS_SIZE is not strictly required in C; however, it should match
   the value for Fortran */
#define MPI_STATUS_SIZE 4

/* 
   Status object.  It is the only user-visible MPI data-structure 
   The "count" field is PRIVATE; use MPI_Get_count to access it. 
 */
typedef struct { 
    int count;
    int MPI_SOURCE;
    int MPI_TAG;
    int MPI_ERROR;
#if (MPI_STATUS_SIZE > 4)
    int extra[MPI_STATUS_SIZE - 4];
#endif
} MPI_Status;

#define HAVE_BNR_CALL
#define HAVE_BNR_FUNCTION_POINTERS
#define HAVE_WINSOCK2_H
#ifndef HAVE_WINDOWS_H
#define HAVE_WINDOWS_H
#endif

#undef ANSI_ARGS
#define ANSI_ARGS(a) a

#define MPI_Aint int
#define MPI_Fint int

#ifndef HAVE_INT64
#define HAVE_INT64
#endif
#ifndef ROMIO_NTFS
#define ROMIO_NTFS
#endif

/*
#define HAS_MPIR_ERR_SETMSG
#define MPICH
#define HAVE_STATUS_SET_BYTES
#define USE_MPI_VERSIONS
*/

#include "mpio.h"

#endif
