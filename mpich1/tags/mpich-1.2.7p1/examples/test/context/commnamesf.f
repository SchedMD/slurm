C
C Check the communicator naming functions from Fortran
C

      include 'mpif.h'

      integer error, namelen
      integer errcnt, rank
      character*40 the_name
      character*40 other_name

      call mpi_init (error)
      
      errcnt = 0
      call xify(the_name)

      call mpi_comm_get_name (MPI_COMM_WORLD, the_name, namelen, error)
      if (error .ne. mpi_success) then
         errcnt = errcnt + 1
         print *,'Failed to get the name from MPI_COMM_WORLD'
         call MPI_Abort( MPI_COMM_WORLD, 1, error )
      end if

      if (the_name .ne. 'MPI_COMM_WORLD') then
         errcnt = errcnt + 1
         print *,'The name on MPI_COMM_WORLD is not "MPI_COMM_WORLD"'
         call MPI_Abort( MPI_COMM_WORLD, 1, error )
      end if

      other_name = 'foobarH'
      call mpi_comm_set_name(MPI_COMM_WORLD, other_name(1:6), error)

      if (error .ne. mpi_success) then
         errcnt = errcnt + 1
         print *,'Failed to put a name onto MPI_COMM_WORLD'
         call MPI_Abort( MPI_COMM_WORLD, 1, error )
      end if
      
      call xify(the_name)

      call mpi_comm_get_name (MPI_COMM_WORLD, the_name, namelen, error)
      if (error .ne. mpi_success) then
         errcnt = errcnt + 1
         print *,'Failed to get the name from MPI_COMM_WORLD ',
     $        'after setting it'
         call MPI_Abort( MPI_COMM_WORLD, 1, error )
      end if

      if (the_name .ne. 'foobar') then
         errcnt = errcnt + 1
         print *,'The name on MPI_COMM_WORLD is not "foobar"'
         print *, 'Got ', the_name
         call MPI_Abort( MPI_COMM_WORLD, 1, error )
      end if

      call mpi_comm_rank( MPI_COMM_WORLD, rank, error )
      if (errcnt .eq. 0 .and. rank .eq. 0) then
         print *, ' No Errors'
      endif
      call mpi_finalize(error)
      end


      subroutine xify( string )
      character*(*) string

      integer i

      do i = 1,len(string)
         string(i:i) = 'X'
      end do

      end

      
