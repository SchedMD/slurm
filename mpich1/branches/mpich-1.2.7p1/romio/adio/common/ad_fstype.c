/* -*- Mode: C; c-basic-offset:4 ; -*- */
/* 
 *   Copyright (C) 1997 University of Chicago. 
 *   See COPYRIGHT notice in top-level directory.
 */

/* This file is quickly becoming the single one, outside the ADIO
 * implementations, which has "what ADIO components are built in" code in it.
 */

#include "adio.h"

#ifdef HAVE_UNISTD_H
#include <unistd.h>
#endif

#ifdef HAVE_SYS_PARAM_H
#include <sys/param.h>
#endif

#ifdef HAVE_PVFS_H
#include "pvfs.h"
#endif

#ifdef HAVE_PVFS2_H
#include "pvfs2.h"
#endif

/* Notes on detection process:
 *
 * There are three more "general" mechanisms that we use for detecting
 * file system type:
 * - struct statfs's f_type field
 * - struct statvfs's f_basetype field
 * - struct stat's st_fstype field
 *
 * Otherwise we'll fall back on some OS-specific approach.
 */

#ifdef HAVE_STRUCT_STATFS
# ifdef HAVE_SYS_VFS_H
# include <sys/vfs.h>
# endif
# ifdef HAVE_SYS_STATVFS_H
# include <sys/statvfs.h>
# endif
# ifdef HAVE_SYS_PARAM_H
# include <sys/param.h>
# endif
# ifdef HAVE_SYS_MOUNT_H
# include <sys/mount.h>
# endif
 /* On Linux platforms, linux/nfs_fs.h is all messed up and cannot be
  * reliably included.
  */
# if defined(ROMIO_NFS) && !defined(NFS_SUPER_MAGIC)
# define NFS_SUPER_MAGIC 0x6969
# endif

# if defined(ROMIO_PANFS) && !defined(PAN_KERNEL_FS_CLIENT_SUPER_MAGIC)
# define PAN_KERNEL_FS_CLIENT_SUPER_MAGIC 0xAAD7AAEA
# endif
#endif

# if defined(ROMIO_XFS) && !defined(XFS_SUPER_MAGIC)
# define XFS_SUPER_MAGIC 0x58465342
# endif

#ifdef ROMIO_HAVE_STRUCT_STATVFS_WITH_F_BASETYPE
# ifdef HAVE_SYS_STATVFS_H
# include <sys/statvfs.h>
# endif
# ifdef HAVE_SYS_VFS_H
# include <sys/vfs.h>
# endif
# ifdef HAVE_SYS_PARAM_H
# include <sys/param.h>
# endif
# ifdef HAVE_SYS_MOUNT_H
# include <sys/mount.h>
# endif
#endif

#ifdef ROMIO_HAVE_STRUCT_STAT_WITH_ST_FSTYPE
# ifdef HAVE_SYS_TYPES_H
# include <sys/types.h>
# endif
# ifdef HAVE_SYS_STAT_H
# include <sys/stat.h>
# endif
#endif

#ifndef ROMIO_NTFS
static void ADIO_FileSysType_parentdir(char *filename, char **dirnamep);
#endif
static void ADIO_FileSysType_prefix(char *filename, int *fstype, 
				    int *error_code);
static void ADIO_FileSysType_fncall(char *filename, int *fstype, 
				    int *error_code);

/*
 ADIO_FileSysType_parentdir - determines a string pathname for the
 parent directory of a given filename.

Input Parameters:
. filename - pointer to file name character array

Output Parameters:
. dirnamep - pointer to location in which to store a pointer to a string

 Note that the caller should free the memory located at the pointer returned
 after the string is no longer needed.
*/
#ifndef ROMIO_NTFS
#ifndef PATH_MAX
#define PATH_MAX 65535
#endif

/* In a strict ANSI environment, S_ISLNK may not be defined.  Fix that
   here.  We assume that S_ISLNK is *always* defined as a macro.  If
   that is not universally true, then add a test to the romio
   configure that trys to link a program that references S_ISLNK */
