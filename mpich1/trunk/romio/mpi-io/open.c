/* -*- Mode: C; c-basic-offset:4 ; -*- */
/* 
 *
 *   Copyright (C) 1997 University of Chicago. 
 *   See COPYRIGHT notice in top-level directory.
 */

#include "mpioimpl.h"

#ifdef HAVE_WEAK_SYMBOLS

#if defined(HAVE_PRAGMA_WEAK)
#pragma weak MPI_File_open = PMPI_File_open
#elif defined(HAVE_PRAGMA_HP_SEC_DEF)
#pragma _HP_SECONDARY_DEF PMPI_File_open MPI_File_open
#elif defined(HAVE_PRAGMA_CRI_DUP)
#pragma _CRI duplicate MPI_File_open as PMPI_File_open
/* end of weak pragmas */
#endif

/* Include mapping from MPI->PMPI */
#define MPIO_BUILD_PROFILING
#include "mpioprof.h"
#endif

extern int ADIO_Init_keyval;

/*@
    MPI_File_open - Opens a file

Input Parameters:
. comm - communicator (handle)
. filename - name of file to open (string)
. amode - file access mode (integer)
. info - info object (handle)

Output Parameters:
. fh - file handle (handle)

.N fortran
@*/
int MPI_File_open(MPI_Comm comm, char *filename, int amode, 
                  MPI_Info info, MPI_File *fh)
{
    int error_code, file_system, flag, /* tmp_amode, */rank;
    char *tmp;
    MPI_Comm dupcomm;
    ADIOI_Fns *fsops;
    static char myname[] = "MPI_FILE_OPEN";

#ifdef MPI_hpux
    int fl_xmpi;

    HPMP_IO_OPEN_START(fl_xmpi, comm);
#endif /* MPI_hpux */

    MPID_CS_ENTER();
    MPIR_Nest_incr();

    /* --BEGIN ERROR HANDLING-- */
    if (comm == MPI_COMM_NULL)
    {
	error_code = MPIO_Err_create_code(MPI_SUCCESS, MPIR_ERR_RECOVERABLE,
					  myname, __LINE__, MPI_ERR_COMM,
					  "**comm", 0);
	error_code = MPIO_Err_return_comm(comm, error_code);
	goto fn_exit;
    }
    /* --END ERROR HANDLING-- */

    MPI_Comm_test_inter(comm, &flag);
    /* --BEGIN ERROR HANDLING-- */
    if (flag)
    {
	error_code = MPIO_Err_create_code(MPI_SUCCESS, MPIR_ERR_RECOVERABLE,
					  myname, __LINE__, MPI_ERR_COMM, 
					  "**commnotintra", 0);
	error_code = MPIO_Err_return_comm(comm, error_code);
	goto fn_exit;
    }

    if ( ((amode&MPI_MODE_RDONLY)?1:0) + ((amode&MPI_MODE_RDWR)?1:0) +
	 ((amode&MPI_MODE_WRONLY)?1:0) != 1 )
    {
	error_code = MPIO_Err_create_code(MPI_SUCCESS, MPIR_ERR_RECOVERABLE,
					  myname, __LINE__, MPI_ERR_AMODE, 
					  "**fileamodeone", 0);
	error_code = MPIO_Err_return_comm(comm, error_code);
	goto fn_exit;
    }

    if ((amode & MPI_MODE_RDONLY) && 
            ((amode & MPI_MODE_CREATE) || (amode & MPI_MODE_EXCL)))
    {
	error_code = MPIO_Err_create_code(MPI_SUCCESS, MPIR_ERR_RECOVERABLE,
					  myname, __LINE__, MPI_ERR_AMODE, 
					  "**fileamoderead", 0);
	error_code = MPIO_Err_return_comm(comm, error_code);
	goto fn_exit;
    }

    if ((amode & MPI_MODE_RDWR) && (amode & MPI_MODE_SEQUENTIAL))
    {
	error_code = MPIO_Err_create_code(MPI_SUCCESS, MPIR_ERR_RECOVERABLE,
					  myname, __LINE__, MPI_ERR_AMODE, 
					  "**fileamodeseq", 0);
	error_code = MPIO_Err_return_comm(comm, error_code);
	goto fn_exit;
    }
    /* --END ERROR HANDLING-- */

/* check if amode is the same on all processes */
    MPI_Comm_dup(comm, &dupcomm);

/*  
    Removed this check because broadcast is too expensive. 
    tmp_amode = amode;
    MPI_Bcast(&tmp_amode, 1, MPI_INT, 0, dupcomm);
    if (amode != tmp_amode) {
	FPRINTF(stderr, "MPI_File_open: amode must be the same on all processes\n");
	MPI_Abort(MPI_COMM_WORLD, 1);
    }
*/

/* check if ADIO has been initialized. If not, initialize it */
    if (ADIO_Init_keyval == MPI_KEYVAL_INVALID) {
	MPI_Initialized(&flag);

	/* --BEGIN ERROR HANDLING-- */
	if (!flag) {
	    error_code = MPIO_Err_create_code(MPI_SUCCESS,
					      MPIR_ERR_RECOVERABLE,
					      myname, __LINE__, MPI_ERR_OTHER,
					      "**initialized", 0);
	    error_code = MPIO_Err_return_comm(comm, error_code);
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


    file_system = -1;

    /* resolve file system type from file name; this is a collective call */
    ADIO_ResolveFileType(dupcomm, filename, &file_system, &fsops, &error_code);
    /* --BEGIN ERROR HANDLING-- */
    if (error_code != MPI_SUCCESS)
    {
	/* ADIO_ResolveFileType() will print as informative a message as it
	 * possibly can or call MPIO_Err_setmsg.  We just need to propagate 
	 * the error up.
	 */
	error_code = MPIO_Err_return_comm(comm, error_code);
	goto fn_exit;
    }

    /* Test for invalid flags in amode.
     *
     * eventually we should allow the ADIO implementations to test for 
     * invalid flags through some functional interface rather than having
     *  these tests here. -- Rob, 06/06/2001
     */
    if (((file_system == ADIO_PIOFS) ||
	 (file_system == ADIO_PVFS) ||
	 (file_system == ADIO_PVFS2) ||
	 (file_system == ADIO_GRIDFTP)) && 
        (amode & MPI_MODE_SEQUENTIAL))
    {
	error_code = MPIO_Err_create_code(MPI_SUCCESS, MPIR_ERR_RECOVERABLE,
					  myname, __LINE__,
					  MPI_ERR_UNSUPPORTED_OPERATION, 
					  "**iosequnsupported", 0);
	error_code = MPIO_Err_return_comm(comm, error_code);
	goto fn_exit;
    }
    /* --END ERROR HANDLING-- */

    /* strip off prefix if there is one, but only skip prefixes
     * if they are greater than length one to allow for windows
     * drive specifications (e.g. c:\...) */

    tmp = strchr(filename, ':');
    if (tmp > filename + 1) {
	filename = tmp + 1;
    }

/* use default values for disp, etype, filetype */    

    *fh = ADIO_Open(comm, dupcomm, filename, file_system, fsops, amode, 0,
		    MPI_BYTE, MPI_BYTE, 0, info, ADIO_PERM_NULL, &error_code);

    /* --BEGIN ERROR HANDLING-- */
    if (error_code != MPI_SUCCESS) {
        MPI_Comm_free(&dupcomm);
    }
    /* --END ERROR HANDLING-- */

    /* determine name of file that will hold the shared file pointer */
    /* can't support shared file pointers on a file system that doesn't
       support file locking. */
    if ((error_code == MPI_SUCCESS) && ((*fh)->file_system != ADIO_PIOFS)
          && ((*fh)->file_system != ADIO_PVFS) 
	  && ((*fh)->file_system != ADIO_PVFS2)
	  && ((*fh)->file_system != ADIO_GRIDFTP) ){
	MPI_Comm_rank(dupcomm, &rank);
	ADIOI_Shfp_fname(*fh, rank);

        /* if MPI_MODE_APPEND, set the shared file pointer to end of file.
           indiv. file pointer already set to end of file in ADIO_Open. 
           Here file view is just bytes. */
	if ((*fh)->access_mode & MPI_MODE_APPEND) {
	    if ((*fh)->io_worker)  /* only one person need set the sharedfp */
		    ADIO_Set_shared_fp(*fh, (*fh)->fp_ind, &error_code);
	    MPI_Barrier(dupcomm);
	}
    }

#ifdef MPI_hpux
    HPMP_IO_OPEN_END(fl_xmpi, *fh, comm);
#endif /* MPI_hpux */

fn_exit:
    MPIR_Nest_decr();
    MPID_CS_EXIT();
    return error_code;
}
