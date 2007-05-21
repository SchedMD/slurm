c PING_PONG two-node message exchanges benchmark program
C 
C Contributed by Richard Frost <frost@SDSC.EDU>, caused problems
C on t3d with -device=t3d -arch=cray_t3d -no_short_longs -nodevdebug
C 
C
C This is a very time-consuming program on a workstation cluster.
C For this reason, I've modified it to do fewer tests (1/10 as many)
C
c
c This is a simple benchmark designed to measure the latency and bandwidth
c of a message-passing MIMD computer.  It is currently set up to run with
c MPI.
c
c Compile (MPI mpich version 1.0.11 or later) with
c     % mpif77 -o pong pong.f
c
c (mpif77 is a script that hides details about libraries from the user)
c
c Execute as
c     % mpirun -np 2 pong
c
c Make sure that ~mpi/bin is in your path.
c
c Note that the MPI-specific calls are:
c
c      MPI_INIT
c      MPI_COMM_RANK
c      MPI_COMM_SIZE
c
c      MPI_Wtime
c      MPI_Wtick
c
c      MPI_SEND
c      MPI_RECV
c
c      MPI_FINALIZE
c
c Some care needs to be taken in using the
c appropriate timing routine.  Check the value of MPI_Wtick() to see if
c the clock resolution is reasonable for your tests.
c
c The benchmark measures
c the time to send a message of length N bytes from node 0 to node 1 and
c receive an acknowledging copy of that message which node 1 sends back to 
c node 0.  Note that node 1 waits for the completion of its receive before
c sending the message back to node 0. Note also that the program is not
c necessarily optimal any given system, but is intended
c to provide a reasonably transparent baseline measurement. 
c 
c For message lengths len (= num of doubles * sizedouble),
c a total of msgspersample ping-pong message exchanges are made,	
c and half of the average round-trip time (i.e. the one-way message
c time) is then fit by a linear function y(N) = a + b*N via a least squares
c linear regression.  The coefficient a is then interpreted as the latency
c (time to send a 0-length message) and b as the inverse bandwidth (i.e. 1/b =
c bandwidth in bytes/sec)
c
c The entire procedure is repeated twice, with the bandwidth, latency, and
c measured and fitted values of the message times reported for each instance.
c
c The underlying message passing performance characteristics of a 
c particular system may not necessarily be accurately modeled by the simple
c linear function assumed here.  This may be reflected in a dependency of
c the observed latency and bandwidth on the range of message sizes used.
c
c Original author:
c R. Leary, San Diego Supercomputer Center
c leary@sdsc.edu        9/20/94
c
c Modified for MPI     10/27/95
c frost@sdsc.edu

c
c =========================== program header =========================== 
c

      program pong
      implicit none
      include 'mpif.h'

c sizedouble = size in bytes of double precision element
      integer sizedouble
      parameter(sizedouble=8)

c Note: set these parameters to one of 3 cases:
c  1. size (each sample) < packetization length of architecture
c  2. size (each sample) > packetization length of architecture
c  3. size (1st sample) < packetization length of architecture
c   & size (all others) > packetization length of architecture
c
c  Some known packetization lengths:
c    Paragon            ~1500    bytes
c    Cray T3D           ~1500    bytes
c    TCP/IP networks    256-1024 bytes
c
c samples = the number of data points collected
      integer samples
      parameter(samples=40)
c initsamplesize = # of elements transmitted in 1st sample
      integer initsamplesize
      parameter(initsamplesize=125)
c samplesizeinc = sample size increase per iteration (linear rate)
      integer samplesizeinc
      parameter(samplesizeinc=125)
c     parameter(samplesizeinc=1)
c msgspersample = the number of messages
      integer msgspersample
c      parameter(msgspersample=1000)
       parameter(msgspersample=100)

c The buffer array contains the message , while x(i) is the message size 
c and y(i) the corresponding measured one-way average time. 
c Note that buffer is a double precision array
c
c ibufcount = total number of elements in buffer
      integer ibufcount
      parameter(ibufcount=(initsamplesize+((samples-1)*samplesizeinc)))
c
      double precision buffer(ibufcount)
      double precision x(samples), y(samples)
      double precision t1, t2
      double precision a, b, bandw
      double precision sumx, sumy, sumxx, sumxy
      double precision det, fit

      integer stat(MPI_STATUS_SIZE)
      integer ierr, ierr1, ierr2
      integer nodenum, numprocs
      integer idest
      integer i, iter, sample
      integer num

c
c =========================== begin =========================== 
c

      call MPI_INIT( ierr )
      call MPI_COMM_RANK( MPI_COMM_WORLD, nodenum, ierr )
      call MPI_COMM_SIZE( MPI_COMM_WORLD, numprocs, ierr )

      if (numprocs .ne. 2) then
         write (6,*) 'This program is only valid for 2 processors'
         write (6,*) 'numprocs = ', numprocs
         stop
      endif

