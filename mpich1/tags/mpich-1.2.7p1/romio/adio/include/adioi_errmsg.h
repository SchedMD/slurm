/* -*- Mode: C; c-basic-offset:4 ; -*- */
/*  $Id: adioi_errmsg.h,v 1.5 2005/05/23 23:27:49 rross Exp $
 *
 *  (C) 2001 by Argonne National Laboratory.
 *      See COPYRIGHT in top-level directory.
 */

MPI_ERR_FILE
    MPIR_ERR_FILE_NULL     "Null file handle"
    MPIR_ERR_FILE_CORRUPT  "Corrupted file handle"

MPI_ERR_AMODE
    3  "Exactly one of MPI_MODE_RDONLY, MPI_MODE_WRONLY, or MPI_MODE_RDWR must be specified"
    5  "Cannot use MPI_MODE_CREATE or MPI_MODE_EXCL with MPI_MODE_RDONLY"
    7  "Cannot specify MPI_MODE_SEQUENTIAL with MPI_MODE_RDWR"

MPI_ERR_ARG
    MPIR_ERR_OFFSET_ARG "Invalid offset argument"
    MPIR_ERR_DATAREP_ARG "Null datarep argument"
    MPIR_ERR_COUNT_ARG "Invalid count argument"
    MPIR_ERR_SIZE_ARG "Invalid size argument"
    MPIR_ERR_WHENCE_ARG "Invalid whence argument"
    MPIR_ERR_FLAG_ARG "flag argument must be the same on all processes"
    MPIR_ERR_DISP_ARG "Invalid displacement argument"
    MPIR_ERR_ETYPE_ARG "Invalid etype argument"
    MPIR_ERR_FILETYPE_ARG "Invalid filetype argument"
    MPIR_ERR_SIZE_ARG_NOT_SAME "size argument must be the same on all processes"
    MPIR_ERR_OFFSET_ARG_NEG "offset points to a negative location in the file"
    MPIR_ERR_WHENCE_ARG_NOT_SAME "whence argument must be the same on all processes"
    MPIR_ERR_OFFSET_ARG_NOT_SAME "offset argument must be the same on all processes"

MPI_ERR_TYPE
    MPIR_ERR_TYPE_NULL (null datatype. from MPICH)

MPI_ERR_UNSUPPORTED_OPERATION
    MPIR_ERR_NO_SHARED_FP "Shared file pointer not supported on PIOFS and PVFS"
    MPIR_ERR_AMODE_SEQ "Cannot use this function when file is opened with amode MPI_MODE_SEQUENTIAL"
    MPIR_ERR_MODE_WRONLY "Cannot read from a file opened with amode MPI_MODE_WRONLY"
    MPIR_ERR_NO_MODE_SEQ "MPI_MODE_SEQUENTIAL not supported on PIOFS and PVFS"

MPI_ERR_REQUEST
    MPIR_ERR_REQUEST_NULL (null request. from MPICH)

MPI_ERR_IO
    MPIR_ERR_ETYPE_FRACTIONAL "Only an integral number of etypes can be accessed"
    MPIR_ERR_NO_FSTYPE "Can't determine the file-system type. Check the filename/path you provided and try again. Otherwise, prefix the filename with a string to indicate the type of file sytem (piofs:, pfs:, nfs:, ufs:, hfs:, xfs:, sfs:, pvfs:, panfs: ftp: gsiftp:)"
    MPIR_ERR_NO_PFS "ROMIO has not been configured to use the PFS file system"
    MPIR_ERR_NO_PIOFS "ROMIO has not been configured to use the PIOFS file system"
    MPIR_ERR_NO_UFS "ROMIO has not been configured to use the UFS file system"
    MPIR_ERR_NO_NFS "ROMIO has not been configured to use the NFS file system"
    MPIR_ERR_NO_HFS "ROMIO has not been configured to use the HFS file system"
    MPIR_ERR_NO_XFS "ROMIO has not been configured to use the XFS file system"
    MPIR_ERR_NO_SFS "ROMIO has not been configured to use the SFS file system"
    MPIR_ERR_NO_PVFS "ROMIO has not been configured to use the PVFS file system"
    MPIR_ERR_NO_PANFS "ROMIO has not been configured to use the PANFS file system"
    MPIR_ERR_MULTIPLE_SPLIT_COLL "Only one active split collective I/O operation allowed per file handle"
    MPIR_ERR_NO_SPLIT_COLL "No previous split collective begin"
    MPIR_ERR_ASYNC_OUTSTANDING "There are outstanding nonblocking I/O operations on this file"
    MPIR_ADIO_ERROR "I/O Error"  strerror(errno)
    MPIR_READ_PERM "ROMIO tries to optimize this access by doing a read-modify-write, but is unable to read the file. Please give the file read permission and open it with MPI_MODE_RDWR."
    MPIR_PREALLOC_PERM "To preallocate disk space, ROMIO needs to read the file and write it back, but is unable to read the file. Please give the file read permission and open it with MPI_MODE_RDWR."
    MPIR_ERR_FILETYPE  "Filetype must be constructed out of one or more etypes"
    MPIR_ERR_NO_TESTFS "ROMIO has not been configured to use the TESTFS file system"
    MPIR_ERR_DEFERRED "independent IO attempted even though no_indep_rw hint given"

MPI_ERR_COMM
    MPIR_ERR_COMM_NULL (null communicator. from MPICH)
MPIR_ERR_COMM_INTER  (no intercommunicator. (from MPICH)

MPI_ERR_UNSUPPORTED_DATAREP
    MPIR_ERR_NOT_NATIVE_DATAREP "Only native data representation currently supported"


