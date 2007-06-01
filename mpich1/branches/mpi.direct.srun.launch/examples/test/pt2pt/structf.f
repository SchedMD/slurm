C Thanks to 
C William R. Magro
C for this test
C
C It has been modifiedly slightly to work with the automated MPI
C tests.
C  WDG.
C
      program bustit
      implicit none

      include 'mpif.h'

      integer ierr
      integer comm
      integer newtype
      integer me
      integer position
      integer type(5)
      integer length(5)
      integer disp(5)
      integer bufsize
      parameter (bufsize=100)
      character buf(bufsize)
      character name*(10)
      integer status(MPI_STATUS_SIZE)
      integer i, size
      double precision x
      integer src, dest

C     Enroll in MPI
      call mpi_init(ierr)

C     get my rank
      call mpi_comm_rank(MPI_COMM_WORLD, me, ierr)
      call mpi_comm_size(MPI_COMM_WORLD, size, ierr )
      if (size .lt. 2) then
         print *, "Must have at least 2 processes"
         call MPI_Abort( 1, MPI_COMM_WORLD, ierr )
      endif

      comm = MPI_COMM_WORLD
      src = 0
      dest = 1

      if(me.eq.src) then
          i=5
          x=5.1234d0
          name="hello"

          type(1)=MPI_CHARACTER
          length(1)=5
          call mpi_address(name,disp(1),ierr)

          type(2)=MPI_DOUBLE_PRECISION
          length(2)=1
          call mpi_address(x,disp(2),ierr)

          call mpi_type_struct(2,length,disp,type,newtype,ierr)
          call mpi_type_commit(newtype,ierr)
          call mpi_barrier( MPI_COMM_WORLD, ierr )
          call mpi_send(MPI_BOTTOM,1,newtype,dest,1,comm,ierr)
          call mpi_type_free(newtype,ierr)
C         write(*,*) "Sent ",name(1:5),x
      else 
C         Everyone calls barrier incase size > 2
          call mpi_barrier( MPI_COMM_WORLD, ierr )
          if (me.eq.dest) then
             position=0

             name = " "
             x    = 0.0d0
             call mpi_recv(buf,bufsize,MPI_PACKED, src,
     .            1, comm, status, ierr)
             
             call mpi_unpack(buf,bufsize,position,
     .            name,5,MPI_CHARACTER, comm,ierr)
             call mpi_unpack(buf,bufsize,position,
     .            x,1,MPI_DOUBLE_PRECISION, comm,ierr)
             print 1, name, x
 1           format( " Received ", a, f7.4 )
          endif
      endif

      call mpi_finalize(ierr)

      end
