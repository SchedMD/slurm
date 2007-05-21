C
C    second - test program that sends an array of floats from the first process
C             of a group to the last, using send and recv
C
C
      program main
      include 'mpif.h'
C
      integer rank, size, to, from, tag, count, i, ierr
      integer src, dest
      integer st_source, st_tag, st_count
C      MPI_Status status
      integer status(MPI_STATUS_SIZE)
      double precision data(100)

      call MPI_INIT( ierr )
C      print *, 'about to call comm rank'
      call MPI_COMM_RANK( MPI_COMM_WORLD, rank, ierr )
C      print *, rank, 'about to call comm size'
      call MPI_COMM_SIZE( MPI_COMM_WORLD, size, ierr )
      print *, 'Process ', rank, ' of ', size, ' is alive'
C
C      src = size - 1
C      dest = 0
      dest = size - 1
      src = 0
C      
      if (rank .eq. src) then
         to     = dest
         count  = 10
         tag    = 2001
         do 10 i=1, 10
            data(i) = i
 10      continue
         call MPI_SEND( data, count, MPI_DOUBLE_PRECISION, to, tag, 
     &                  MPI_COMM_WORLD, ierr )
         print *, rank, ' sent'
         print *, (data(i),i=1,10)
      elseif (rank .eq. dest) then
         tag   = MPI_ANY_TAG
         count = 10		
         from  = MPI_ANY_SOURCE
         call MPI_RECV(data, count, MPI_DOUBLE_PRECISION, from, tag, 
     &                 MPI_COMM_WORLD, status, ierr ) 
            
         call MPI_GET_COUNT( status, MPI_DOUBLE_PRECISION, 
     &                       st_count, ierr )
         st_source = status(MPI_SOURCE)
         st_tag    = status(MPI_TAG)
c         
         print *, 'Status info: source = ', st_source, 
     &             ' tag = ', st_tag, ' count = ', st_count
         print *, rank, ' received', (data(i),i=1,10)
      endif
        
        call MPI_FINALIZE( ierr )
        print *, 'Process ', rank, ' exiting'
        end

