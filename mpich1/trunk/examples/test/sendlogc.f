      PROGRAM test

      IMPLICIT NONE

      include "mpif.h"

      logical  l
      integer  e, myid, st(MPI_STATUS_SIZE), to, from

      call mpi_init     ( e )
      call mpi_comm_rank( mpi_comm_world, myid , e )

      call mpi_barrier( mpi_comm_world, e )
      l = .false.
      to = 1
      from = 0
      if( myid .eq. to ) then
        call mpi_recv( l,1,MPI_LOGICAL,from,0,mpi_comm_world,st,e )
        if (.not. l) then
           print *, 'Received false for logical value'
        endif
      else if (myid .eq. from ) then
        l = .true.
        call mpi_send( l,1,MPI_LOGICAL,to,0,mpi_comm_world,   e )
      endif

      if (myid .eq. 0) then
         print *, 'End of test'
      endif
      call mpi_barrier( mpi_comm_world, e )
      call mpi_finalize( e )
      end

