      program main
      include 'mpif.h'
      integer ierr, errs, toterrs
      integer request
      integer status(MPI_STATUS_SIZE)
      integer rank, size, buf(10)
      logical flag
C
      call MPI_Init( ierr )
      errs = 0
C
      call MPI_Comm_size( MPI_COMM_WORLD, size, ierr )
      if (size .lt. 2) then
         print *, 'Must have at least two processes'
         call MPI_Abort( MPI_COMM_WORLD, 1, ierr )
      endif
      call MPI_Comm_rank( MPI_COMM_WORLD, rank, ierr )
      if (rank .eq. 0) then
         do i = 1, 10
            buf(i) = i
         enddo
         call MPI_Isend( buf, 10, MPI_INTEGER, size - 1, 1,
     $        MPI_COMM_WORLD, request, ierr )
         call MPI_Wait( request, status, ierr )
      endif
      if (rank .eq. size - 1) then
         call MPI_Irecv( buf, 10, MPI_INTEGER, 0, 1, MPI_COMM_WORLD,
     $        request, ierr )
C         call MPI_Wait( request, status, ierr )
         flag = .FALSE.
         do while (.not. flag) 
            call MPI_Test( request, flag, status, ierr )
         enddo
C     
C     Check the results
         do i = 1, 10
            if (buf(i) .ne. i) then
               errs = errs + 1
            endif
         enddo
      endif
C
      call MPI_Allreduce( errs, toterrs, 1, MPI_INTEGER, MPI_SUM,
     $     MPI_COMM_WORLD, ierr )
      if (rank .eq. 0) then
         if (toterrs .gt. 0) then
            print *, "Found ", toterrs, " Errors"
         else
            PRINT *, " No Errors"
         endif
      endif
      call MPI_Finalize( ierr )
      stop
      end