#if !defined(S_ISLNK) 
#    if defined(S_IFLNK)
     /* Check for the link bit */
#    define S_ISLNK(mode) ((mode) & S_IFLNK)
#    else
     /* no way to check if it is a link, so say false */
#    define S_ISLNK(mode) 0   
#    endif
#endif

/* ADIO_FileSysType_parentdir
 *
 * Returns pointer to string in dirnamep; that string is allocated with
 * strdup and must be free()'d.
 */
static void ADIO_FileSysType_parentdir(char *filename, char **dirnamep)
{
    int err;
    char *dir = NULL, *slash;
    struct stat statbuf;
    
    err = lstat(filename, &statbuf);

    if (err || (!S_ISLNK(statbuf.st_mode))) {
	/* no such file, or file is not a link; these are the "normal"
	 * cases where we can just return the parent directory.
	 */
	dir = ADIOI_Strdup(filename);
    }
    else {
	/* filename is a symlink.  we've presumably already tried
	 * to stat it and found it to be missing (dangling link),
	 * but this code doesn't care if the target is really there
	 * or not.
	 */
	int namelen;
	char *linkbuf;

	linkbuf = ADIOI_Malloc(PATH_MAX+1);
	namelen = readlink(filename, linkbuf, PATH_MAX+1);
	if (namelen == -1) {
	    /* something strange has happened between the time that
	     * we determined that this was a link and the time that
	     * we attempted to read it; punt and use the old name.
	     */
	    dir = ADIOI_Strdup(filename);
	}
	else {
	    /* successfully read the link */
	    linkbuf[namelen] = '\0'; /* readlink doesn't null terminate */
	    dir = ADIOI_Strdup(linkbuf);
	    ADIOI_Free(linkbuf);
	}
    }

    slash = strrchr(dir, '/');
    if (!slash) ADIOI_Strncpy(dir, ".", 2);
    else {
	if (slash == dir) *(dir + 1) = '\0';
	else *slash = '\0';
    }

    *dirnamep = dir;
    return;
}
#endif /* ROMIO_NTFS */

/*
 ADIO_FileSysType_fncall - determines the file system type for a given file 
 using a system-dependent function call

Input Parameters:
. filename - pointer to file name character array

Output Parameters:
. fstype - location in which to store file system type (ADIO_XXX)
. error_code - location in which to store error code

 MPI_SUCCESS is stored in the location pointed to by error_code on success.

 This function is used by MPI_File_open() and MPI_File_delete() to determine 
 file system type.  Most other functions use the type which is stored when the 
 file is opened.
 */
