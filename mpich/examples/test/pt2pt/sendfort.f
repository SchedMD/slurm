      program main
C
C     Test Fortran logical data
C
      implicit none
      include 'mpif.h'
      integer ierr, n, tag, status(MPI_STATUS_SIZE), size, rank, i
      integer errs, nrecv
      logical l(1000)
C
      call mpi_init( ierr )
      call mpi_comm_size( MPI_COMM_WORLD, size, ierr )
      call mpi_comm_rank( MPI_COMM_WORLD, rank, ierr )
C
      n = 100
      do i=1, n
         l(i) = i .lt. n/2
      enddo
      tag = 27
      if (rank .eq. 1) then
         call MPI_Send( l, n, MPI_LOGICAL, 0, tag, MPI_COMM_WORLD, ierr
     $        )
      else if (rank .eq. 0) then
         call MPI_Recv( l, n, MPI_LOGICAL, 1, tag, MPI_COMM_WORLD,
     $        status, ierr )
C         Check results
         call MPI_Get_count( status, MPI_LOGICAL, nrecv, ierr )
         if (nrecv .ne. n) then
            print *, 'Wrong count for logical data'
         endif
         errs = 0
         do i=1, n
            if (l(i) .neqv. (i .lt. n/2)) then
               errs = errs + 1
               print *, 'Error in logical entry ', i
            endif
         enddo
         if (errs .gt. 0) then
            print *, ' Found ', errs, ' errors'
         else
            print *, ' No Errors'
         endif
      endif
C
      call mpi_finalize( ierr )
C
      end
