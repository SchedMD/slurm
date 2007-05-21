/* -*- Mode: C; c-basic-offset:4 ; -*- */
/* 
 *
 *   Copyright (C) 1997 University of Chicago. 
 *   See COPYRIGHT notice in top-level directory.
 */

#include "mpioimpl.h"

#ifdef HAVE_WEAK_SYMBOLS

#if defined(HAVE_PRAGMA_WEAK)
#pragma weak MPI_File_delete = PMPI_File_delete
#elif defined(HAVE_PRAGMA_HP_SEC_DEF)
#pragma _HP_SECONDARY_DEF PMPI_File_delete MPI_File_delete
#elif defined(HAVE_PRAGMA_CRI_DUP)
#pragma _CRI duplicate MPI_File_delete as PMPI_File_delete
/* end of weak pragmas */
#endif

/* Include mapping from MPI->PMPI */
#define MPIO_BUILD_PROFILING
#include "mpioprof.h"
#endif

extern int ADIO_Init_keyval;

/*@
    MPI_File_delete - Deletes a file

Input Parameters:
. filename - name of file to delete (string)
. info - info object (handle)

.N fortran
@*/
int MPI_File_delete(char *filename, MPI_Info info)
{
    int flag, error_code, file_system;
    char *tmp;
    ADIOI_Fns *fsops;
    static char myname[] = "MPI_FILE_DELETE";
#ifdef MPI_hpux
    int fl_xmpi;
  
    HPMP_IO_START(fl_xmpi, BLKMPIFILEDELETE, TRDTBLOCK,
                MPI_FILE_NULL, MPI_DATATYPE_NULL, -1);
#endif /* MPI_hpux */

    MPIU_UNREFERENCED_ARG(info);

    MPID_CS_ENTER();
    MPIR_Nest_incr();

    /* first check if ADIO has been initialized. If not, initialize it */
    if (ADIO_Init_keyval == MPI_KEYVAL_INVALID) {
        MPI_Initialized(&flag);

	/* --BEGIN ERROR HANDLING-- */
        if (!flag) {
	    error_code = MPIO_Err_create_code(MPI_SUCCESS, MPIR_ERR_RECOVERABLE,
					      myname, __LINE__, MPI_ERR_OTHER, 
					      "**initialized", 0);
	    error_code = MPIO_Err_return_file(MPI_FILE_NULL, error_code);
	    goto fn_exit;
	}
	/* --END ERROR HANDLING-- */

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


    /* resolve file system type from file name; this is a collective call */
    ADIO_ResolveFileType(MPI_COMM_SELF, filename, &file_system, &fsops, 
			 &error_code);

    /* --BEGIN ERROR HANDLING-- */
    if (error_code != MPI_SUCCESS)
    {
	/* ADIO_ResolveFileType() will print as informative a message as it
	 * possibly can or call MPIR_Err_setmsg.  We just need to propagate 
	 * the error up.  In the PRINT_ERR_MSG case MPI_Abort has already
	 * been called as well, so we probably didn't even make it this far.
	 */
	error_code = MPIO_Err_return_file(MPI_FILE_NULL, error_code);
	goto fn_exit;
    }
    /* --END ERROR HANDLING-- */

    /* skip prefixes on file names if they have more than one character;
     * single-character prefixes are assumed to be windows drive
     * specifications (e.g. c:\foo) and are left alone.
     */
    tmp = strchr(filename, ':');
    if (tmp > filename + 1)
	filename = tmp + 1;

    /* call the fs-specific delete function */
    (fsops->ADIOI_xxx_Delete)(filename, &error_code);
	
#ifdef MPI_hpux
    HPMP_IO_END(fl_xmpi, MPI_FILE_NULL, MPI_DATATYPE_NULL, -1);
#endif /* MPI_hpux */

fn_exit:
    MPIR_Nest_decr();
    MPID_CS_EXIT();   
    return error_code;
}
