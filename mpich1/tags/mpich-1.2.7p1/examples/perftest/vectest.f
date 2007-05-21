      program main
c
      include 'mpif.h'
      parameter (nx=16, ny=16, nz=16)
      integer sx, sy, sz, ex, ey, ez, sizeof
      integer newx, newx1, newy, newz
      integer ierror
      integer status(MPI_STATUS_SIZE)
      integer i, size1, size2, size3
      double precision a(nx,ny,nz)
      double precision t1, ttmp
c
      call MPI_Init( ierror )
c
      sx = 8
      ex = 12
      sy = 4
      ey = 8
      sz = 6
      ez = 10
c
c     Example program to move data around a 3-d cube using a single
c     processor and derived datatypes
c      
c     See the book 'Using MPI' for an explanation
c
c     a(sx:ex,sy:ez:k)
      call mpi_type_vector( ey-sy+1, ex-sx+1, nx, 
     *                      MPI_DOUBLE_PRECISION, newz, ierror )
      call mpi_type_commit( newz, ierror )
      size1 = (ey-sy+1) * (ex-sx+1)
c
c     a(sx:ex,j,sz:ez)
      call mpi_type_vector( ez-sz+1, ex-sx+1, nx*ny, 
     *                      MPI_DOUBLE_PRECISION, newy, ierror )
      call mpi_type_commit( newy, ierror )
      size2 = (ez-sz+1) * (ex-sx+1)
c
c     a(i,sy:ey,sz:ez)
      call mpi_type_vector( ey-sy+1, 1, nx,
     *                      MPI_DOUBLE_PRECISION, newx1, ierror )
      call mpi_type_extent( MPI_DOUBLE_PRECISION, sizeof, ierror )
      call mpi_type_hvector( ez-sz+1, 1, nx*ny*sizeof, 
     *                       newx1, newx, ierror )
      call mpi_type_commit( newx, ierror )
      size3 = (ey-sy+1) * (ez-sz+1)
c
      ttmp = MPI_Wtime()
      do 10 i=1,100

c
      call mpi_sendrecv( a, 1, newz, 0, 0, a(1,1,4), 1, newz, 0, 0, 
     *                   MPI_COMM_WORLD, status, ierror )
c
      call mpi_sendrecv( a, 1, newy, 0, 0, a(1,4,1), 1, newy, 0, 0,
     *                   MPI_COMM_WORLD, status, ierror )
c
      call mpi_sendrecv( a, 1, newx, 0, 0, a(1,1,4), 1, newx, 0, 0,
     *                   MPI_COMM_WORLD, status, ierror )
 10   continue
      t1 = MPI_Wtime() - ttmp
      print *, 'Time to perform 100 steps = ', t1, ' secs'
      print *, 'Rate is ', 100 * ( size1 * size2 * size3 ) / t1, 
     *         ' dbl/sec'
c
      call mpi_type_free( newx, ierror )
      call mpi_type_free( newy, ierror )
      call mpi_type_free( newz, ierror )
      call mpi_type_free( newx1, ierror )
c
      call MPI_Finalize( ierror )
      end
