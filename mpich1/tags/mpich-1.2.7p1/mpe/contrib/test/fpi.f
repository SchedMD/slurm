c**********************************************************************
c   pi.f - compute pi by integrating f(x) = 4/(1 + x**2)     
c     
c   Each node: 
c    1) receives the number of rectangles used in the approximation.
c    2) calculates the areas of it's rectangles.
c    3) Synchronizes for a global summation.
c   Node 0 prints the result.
c
c  Variables:
c
c    pi  the calculated result
c    n   number of points of integration.  
c    x           midpoint of each rectangle's interval
c    f           function to integrate
c    sum,pi      area of rectangles
c    tmp         temporary scratch space for global summation
c    i           do loop index
c****************************************************************************
      program main

      include 'mpif.h'

      double precision  PI25DT
      parameter        (PI25DT = 3.141592653589793238462643d0)

      double precision  mypi, pi, h, sum, x, f, a
      integer n, myid, numprocs, i, rc, idx

      integer event1a, event1b, event2a, event2b
      integer event3a, event3b, event4a, event4b
      integer ierr

c                                 function to integrate
      f(a) = 4.d0 / (1.d0 + a*a)

      call MPI_INIT( ierr )

      call MPI_Pcontrol( 0, ierr )

      call MPI_COMM_RANK( MPI_COMM_WORLD, myid, ierr )
      call MPI_COMM_SIZE( MPI_COMM_WORLD, numprocs, ierr )
      print *, "Process ", myid, " of ", numprocs, " is alive"

      sizetype   = 1
      sumtype    = 2
      
      event1a = MPE_Log_get_event_number()
      event1b = MPE_Log_get_event_number()
      event2a = MPE_Log_get_event_number()
      event2b = MPE_Log_get_event_number()
      event3a = MPE_Log_get_event_number()
      event3b = MPE_Log_get_event_number()
      event4a = MPE_Log_get_event_number()
      event4b = MPE_Log_get_event_number()

      if ( myid .eq. 0 ) then
          ierr = MPE_Describe_state( event1a, event1b,
     &                               "User_Broadcast", "red" )
          ierr = MPE_Describe_state( event2a, event2b,
     &                               "User_Barrier", "blue" )
          ierr = MPE_Describe_state( event3a, event3b,
     &                               "User_Compute", "orange" )
          ierr = MPE_Describe_state( event4a, event4b,
     &                               "User_Reduce", "green" )
          write(6,*) "events ID are ", event1a, event1b,
     &                                 event2a, event2b
      endif

      if ( myid .eq. 0 ) then
C         write(6,98)
C 98      format('Enter the number of intervals: (0 quits)')
C         read(5,99) n
C 99      format(i10)
          n = 1000000
          write(6,*) 'The number of intervals =', n
      endif

      call MPI_BARRIER( MPI_COMM_WORLD, ierr )
      call MPI_Pcontrol( 1, ierr )

      do idx = 1, 2

          ierr = MPE_Log_event( event1a, 0, '' )
          call MPI_BCAST(n,1,MPI_INTEGER,0,MPI_COMM_WORLD,ierr)
          ierr = MPE_Log_event( event1b, 0, '' )

c                                 check for quit signal
C         if ( n .le. 0 ) goto 30

          ierr = MPE_Log_event( event2a, 0, '' )
          call MPI_Barrier( MPI_COMM_WORLD, ierr )
          ierr = MPE_Log_event( event2b, 0, '' )

c                                 calculate the interval size
          ierr = MPE_Log_event( event3a, 0, '' )
          h = 1.0d0/n

          sum  = 0.0d0
          do 20 i = myid+1, n, numprocs
              x = h * (dble(i) - 0.5d0)
              sum = sum + f(x)
 20       continue
          mypi = h * sum
          ierr = MPE_Log_event( event3b, 0, '' )

c                                 collect all the partial sums
          ierr = MPE_Log_event( event4a, 0, '' )
          call MPI_REDUCE(mypi,pi,1,MPI_DOUBLE_PRECISION,MPI_SUM,0,
     $                    MPI_COMM_WORLD,ierr)
          ierr = MPE_Log_event( event4b, 0, '' )

c                                 node 0 prints the answer.
          if ( myid .eq. 0 ) then
              write(6, 97) pi, abs(pi - PI25DT)
 97           format('  pi is approximately: ', F18.16,
     +               '  Error is: ', F18.16)
          endif

      enddo

 30   call MPI_FINALIZE(rc)

      end