static void ADIO_FileSysType_fncall(char *filename, int *fstype, int *error_code)
{
#ifndef ROMIO_NTFS
    char *dir;
    int err;
#endif

#ifdef ROMIO_HAVE_STRUCT_STATVFS_WITH_F_BASETYPE
    struct statvfs vfsbuf;
#endif
#ifdef HAVE_STRUCT_STATFS
    struct statfs fsbuf;
#endif
#ifdef ROMIO_HAVE_STRUCT_STAT_WITH_ST_FSTYPE
    struct stat sbuf;
#endif
    static char myname[] = "ADIO_RESOLVEFILETYPE_FNCALL";

    *error_code = MPI_SUCCESS;

#ifdef ROMIO_HAVE_STRUCT_STATVFS_WITH_F_BASETYPE
    do {
	err = statvfs(filename, &vfsbuf);
    } while (err && (errno == ESTALE));

    if (err && (errno == ENOENT)) {
	/* ENOENT may be returned in two cases:
	 * 1) no directory entry for "filename"
	 * 2) "filename" is a dangling symbolic link
	 *
	 * ADIO_FileSysType_parentdir tries to deal with both cases.
	 */
	ADIO_FileSysType_parentdir(filename, &dir);
	err = statvfs(dir, &vfsbuf);

	ADIOI_Free(dir);
    }

    /* --BEGIN ERROR HANDLING-- */
    if (err) {
	*error_code = MPIO_Err_create_code(MPI_SUCCESS, MPIR_ERR_RECOVERABLE,
					   myname, __LINE__, MPI_ERR_NO_SUCH_FILE,
					   "**filename", "**filename %s", filename);
	return;
    }
    /* --END ERROR HANDLING-- */

    /* FPRINTF(stderr, "%s\n", vfsbuf.f_basetype); */
    if (!strncmp(vfsbuf.f_basetype, "nfs", 3)) {
	*fstype = ADIO_NFS;
	return;
    }
    if (!strncmp(vfsbuf.f_basetype, "xfs", 3)) {
	*fstype = ADIO_XFS;
	return;
    }

# ifdef ROMIO_UFS
    /* if UFS support is enabled, default to that */
    *fstype = ADIO_UFS;
    return;
# endif

    /* --BEGIN ERROR HANDLING-- */
    *error_code = MPIO_Err_create_code(MPI_SUCCESS, MPIR_ERR_RECOVERABLE,
				       myname, __LINE__, MPI_ERR_NO_SUCH_FILE,
				       "**filename", "**filename %s", filename);
    /* --END ERROR HANDLING-- */
#endif /* STATVFS APPROACH */

#ifdef HAVE_STRUCT_STATFS
    do {
	err = statfs(filename, &fsbuf);
    } while (err && (errno == ESTALE));

    if (err && (errno == ENOENT)) {
	ADIO_FileSysType_parentdir(filename, &dir);
	err = statfs(dir, &fsbuf);
	ADIOI_Free(dir);
    }

    /* --BEGIN ERROR HANDLING-- */
    if (err) {
    	*error_code = MPIO_Err_create_code(MPI_SUCCESS, MPIR_ERR_RECOVERABLE,
					   myname, __LINE__, MPI_ERR_NO_SUCH_FILE,
					   "**filename", "**filename %s", filename);
	return;
    }
    /* --END ERROR HANDLING-- */

# ifdef ROMIO_HAVE_STRUCT_STATFS_WITH_F_FSTYPENAME
    if ( !strncmp("nfs",fsbuf.f_fstypename,3) ) {
	*fstype = ADIO_NFS;
	return;
    }
# endif
    /* FPRINTF(stderr, "%d\n", fsbuf.f_type);*/
# ifdef NFS_SUPER_MAGIC
    if (fsbuf.f_type == NFS_SUPER_MAGIC) {
	*fstype = ADIO_NFS;
	return;
    }
# endif

# ifdef PAN_KERNEL_FS_CLIENT_SUPER_MAGIC
    if (fsbuf.f_type == PAN_KERNEL_FS_CLIENT_SUPER_MAGIC) {
	*fstype = ADIO_PANFS;
	return;
    }
# endif

# ifdef MOUNT_NFS
    if (fsbuf.f_type == MOUNT_NFS) {
	*fstype = ADIO_NFS;
	return;
    }
# endif

# ifdef MOUNT_PFS
    if (fsbuf.f_type == MOUNT_PFS) {
	*fstype = ADIO_PFS;
	return;
    }
# endif

# ifdef PVFS_SUPER_MAGIC
    if (fsbuf.f_type == PVFS_SUPER_MAGIC) {
	*fstype = ADIO_PVFS;
	return;
    }
# endif

# ifdef PVFS2_SUPER_MAGIC
    if (fsbuf.f_type == PVFS2_SUPER_MAGIC) {
	*fstype = ADIO_PVFS2;
	return;
    }
# endif

# ifdef ROMIO_UFS
    /* if UFS support is enabled, default to that */
    *fstype = ADIO_UFS;
    return;
# endif
    /* --BEGIN ERROR HANDLING-- */
    *error_code = MPIO_Err_create_code(MPI_SUCCESS, MPIR_ERR_RECOVERABLE,
				       myname, __LINE__, MPI_ERR_NO_SUCH_FILE,
				       "**filename", "**filename %s", filename);
    /* --END ERROR HANDLING-- */
#endif /* STATFS APPROACH */

#ifdef ROMIO_HAVE_STRUCT_STAT_WITH_ST_FSTYPE
    do {
	err = stat(filename, &sbuf);
    } while (err && (errno == ESTALE));

    if (err && (errno == ENOENT)) {
	ADIO_FileSysType_parentdir(filename, &dir);
	err = stat(dir, &sbuf);
	ADIOI_Free(dir);
    }
    
    if (err) {
    	/* --BEGIN ERROR HANDLING-- */
    	*error_code = MPIO_Err_create_code(MPI_SUCCESS, MPIR_ERR_RECOVERABLE,
				           myname, __LINE__, MPI_ERR_NO_SUCH_FILE,
				           "**filename", "**filename %s", filename);
    	/* --END ERROR HANDLING-- */
	return;
    }
    else {
	if (!strcmp(sbuf.st_fstype, "nfs")) *fstype = ADIO_NFS;
	else *fstype = ADIO_SFS; /* assuming SX4 for now */
    }
#endif /* STAT APPROACH */

#ifdef ROMIO_NTFS
    ADIOI_UNREFERENCED_ARG(filename);
    ADIOI_UNREFERENCED_ARG(error_code);
    *fstype = ADIO_NTFS; /* only supported FS on Windows */
#elif defined(ROMIO_NFS)
    *fstype = ADIO_NFS;
#elif defined(ROMIO_UFS)
    *fstype = ADIO_UFS;
#else
    /* --BEGIN ERROR HANDLING-- */
    *error_code = MPIO_Err_create_code(MPI_SUCCESS, MPIR_ERR_RECOVERABLE,
				       myname, __LINE__, MPI_ERR_NO_SUCH_FILE,
				       "**filename", "**filename %s", filename);
    /* --END ERROR HANDLING-- */
#endif
}

