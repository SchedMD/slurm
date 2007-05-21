      program main
c     test bcast of logical
c     works on suns, needs mpich fix and heterogeneous test on alpha with PC
      include 'mpif.h'
      integer myid, numprocs, rc, ierr
      integer errs, toterrs
      logical boo

      call MPI_INIT( ierr )
      call MPI_COMM_RANK( MPI_COMM_WORLD, myid, ierr )
      call MPI_COMM_SIZE( MPI_COMM_WORLD, numprocs, ierr )
C
      errs = 0
      boo = .true.
      call MPI_BCAST(boo,1,MPI_LOGICAL,0,MPI_COMM_WORLD,ierr)
      if (boo .neqv. .true.) then 
         print *, 'Did not broadcast Fortran logical (true)'
         errs = errs + 1
      endif
C
      boo = .false.
      call MPI_BCAST(boo,1,MPI_LOGICAL,0,MPI_COMM_WORLD,ierr)
      if (boo .neqv. .false.) then 
         print *, 'Did not broadcast Fortran logical (false)'
         errs = errs + 1
      endif
      call MPI_Reduce( errs, toterrs, 1, MPI_INTEGER, MPI_SUM, 
     $                 0, MPI_COMM_WORLD, ierr )
      if (myid .eq. 0) then
         if (toterrs .eq. 0) then
            print *, ' No Errors'
         else
            print *, ' Found ', toterrs, ' errors'
         endif
      endif
      call MPI_FINALIZE(rc)
      stop
      end
