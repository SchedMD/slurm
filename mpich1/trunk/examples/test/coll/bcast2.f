      program test
C
C This program hangs when run with the version of MPICH (1.1.2) distributed
C by Myricom using their ch_gm device.  I've added it to our collection
C on general principle; note that it hasn't been put into a form usable
C by our tests yet
C
      include 'mpif.h'
      integer comm_size,comm_rank,status(mpi_status_size)
      integer at_number,chunk
      double precision T0,D
      at_number=0
      chunk=0
      T0=3D3048.48883
      D=3D3877.4888
      call mpi_init(ierror)
      call mpi_comm_size(mpi_comm_world,comm_size,ierror)
      call mpi_comm_rank(mpi_comm_world,comm_rank,ierror)
      CALL MPI_BCAST(at_number,1,mpi_integer,0,mpi_comm_world,ierr)
      CALL MPI_BCAST(chunk,1,mpi_integer,0,mpi_comm_world,ierr)
      CALL MPI_BCAST(T0,1,mpi_double_precision,0,mpi_comm_world,ierr)
      CALL MPI_BCAST(D,1,mpi_double_precision,0,mpi_comm_world,ierr)

      write(6,*) 'Rank=3D',comm_rank,' finished bcast'
      do i=3D1,99999
        T0=3Di*1.0d0
        d=3Dt0**.987
        do j=3D1,100
	    a=3Dj**.2
        enddo
      enddo
      write(6,*) 'Rank=3D',comm_rank,' finished calculations'
      call mpi_finalize(ierror)
      stop
      en
C 
C Run with mpirun -np 16 test