/*
  ADIO_FileSysType_prefix - determines file system type for a file using 
  a prefix on the file name.  upper layer should have already determined
  that a prefix is present.

Input Parameters:
. filename - path to file, including prefix (xxx:)

Output Parameters:
. fstype - pointer to integer in which to store file system type (ADIO_XXX)
. error_code - pointer to integer in which to store error code

  Returns MPI_SUCCESS in error_code on success.  Filename not having a prefix
  is considered an error. Except for on Windows systems where the default is NTFS.

 */
static void ADIO_FileSysType_prefix(char *filename, int *fstype, int *error_code)
{
    static char myname[] = "ADIO_RESOLVEFILETYPE_PREFIX";
    *error_code = MPI_SUCCESS;

    if (!strncmp(filename, "pfs:", 4) || !strncmp(filename, "PFS:", 4)) {
	*fstype = ADIO_PFS;
    }
    else if (!strncmp(filename, "piofs:", 6) || !strncmp(filename, "PIOFS:", 6)) {
	*fstype = ADIO_PIOFS;
    }
    else if (!strncmp(filename, "ufs:", 4) || !strncmp(filename, "UFS:", 4)) {
	*fstype = ADIO_UFS;
    }
    else if (!strncmp(filename, "nfs:", 4) || !strncmp(filename, "NFS:", 4)) {
	*fstype = ADIO_NFS;
    }
    else if (!strncmp(filename, "panfs:", 6) || !strncmp(filename, "PANFS:", 6)) {
	*fstype = ADIO_PANFS;
    }
    else if (!strncmp(filename, "hfs:", 4) || !strncmp(filename, "HFS:", 4)) {
	*fstype = ADIO_HFS;
    }
    else if (!strncmp(filename, "xfs:", 4) || !strncmp(filename, "XFS:", 4)) {
	*fstype = ADIO_XFS;
    }
    else if (!strncmp(filename, "sfs:", 4) || !strncmp(filename, "SFS:", 4)) {
	*fstype = ADIO_SFS;
    }
    else if (!strncmp(filename, "pvfs:", 5) || !strncmp(filename, "PVFS:", 5)) {
	*fstype = ADIO_PVFS;
    }
    else if (!strncmp(filename, "pvfs2:", 6)||!strncmp(filename, "PVFS2:", 6)) {
	*fstype = ADIO_PVFS2;
    }
    else if (!strncmp(filename, "testfs:", 7) 
	     || !strncmp(filename, "TESTFS:", 7))
    {
	*fstype = ADIO_TESTFS;
    }
    else if (!strncmp(filename, "ftp:", 4) 
		    || !strncmp(filename, "gsiftp:", 7))
    {
	*fstype = ADIO_GRIDFTP;
    }
    else {
#ifdef ROMIO_NTFS
	*fstype = ADIO_NTFS;
#else
	*fstype = 0;
        /* --BEGIN ERROR HANDLING-- */
        *error_code = MPIO_Err_create_code(MPI_SUCCESS, MPIR_ERR_RECOVERABLE,
				           myname, __LINE__, MPI_ERR_NO_SUCH_FILE,
				           "**filename", "**filename %s", filename);
        /* --END ERROR HANDLING-- */
#endif
    }
}