c Put something into array
      do 2 i=1,ibufcount
        buffer(i) = dfloat(i)
    2 continue

      if (nodenum .eq. 0) then
         write (6,*) ' MPI pong test'
         write (6,*) ' samples = ', samples
         write (6,*) ' initsamplesize = ', initsamplesize
         write (6,*) ' samplesizeinc = ', samplesizeinc
         write (6,*) ' msgspersample = ', msgspersample
         write (6,*) ' ibufcount = ', ibufcount
         write (6,98) MPI_Wtick()
         write (6,*) 
      endif
   98 format (' clock resolution = ',e10.5)

      call MPI_BARRIER(MPI_COMM_WORLD, ierr)

c
c =========================== main loop =========================== 
c

c Start main loop - iterate twice to generate two complete sets of timings
      do 60 iter = 1,2
      do 40 sample = 1,samples
      num = initsamplesize + ((sample-1)*samplesizeinc)

c debug
      write (6,99) nodenum, iter, sample, num
      call MPI_BARRIER(MPI_COMM_WORLD, ierr)
   99 format ( 1x, 'PE = ', i1, ', iter = ',i1,
     +             ', sample = ', i3, ', num = ', i5 )

c Find initial elapsed time in seconds

      if(nodenum.eq.0) then
c Send message from node 0 to 1 and receive message from 1
        idest = 1
        t1 = MPI_Wtime()
        do 20 i = 1,msgspersample
           call MPI_SEND(buffer, num, MPI_DOUBLE_PRECISION, 
     +              idest, 0, MPI_COMM_WORLD, ierr1)
           call MPI_RECV(buffer, num, MPI_DOUBLE_PRECISION, 
     +              MPI_ANY_SOURCE, MPI_ANY_TAG, MPI_COMM_WORLD,
     +              stat, ierr2)
   20   continue
        t2 = MPI_Wtime()
      else
c Send message from node 1 to 0 and receive message from 0
        idest = 0
        t1 = MPI_Wtime()
        do 21 i = 1,msgspersample
           call MPI_RECV(buffer, num, MPI_DOUBLE_PRECISION, 
     +              MPI_ANY_SOURCE, MPI_ANY_TAG, MPI_COMM_WORLD,
     +              stat, ierr2)
           call MPI_SEND(buffer, num, MPI_DOUBLE_PRECISION, 
     +              idest, 0, MPI_COMM_WORLD, ierr1)
   21   continue
        t2 = MPI_Wtime()
      endif

c independent variable is message length:
      x(sample) = dfloat(num * sizedouble)

c dependent variable is average one-way transit time:
      y(sample) = ((t2 - t1) * 0.5) /
     +            dfloat(msgspersample)

   40 continue

c now do linear least squares fit to data
c time = a + b*x

      if (nodenum .eq. 0) then
      sumy = 0.d0
      sumx = 0.d0
      sumxy = 0.d0
      sumxx = 0. d0
      do 45 i=1,samples
         sumx = sumx + x(i)
         sumy = sumy + y(i)
         sumxy = sumxy + ( x(i) * y(i) )
         sumxx = sumxx + ( x(i) * x(i) )
   45 continue

      det = (dfloat(samples) * sumxx) - (sumx * sumx)
      a = (1.d6 * ((sumxx * sumy) - (sumx * sumxy))) / det
      b = (1.d6 * ((dfloat(samples) * sumxy) - (sumx * sumy))) / det

      write(6,*)
      write(6,*) ' iter = ', iter
      write(6,*)
      write(6,*) ' least squares fit:  time = a + b * (msg length)'
      write(6,200) a
      write(6,300) b
      bandw = 1./b
      write(6,400) bandw
      write(6,*)
      write(6,*) '    message         observed          fitted'
      write(6,*) ' length(bytes)     time(usec)       time(usec)'
      write(6,*)
      do 50 i=1,samples
         fit = a + b*x(i) 
         y(i) = y(i)*1.d6
         write(6,100) x(i),y(i),fit
   50 continue
      endif

   60 continue

c
c =========================== end loop =========================== 
c

  100 format(3x,f8.0,5x,f12.2,5x,f12.2)
  200 format(5x,'a = latency = ',f8.2,' microseconds')
  300 format(5x,'b = inverse bandwidth = ' , f8.5,' secs/Mbyte')     
  400 format(5x,'1/b = bandwidth = ',f8.2,' Mbytes/sec')

c
c =========================== end program =========================== 
c

      call MPI_FINALIZE(ierr)
      stop
      end
