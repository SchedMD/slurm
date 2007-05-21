/* -*- Mode: C; c-basic-offset:4 ; -*- */
/*  
 *  (C) 2004 by Argonne National Laboratory.
 *      See COPYRIGHT in top-level directory.
 */
#include <stdio.h>
#include <fcntl.h>
#include <errno.h>
#include <unistd.h>

/*
 * This program tests to see if fcntl returns success when asked to 
 * establish a file lock.  This test is intended for use on file systems
 * such as NFS that may not implement file locks.  ROMIO makes use
 * of file locks to implement certain operations, and may not work
 * properly if file locks are not available.  
 *
 * This is a simple test and has at least two limitations:
 * 
 * 1. Some implementations of NFS are known to return success for 
 * setting a file lock when in fact no lock has been set.  This
 * test will not detect such erroneous implementations of NFS
 *
 * 2. Some implementations will hang (enter and wait indefinitately)
 * within the fcntl call.  This program will also hang in that case.
 * Under normal conditions, this program should only take a few seconds to 
 * run.
 *
 * The program prints a message showing the success or failure of
 * setting the file lock and sets the return status to 0 on success and
 * non-zero on failure.  If there is a failure, the system routine
 * perror is also called to explain the reason.
 */

/* style: allow:printf:2 sig:0 */

int main( int argc, char *argv[] )
{
    struct flock lock;
    int fd, err;
    char *filename;

    /* Set the filename.  Either arg[1] or conftest.dat */
    if (argc > 1 && argv[1]) {
      filename = argv[1];
    }
    else {
      filename = "conftest.dat";
    }

       
    lock.l_type   = F_WRLCK;
    lock.l_start  = 0;
    lock.l_whence = SEEK_SET;
    lock.l_len    = 100;

    fd = open(filename, O_RDWR | O_CREAT, 0644);

    err = fcntl(fd, F_SETLKW, &lock);

    if (err) {
      printf( "Failed to set a file lock on %s\n", filename );
      perror( "Reason " );
    }
    else {
      printf( "fcntl claims success in setting a file lock on %s\n", filename );
    }
   /* printf("err = %d, errno = %d\n", err, errno); */
    close(fd);
    unlink( filename );
    return err;
}
