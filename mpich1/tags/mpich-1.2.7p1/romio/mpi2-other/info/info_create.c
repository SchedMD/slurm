/* -*- Mode: C; c-basic-offset:4 ; -*- */
/* 
 *
 *   Copyright (C) 1997 University of Chicago. 
 *   See COPYRIGHT notice in top-level directory.
 */

#include "mpioimpl.h"

#ifdef HAVE_WEAK_SYMBOLS

#if defined(HAVE_PRAGMA_WEAK)
#pragma weak MPI_Info_create = PMPI_Info_create
#elif defined(HAVE_PRAGMA_HP_SEC_DEF)
#pragma _HP_SECONDARY_DEF PMPI_Info_create MPI_Info_create
#elif defined(HAVE_PRAGMA_CRI_DUP)
#pragma _CRI duplicate MPI_Info_create as PMPI_Info_create
/* end of weak pragmas */
#endif

/* Include mapping from MPI->PMPI */
#define MPIO_BUILD_PROFILING
#include "mpioprof.h"
#endif

extern int ADIO_Init_keyval;

/*@
    MPI_Info_create - Creates a new info object

Output Parameters:
. info - info object (handle)

.N fortran
@*/
int MPI_Info_create(MPI_Info *info)
{
    int flag, error_code;

    /* first check if ADIO has been initialized. If not, initialize it */
    if (ADIO_Init_keyval == MPI_KEYVAL_INVALID) {

   /* check if MPI itself has been initialized. If not, flag an error.
   Can't initialize it here, because don't know argc, argv */
        MPI_Initialized(&flag);
        if (!flag) {
            FPRINTF(stderr, "Error: MPI_Init() must be called before using MPI_Info_create\n");
            MPI_Abort(MPI_COMM_WORLD, 1);
        }

        MPI_Keyval_create(MPI_NULL_COPY_FN, ADIOI_End_call, &ADIO_Init_keyval,
                          (void *) 0);  

   /* put a dummy attribute on MPI_COMM_WORLD, because we want the delete
   function to be called when MPI_COMM_WORLD is freed. Hopefully the
   MPI library frees MPI_COMM_WORLD when MPI_Finalize is called,
   though the standard does not mandate this. */

        MPI_Attr_put(MPI_COMM_WORLD, ADIO_Init_keyval, (void *) 0);

/* initialize ADIO */

        ADIO_Init( (int *)0, (char ***)0, &error_code);
    }

    *info = (MPI_Info) ADIOI_Malloc(sizeof(struct MPIR_Info));
    (*info)->cookie = MPIR_INFO_COOKIE;
    (*info)->key = 0;
    (*info)->value = 0;
    (*info)->next = 0;
    /* this is the first structure in this linked list. it is 
       always kept empty. new (key,value) pairs are added after it. */

    return MPI_SUCCESS;
}