/*@
    ADIO_ResolveFileType - determines file system type and operations from
                           file name string; this is a collective call

Input Parameters:
. comm - communicator across which collective open is performed
. filename - name of file (string)

Output Parameters:
. fstype - (pointer to) int holding file system type
. ops - (address of) pointer to table of valid file operations
. error_code - (pointer to) int holding error code

Notes:
This code used to be in MPI_File_open(), but it has been moved into here in 
order to clean things up.  The goal is to separate all this "did we compile
for this fs type" code from the MPI layer and also to introduce the ADIOI_Fns
tables in a reasonable way. -- Rob, 06/06/2001
@*/
void ADIO_ResolveFileType(MPI_Comm comm, char *filename, int *fstype, 
			  ADIOI_Fns **ops, int *error_code)
{
    int myerrcode, file_system, min_code;
    char *tmp;
    static char myname[] = "ADIO_RESOLVEFILETYPE";

    file_system = -1;
    tmp = strchr(filename, ':');
    if (!tmp) {
	/* no prefix; use system-dependent function call to determine type */
	ADIO_FileSysType_fncall(filename, &file_system, &myerrcode);
	if (myerrcode != MPI_SUCCESS) {
	    *error_code = myerrcode;
	    return;
	}

	/* ensure that everyone came up with the same file system type */
	MPI_Allreduce(&file_system, &min_code, 1, MPI_INT, MPI_MIN, comm);
	if (min_code == ADIO_NFS) file_system = ADIO_NFS;

    }
    else {
	/* prefix specified; just match via prefix and assume everyone got 
	 * the same thing.
	 *
	 * perhaps we should have this code go through the allreduce as well?
	 */
	ADIO_FileSysType_prefix(filename, &file_system, &myerrcode);
	if (myerrcode != MPI_SUCCESS) {
	    *error_code = myerrcode;
	    return;
	}
    }

    /* verify that we support this file system type and set ops pointer */
    if (file_system == ADIO_PFS) {
#ifndef ROMIO_PFS
	*error_code = MPIO_Err_create_code(MPI_SUCCESS, MPIR_ERR_RECOVERABLE,
					   myname, __LINE__, MPI_ERR_IO,
					   "**iofstypeunsupported", 0);
	return;
#else
	*ops = &ADIO_PFS_operations;
#endif
    }
    if (file_system == ADIO_PIOFS) {
#ifndef ROMIO_PIOFS
	*error_code = MPIO_Err_create_code(MPI_SUCCESS, MPIR_ERR_RECOVERABLE,
					   myname, __LINE__, MPI_ERR_IO,
					   "**iofstypeunsupported", 0);
	return;
#else
	*ops = &ADIO_PIOFS_operations;
#endif
    }
    if (file_system == ADIO_UFS) {
#ifndef ROMIO_UFS
	*error_code = MPIO_Err_create_code(MPI_SUCCESS, MPIR_ERR_RECOVERABLE,
					   myname, __LINE__, MPI_ERR_IO,
					   "**iofstypeunsupported", 0);
	return;
#else
	*ops = &ADIO_UFS_operations;
#endif
    }
    if (file_system == ADIO_NFS) {
#ifndef ROMIO_NFS
	*error_code = MPIO_Err_create_code(MPI_SUCCESS, MPIR_ERR_RECOVERABLE,
					   myname, __LINE__, MPI_ERR_IO,
					   "**iofstypeunsupported", 0);
	return;
#else
	*ops = &ADIO_NFS_operations;
#endif
    }
    if (file_system == ADIO_PANFS) {
#ifndef ROMIO_PANFS
	*error_code = MPIO_Err_create_code(MPI_SUCCESS, MPIR_ERR_RECOVERABLE,
					   myname, __LINE__, MPI_ERR_IO,
					   "**iofstypeunsupported", 0);
	return;
#else
	*ops = &ADIO_PANFS_operations;
#endif
    }
    if (file_system == ADIO_HFS) {
#ifndef ROMIO_HFS
	*error_code = MPIO_Err_create_code(MPI_SUCCESS, MPIR_ERR_RECOVERABLE,
					   myname, __LINE__, MPI_ERR_IO,
					   "**iofstypeunsupported", 0);
	return;
#else
	*ops = &ADIO_HFS_operations;
#endif
    }
    if (file_system == ADIO_XFS) {
#ifndef ROMIO_XFS
	*error_code = MPIO_Err_create_code(MPI_SUCCESS, MPIR_ERR_RECOVERABLE,
					   myname, __LINE__, MPI_ERR_IO,
					   "**iofstypeunsupported", 0);
	return;
#else
	*ops = &ADIO_XFS_operations;
#endif
    }
    if (file_system == ADIO_SFS) {
#ifndef ROMIO_SFS
	*error_code = MPIO_Err_create_code(MPI_SUCCESS, MPIR_ERR_RECOVERABLE,
					   myname, __LINE__, MPI_ERR_IO,
					   "**iofstypeunsupported", 0);
	return;
#else
	*ops = &ADIO_SFS_operations;
#endif
    }
    if (file_system == ADIO_PVFS) {
#ifndef ROMIO_PVFS
	*error_code = MPIO_Err_create_code(MPI_SUCCESS, MPIR_ERR_RECOVERABLE,
					   myname, __LINE__, MPI_ERR_IO,
					   "**iofstypeunsupported", 0);
	return;
#else
	*ops = &ADIO_PVFS_operations;
#endif
    }
    if (file_system == ADIO_PVFS2) {
#ifndef ROMIO_PVFS2
	*error_code = MPIO_Err_create_code(MPI_SUCCESS, MPIR_ERR_RECOVERABLE,
					   myname, __LINE__, MPI_ERR_IO,
					   "**iofstypeunsupported", 0);
	return;
#else
	*ops = &ADIO_PVFS2_operations;
#endif
    }
    if (file_system == ADIO_NTFS) {
#ifndef ROMIO_NTFS
	*error_code = MPIO_Err_create_code(MPI_SUCCESS, MPIR_ERR_RECOVERABLE,
					   myname, __LINE__, MPI_ERR_IO,
					   "**iofstypeunsupported", 0);
	return;
#else
	*ops = &ADIO_NTFS_operations;
#endif
    }
    if (file_system == ADIO_TESTFS) {
#ifndef ROMIO_TESTFS
	*error_code = MPIO_Err_create_code(MPI_SUCCESS, MPIR_ERR_RECOVERABLE,
					   myname, __LINE__, MPI_ERR_IO,
					   "**iofstypeunsupported", 0);
	return;
#else
	*ops = &ADIO_TESTFS_operations;
#endif
    }
    if (file_system == ADIO_GRIDFTP) {
#ifndef ROMIO_GRIDFTP
	*error_code = MPIO_Err_create_code(MPI_SUCCESS, MPIR_ERR_RECOVERABLE,
					   myname, __LINE__, MPI_ERR_IO,
					   "**iofstypeunsupported", 0);
	return;
#else
	*ops = &ADIO_GRIDFTP_operations;
#endif
    }
    *error_code = MPI_SUCCESS;
    *fstype = file_system;
    return;
}
