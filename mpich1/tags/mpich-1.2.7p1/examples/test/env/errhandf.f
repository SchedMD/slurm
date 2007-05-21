C
C Test that error handlers can be applied and used through Fortran
C
      program main

      include 'mpif.h'
      integer ierr, errorclass
      integer buf, errors, request
C
      call mpi_init(ierr)
C 
C  Try to set the errors-return handler
C 
      call mpi_errhandler_set(mpi_comm_world, mpi_errors_return, ierr)
      errors = 0
C
C Activate the handler with a simple case
C 
      call mpi_send( buf, 1, MPI_INTEGER, -99, 0, MPI_COMM_WORLD, ierr )
      if (IERR .eq. MPI_SUCCESS) then
         errors = errors + 1
         print *, 'MPI_Send of negative rank did not return error'
      endif
C
C Check for a reasonable error message      
      call mpi_error_class(ierr, errorclass, err)
      if (errorclass .ne. MPI_ERR_RANK) then
         errors = errors + 1
         print *, 'Error class was not MPI_ERR_RANK, was ', errorclass
      endif
C
C Activate the handler with a simple case
C 
      call mpi_irecv( buf, 1, MPI_INTEGER, -100, 2, MPI_COMM_WORLD, 
     *                request, ierr )
      if (IERR .eq. MPI_SUCCESS) then
         errors = errors + 1
         print *, 'MPI_Irecv of negative rank did not return error'
      endif
C
C Check for a reasonable error message      
      call mpi_error_class(ierr, errorclass, err)
      if (errorclass .ne. MPI_ERR_RANK) then
         errors = errors + 1
         print *, 'Error class was not MPI_ERR_RANK, was ', errorclass
      endif

      if (errors .eq. 0) then
         print *, ' No Errors'
      else
         print *, ' Found ', errors, ' errors'
      endif
C         
      call mpi_finalize(ierr)
C
      end
