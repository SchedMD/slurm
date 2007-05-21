c
c This test looks at sending some data with a count of zero.
c
      program testmpi
      integer           mnprocs, lcwk1
      parameter         ( mnprocs = 2, lcwk1 = 6 )
      integer           comm, rc, myid, nprocs, ierr, i,
     &                  recvts(0:mnprocs-1), displs(0:mnprocs-1)
      double precision  wrkbuf(3), cwk1(lcwk1)
      include           'mpif.h'
c
      call MPI_INIT( ierr )
      comm = MPI_COMM_WORLD
      call MPI_COMM_RANK( comm, myid, ierr )
      call MPI_COMM_SIZE( comm, nprocs, ierr )
c
      do i = 1, lcwk1
         cwk1(i) = -10
      end do
      do i=1,3
         wrkbuf(i) = myid
      end do
      do i = 0, mnprocs-1
         recvts(i) = 3
         displs(i) = 3 * i
      end do
      recvts(mnprocs-1) = 0
      displs(mnprocs-1) = 0
c
      call MPI_ALLGATHERV( wrkbuf, recvts(myid), 
     &                     MPI_DOUBLE_PRECISION, cwk1, recvts, 
     &                     displs, MPI_DOUBLE_PRECISION, comm, ierr )
c 
      do i = 1, lcwk1
         print *, myid, i, cwk1(i)
      end do
c
      call MPI_FINALIZE(rc)
c
      end
c
