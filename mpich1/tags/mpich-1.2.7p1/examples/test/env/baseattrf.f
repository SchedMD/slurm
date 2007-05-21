
      program main
      integer err, ierr
      integer v
      logical  flag
      integer  rank, size
      include 'mpif.h'

      err = 0
      call MPI_Init( ierr )
      call MPI_Comm_size( MPI_COMM_WORLD, size, ierr )
      call MPI_Comm_rank( MPI_COMM_WORLD, rank, ierr )
      call MPI_Attr_get( MPI_COMM_WORLD, MPI_TAG_UB, v, flag, ierr )
      if (.not. flag .or. v .lt. 32767) then
         err = err + 1
         print *, 'Could not get TAG_UB or got too-small value', v
      endif
c
      call MPI_Attr_get( MPI_COMM_WORLD, MPI_HOST, v, flag, ierr )
      if (.not. flag .or. ((v .lt. 0 .or. v .ge. size) .and.
     *                     v .ne. MPI_PROC_NULL)) then
         err = err + 1
         print *, 'Could not get HOST or got invalid value', v
      endif
c
      call MPI_Attr_get( MPI_COMM_WORLD, MPI_IO, v, flag, ierr )
      if (.not. flag .or. (( v .lt. 0 .or. v .gt. size) .and.
     *                       v .ne. MPI_PROC_NULL .and.
     *                       v .ne. MPI_ANY_SOURCE)) then
         err = err + 1
         print *, 'Could not get IO or got invalid value', v
      endif
      call MPI_Finalize( ierr )

      end
