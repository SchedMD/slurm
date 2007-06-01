c
c This program was inspired by a bug report from 
c fsset@corelli.lerc.nasa.gov (Scott Townsend)
c The original version of this program was submitted by email to 
c mpi-bugs and is in the directory mpich/bugs/ssend (not distributed 
c with the distribution).  This program was modified by William
c Gropp (to correct a few errors and make more consistent with the
c structure of the test programs in the examples/test/pt2pt directory.

c A C version of this program is in allpairc.c
c
c This version is intended to test for memory leaks; it runs each test
c a number of times (TEST_COUNT + some in test_pair).
c 
      program allpair2
      include 'mpif.h'
      integer ierr

      call MPI_Init(ierr)

      call test_pair

      call MPI_Finalize(ierr)

      end

c------------------------------------------------------------------------------
c
c  Simple pair communication exercises.
c
c------------------------------------------------------------------------------
      subroutine test_pair
      include 'mpif.h'
      integer TEST_SIZE, TEST_COUNT
      parameter (TEST_SIZE=2000)
      parameter (TEST_COUNT=100)

      integer ierr, prev, next, count, tag, index, i, outcount,
     .        requests(2), indices(2), rank, size, 
     .        status(MPI_STATUS_SIZE), statuses(MPI_STATUS_SIZE,2)
      integer dupcom
      integer c
      logical flag
      real send_buf( TEST_SIZE ), recv_buf ( TEST_SIZE )

      call MPI_Comm_rank( MPI_COMM_WORLD, rank, ierr )
      call MPI_Comm_size( MPI_COMM_WORLD, size, ierr )
      call MPI_Comm_dup( MPI_COMM_WORLD, dupcom, ierr )
      next = rank + 1
      if (next .ge. size) next = 0

      prev = rank - 1
      if (prev .lt. 0) prev = size - 1
c
c     Normal sends
c
      if (rank .eq. 0) then
         print *, '    Send'
         end if

      tag = 1123
      count = TEST_SIZE / 5

      do 111 c=1, TEST_COUNT+1

      call clear_test_data(recv_buf,TEST_SIZE)

      if (rank .eq. 0) then

         call init_test_data(send_buf,TEST_SIZE)

         call MPI_Send(send_buf, count, MPI_REAL, next, tag,
     .                 MPI_COMM_WORLD, ierr) 

         call MPI_Recv(recv_buf, TEST_SIZE, MPI_REAL,
     .                 MPI_ANY_SOURCE, MPI_ANY_TAG, MPI_COMM_WORLD,
     .                 status, ierr)

         call msg_check( recv_buf, prev, tag, count, status, TEST_SIZE,
     .                   'send and recv' )

      else

         call MPI_Recv(recv_buf, TEST_SIZE, MPI_REAL,
     .                 MPI_ANY_SOURCE, MPI_ANY_TAG, MPI_COMM_WORLD,
     .                 status, ierr)

         call msg_check( recv_buf, prev, tag, count, status, TEST_SIZE,
     .                   'send and recv' )

         call MPI_Send(recv_buf, count, MPI_REAL, next, tag,
     .                 MPI_COMM_WORLD, ierr) 
         end if
 111  continue
c
c     Ready sends.  Note that we must insure that the receive is posted
c     before the rsend; this requires using Irecv.
c
      if (rank .eq. 0) then
         print *, '    Rsend'
         end if

      tag = 1456
      count = TEST_SIZE / 3

      do 112 c = 1, TEST_COUNT+2
      call clear_test_data(recv_buf,TEST_SIZE)

      if (rank .eq. 0) then

         call init_test_data(send_buf,TEST_SIZE)

         call MPI_Recv( MPI_BOTTOM, 0, MPI_INTEGER, next, tag, 
     .                  MPI_COMM_WORLD, status, ierr )

         call MPI_Rsend(send_buf, count, MPI_REAL, next, tag,
     .                  MPI_COMM_WORLD, ierr) 

         call MPI_Probe(MPI_ANY_SOURCE, tag,
     .                  MPI_COMM_WORLD, status, ierr) 

         if (status(MPI_SOURCE) .ne. prev) then
            print *, 'Incorrect source, expected', prev,
     .               ', got', status(MPI_SOURCE)
            end if

         if (status(MPI_TAG) .ne. tag) then
            print *, 'Incorrect tag, expected', tag,
     .               ', got', status(MPI_TAG)
            end if

         call MPI_Get_count(status, MPI_REAL, i, ierr)

         if (i .ne. count) then
            print *, 'Incorrect count, expected', count,
     .               ', got', i
            end if

         call MPI_Recv(recv_buf, TEST_SIZE, MPI_REAL,
     .                 MPI_ANY_SOURCE, MPI_ANY_TAG, MPI_COMM_WORLD,
     .                 status, ierr)

         call msg_check( recv_buf, prev, tag, count, status, TEST_SIZE,
     .                   'rsend and recv' )

      else

         call MPI_Irecv(recv_buf, TEST_SIZE, MPI_REAL,
     .                 MPI_ANY_SOURCE, MPI_ANY_TAG, MPI_COMM_WORLD,
     .                 requests(1), ierr)
         call MPI_Send( MPI_BOTTOM, 0, MPI_INTEGER, next, tag, 
     .                  MPI_COMM_WORLD, ierr )
         call MPI_Wait( requests(1), status, ierr )

         call msg_check( recv_buf, prev, tag, count, status, TEST_SIZE,
     .                   'rsend and recv' )

         call MPI_Send(recv_buf, count, MPI_REAL, next, tag,
     .                  MPI_COMM_WORLD, ierr) 
         end if
 112  continue
c
c     Synchronous sends
c
      if (rank .eq. 0) then
         print *, '    Ssend'
         end if

      tag = 1789
      count = TEST_SIZE / 3

      do 113 c = 1, TEST_COUNT+3
      call clear_test_data(recv_buf,TEST_SIZE)

      if (rank .eq. 0) then

         call init_test_data(send_buf,TEST_SIZE)

         call MPI_Iprobe(MPI_ANY_SOURCE, tag,
     .                   MPI_COMM_WORLD, flag, status, ierr) 

         if (flag) then
            print *, 'Iprobe succeeded! source', status(MPI_SOURCE),
     .               ', tag', status(MPI_TAG)
            end if

         call MPI_Ssend(send_buf, count, MPI_REAL, next, tag,
     .                  MPI_COMM_WORLD, ierr) 

         do while (.not. flag)
            call MPI_Iprobe(MPI_ANY_SOURCE, tag,
     .                      MPI_COMM_WORLD, flag, status, ierr) 
            end do

         if (status(MPI_SOURCE) .ne. prev) then
            print *, 'Incorrect source, expected', prev,
     .               ', got', status(MPI_SOURCE)
            end if

         if (status(MPI_TAG) .ne. tag) then
            print *, 'Incorrect tag, expected', tag,
     .               ', got', status(MPI_TAG)
            end if

         call MPI_Get_count(status, MPI_REAL, i, ierr)

         if (i .ne. count) then
            print *, 'Incorrect count, expected', count,
     .               ', got', i
            end if

         call MPI_Recv(recv_buf, TEST_SIZE, MPI_REAL,
     .                 MPI_ANY_SOURCE, MPI_ANY_TAG, MPI_COMM_WORLD,
     .                 status, ierr)

         call msg_check( recv_buf, prev, tag, count, status,
     $        TEST_SIZE, 'ssend and recv' ) 

      else

         call MPI_Recv(recv_buf, TEST_SIZE, MPI_REAL,
     .                 MPI_ANY_SOURCE, MPI_ANY_TAG, MPI_COMM_WORLD,
     .                 status, ierr)

         call msg_check( recv_buf, prev, tag, count, status, TEST_SIZE,
     .                   'ssend and recv' )

         call MPI_Ssend(recv_buf, count, MPI_REAL, next, tag,
     .                  MPI_COMM_WORLD, ierr) 
         end if
 113  continue
c
c     Nonblocking normal sends
c
      if (rank .eq. 0) then
         print *, '    Isend'
         end if

      tag = 2123
      count = TEST_SIZE / 5

      do 114 c = 1, TEST_COUNT+4
      call clear_test_data(recv_buf,TEST_SIZE)

      if (rank .eq. 0) then

         call MPI_Irecv(recv_buf, TEST_SIZE, MPI_REAL,
     .                  MPI_ANY_SOURCE, MPI_ANY_TAG, MPI_COMM_WORLD,
     .                  requests(1), ierr)

         call init_test_data(send_buf,TEST_SIZE)

         call MPI_Isend(send_buf, count, MPI_REAL, next, tag,
     .                  MPI_COMM_WORLD, requests(2), ierr) 

         call MPI_Waitall(2, requests, statuses, ierr)

         call rq_check( requests, 2, 'isend and irecv' )

         call msg_check( recv_buf, prev, tag, count, statuses(1,1),
     $        TEST_SIZE, 'isend and irecv' )

      else

         call MPI_Recv(recv_buf, TEST_SIZE, MPI_REAL,
     .                 MPI_ANY_SOURCE, MPI_ANY_TAG, MPI_COMM_WORLD,
     .                 status, ierr)

         call msg_check( recv_buf, prev, tag, count, status, TEST_SIZE,
     .                   'isend and irecv' )

         call MPI_Isend(recv_buf, count, MPI_REAL, next, tag,
     .                  MPI_COMM_WORLD, requests(1), ierr) 

         call MPI_Wait(requests(1), status, ierr)

         call rq_check( requests(1), 1, 'isend and irecv' )

         end if
 114  continue
c
c     Nonblocking ready sends
c
      if (rank .eq. 0) then
         print *, '    Irsend'
         end if

      tag = 2456
      count = TEST_SIZE / 3

      do 115 c = 1, TEST_COUNT+5
      call clear_test_data(recv_buf,TEST_SIZE)

      if (rank .eq. 0) then

         call MPI_Irecv(recv_buf, TEST_SIZE, MPI_REAL,
     .                  MPI_ANY_SOURCE, MPI_ANY_TAG, MPI_COMM_WORLD,
     .                  requests(1), ierr)

         call init_test_data(send_buf,TEST_SIZE)

         call MPI_Sendrecv( MPI_BOTTOM, 0, MPI_INTEGER, next, 0, 
     .                      MPI_BOTTOM, 0, MPI_INTEGER, next, 0, 
     .                      dupcom, status, ierr )

         call MPI_Irsend(send_buf, count, MPI_REAL, next, tag,
     .                   MPI_COMM_WORLD, requests(2), ierr) 

         index = -1
         do while (index .ne. 1)
            call MPI_Waitany(2, requests, index, statuses, ierr)
            end do

         call rq_check( requests(1), 1, 'irsend and irecv' )

         call msg_check( recv_buf, prev, tag, count, statuses,
     $           TEST_SIZE, 'irsend and irecv' )

C
C        In case the send didn't complete yet.
         call MPI_Waitall( 2, requests, statuses, ierr )

      else

         call MPI_Irecv(recv_buf, TEST_SIZE, MPI_REAL,
     .                  MPI_ANY_SOURCE, MPI_ANY_TAG, MPI_COMM_WORLD,
     .                  requests(1), ierr)

         call MPI_Sendrecv( MPI_BOTTOM, 0, MPI_INTEGER, next, 0, 
     .                      MPI_BOTTOM, 0, MPI_INTEGER, next, 0, 
     .                      dupcom, status, ierr )

         flag = .FALSE.
         do while (.not. flag)
            call MPI_Test(requests(1), flag, status, ierr)
            end do

         call rq_check( requests, 1, 'irsend and irecv (test)' )

         call msg_check( recv_buf, prev, tag, count, status, TEST_SIZE,
     .                   'irsend and irecv' )

         call MPI_Irsend(recv_buf, count, MPI_REAL, next, tag,
     .                   MPI_COMM_WORLD, requests(1), ierr) 

         call MPI_Waitall(1, requests, statuses, ierr)

         call rq_check( requests, 1, 'irsend and irecv' )

         end if
 115  continue
c
c     Nonblocking synchronous sends
c
      if (rank .eq. 0) then
         print *, '    Issend'
         end if

      tag = 2789
      count = TEST_SIZE / 3

      do 116 c = 1, TEST_COUNT+6
      call clear_test_data(recv_buf,TEST_SIZE)

      if (rank .eq. 0) then

         call MPI_Irecv(recv_buf, TEST_SIZE, MPI_REAL,
     .                  MPI_ANY_SOURCE, MPI_ANY_TAG, MPI_COMM_WORLD,
     .                  requests(1), ierr)

         call init_test_data(send_buf,TEST_SIZE)

         call MPI_Issend(send_buf, count, MPI_REAL, next, tag,
     .                   MPI_COMM_WORLD, requests(2), ierr) 

         flag = .FALSE.
         do while (.not. flag)
            call MPI_Testall(2, requests, flag, statuses, ierr)
C            print *, 'flag = ', flag
            end do

         call rq_check( requests, 2, 'issend and irecv (testall)' )

         call msg_check( recv_buf, prev, tag, count, statuses(1,1),
     $           TEST_SIZE, 'issend and recv (testall)' )

      else

         call MPI_Recv(recv_buf, TEST_SIZE, MPI_REAL,
     .                 MPI_ANY_SOURCE, MPI_ANY_TAG, MPI_COMM_WORLD,
     .                 status, ierr)

         call msg_check( recv_buf, prev, tag, count, status, TEST_SIZE,
     .                   'issend and recv' )

         call MPI_Issend(recv_buf, count, MPI_REAL, next, tag,
     .                   MPI_COMM_WORLD, requests(1), ierr) 

         flag = .FALSE.
         do while (.not. flag)
            call MPI_Testany(1, requests(1), index, flag,
     .                       statuses(1,1), ierr)
c            print *, 'flag = ', flag
            end do

         call rq_check( requests, 1, 'issend and recv (testany)' )

         end if
 116  continue
c
c     Persistent normal sends
c
      if (rank .eq. 0) then
         print *, '    Send_init' 
         end if

      tag = 3123
      count = TEST_SIZE / 5

      do 117 c = 1, TEST_COUNT+7
      call clear_test_data(recv_buf,TEST_SIZE)

      call MPI_Send_init(send_buf, count, MPI_REAL, next, tag,
     .                   MPI_COMM_WORLD, requests(1), ierr) 

      call MPI_Recv_init(recv_buf, TEST_SIZE, MPI_REAL,
     .                   MPI_ANY_SOURCE, MPI_ANY_TAG, MPI_COMM_WORLD,
     .                   requests(2), ierr)

      if (rank .eq. 0) then

         call init_test_data(send_buf,TEST_SIZE)

         call MPI_Startall(2, requests, ierr) 
         call MPI_Waitall(2, requests, statuses, ierr)

         call msg_check( recv_buf, prev, tag, count, statuses(1,2),
     $        TEST_SIZE, 'persistent send/recv' )

      else

         call MPI_Start(requests(2), ierr) 
         call MPI_Wait(requests(2), status, ierr)

         call msg_check( recv_buf, prev, tag, count, status, TEST_SIZE,
     *                   'persistent send/recv')

         do i = 1,count
            send_buf(i) = recv_buf(i)
            end do

         call MPI_Start(requests(1), ierr) 
         call MPI_Wait(requests(1), status, ierr)

         end if

      call MPI_Request_free(requests(1), ierr)
      call MPI_Request_free(requests(2), ierr)
 117  continue
c
c     Persistent ready sends
c
      if (rank .eq. 0) then
         print *, '    Rsend_init'
         end if

      tag = 3456
      count = TEST_SIZE / 3

      do 118 c = 1, TEST_COUNT+8
      call clear_test_data(recv_buf,TEST_SIZE)

      call MPI_Rsend_init(send_buf, count, MPI_REAL, next, tag,
     .                    MPI_COMM_WORLD, requests(1), ierr) 

      call MPI_Recv_init(recv_buf, TEST_SIZE, MPI_REAL,
     .                   MPI_ANY_SOURCE, MPI_ANY_TAG, MPI_COMM_WORLD,
     .                   requests(2), ierr)

      if (rank .eq. 0) then

         call init_test_data(send_buf,TEST_SIZE)

         call MPI_Startall(2, requests, ierr)

         index = -1
         do while (index .ne. 2)
            call MPI_Waitsome(2, requests, outcount,
     .                        indices, statuses, ierr)
            do i = 1,outcount
               if (indices(i) .eq. 2) then
                  call msg_check( recv_buf, prev, tag, count,
     $                 statuses(1,i), TEST_SIZE, 'waitsome' )
                  index = 2
                  end if
               end do
            end do

      else

         call MPI_Start(requests(2), ierr)

         flag = .FALSE.
         do while (.not. flag)
            call MPI_Test(requests(2), flag, status, ierr)
            end do

         call msg_check( recv_buf, prev, tag, count, status, TEST_SIZE,
     *                   'test' )

         do i = 1,count
            send_buf(i) = recv_buf(i)
            end do

         call MPI_Start(requests(1), ierr)
         call MPI_Wait(requests(1), status, ierr)

         end if

      call MPI_Request_free(requests(1), ierr)
      call MPI_Request_free(requests(2), ierr)
 118  continue
c
c     Persistent synchronous sends
c
      if (rank .eq. 0) then
         print *, '    Ssend_init'
         end if

      tag = 3789
      count = TEST_SIZE / 3

      do 119 c = 1, TEST_COUNT+9
      call clear_test_data(recv_buf,TEST_SIZE)
      
      call MPI_Ssend_init(send_buf, count, MPI_REAL, next, tag,
     .                    MPI_COMM_WORLD, requests(2), ierr) 

      call MPI_Recv_init(recv_buf, TEST_SIZE, MPI_REAL,
     .                   MPI_ANY_SOURCE, MPI_ANY_TAG, MPI_COMM_WORLD,
     .                   requests(1), ierr)

      if (rank .eq. 0) then

         call init_test_data(send_buf,TEST_SIZE)

         call MPI_Startall(2, requests, ierr)

         index = -1
         do while (index .ne. 1)
            call MPI_Testsome(2, requests, outcount,
     .                        indices, statuses, ierr)
            do i = 1,outcount
               if (indices(i) .eq. 1) then
                  call msg_check( recv_buf, prev, tag, count,
     $                 statuses(1,i), TEST_SIZE, 'testsome' )
                  index = 1
                  end if
               end do
            end do

      else

         call MPI_Start(requests(1), ierr)

         flag = .FALSE.
         do while (.not. flag)
            call MPI_Testany(1, requests(1), index, flag,
     .                       statuses(1,1), ierr)
            end do

         call msg_check( recv_buf, prev, tag, count, statuses(1,1),
     $           TEST_SIZE, 'testany' )

         do i = 1,count
            send_buf(i) = recv_buf(i)
            end do

         call MPI_Start(requests(2), ierr)
         call MPI_Wait(requests(2), status, ierr)

         end if

      call MPI_Request_free(requests(1), ierr)
      call MPI_Request_free(requests(2), ierr)
 119  continue
c
c     Send/receive.
c
      if (rank .eq. 0) then
         print *, '    Sendrecv'
         end if

      tag = 4123
      count = TEST_SIZE / 5

      do 120 c = 1, TEST_COUNT+10
      call clear_test_data(recv_buf,TEST_SIZE)

      if (rank .eq. 0) then

         call init_test_data(send_buf,TEST_SIZE)

         call MPI_Sendrecv(send_buf, count, MPI_REAL, next, tag,
     .                     recv_buf, count, MPI_REAL, prev, tag,
     .                     MPI_COMM_WORLD, status, ierr) 

         call msg_check( recv_buf, prev, tag, count, status, TEST_SIZE,
     .                   'sendrecv' )

      else

         call MPI_Recv(recv_buf, TEST_SIZE, MPI_REAL,
     .                 MPI_ANY_SOURCE, MPI_ANY_TAG, MPI_COMM_WORLD,
     .                 status, ierr)

         call msg_check( recv_buf, prev, tag, count, status, TEST_SIZE,
     .                   'recv/send' )

         call MPI_Send(recv_buf, count, MPI_REAL, next, tag,
     .                 MPI_COMM_WORLD, ierr) 
         end if
 120  continue
c
c     Send/receive replace.
c
      if (rank .eq. 0) then
         print *, '    Sendrecv_replace'
         end if

      tag = 4456
      count = TEST_SIZE / 3

      do 121 c = 1, TEST_COUNT+11
      if (rank .eq. 0) then

         call init_test_data(recv_buf, TEST_SIZE)

         do 11 i = count+1,TEST_SIZE
            recv_buf(i) = 0.0
 11      continue

         call MPI_Sendrecv_replace(recv_buf, count, MPI_REAL,
     .                             next, tag, prev, tag,
     .                             MPI_COMM_WORLD, status, ierr)  

         call msg_check( recv_buf, prev, tag, count, status, TEST_SIZE,
     .                   'sendrecvreplace' )

      else

         call clear_test_data(recv_buf,TEST_SIZE)

         call MPI_Recv(recv_buf, TEST_SIZE, MPI_REAL,
     .                 MPI_ANY_SOURCE, MPI_ANY_TAG, MPI_COMM_WORLD,
     .                 status, ierr)

         call msg_check( recv_buf, prev, tag, count, status, TEST_SIZE,
     .                   'recv/send for replace' )

         call MPI_Send(recv_buf, count, MPI_REAL, next, tag,
     .                 MPI_COMM_WORLD, ierr) 
         end if

 121  continue

      call MPI_Comm_free( dupcom, ierr )
      return
      
      end

c------------------------------------------------------------------------------
c
c  Check for correct source, tag, count, and data in test message.
c
c------------------------------------------------------------------------------
      subroutine msg_check( recv_buf, source, tag, count, status, n, 
     *                      name )
      include 'mpif.h'
      integer n
      real    recv_buf(n)
      integer source, tag, count, rank, status(MPI_STATUS_SIZE)
      character*(*) name

      integer ierr, recv_src, recv_tag, recv_count

      recv_src = status(MPI_SOURCE)
      recv_tag = status(MPI_TAG)
      call MPI_Comm_rank( MPI_COMM_WORLD, rank, ierr )
      call MPI_Get_count(status, MPI_REAL, recv_count, ierr)

C     Check for null status
      if (recv_src .eq. MPI_ANY_SOURCE .and. 
     *    recv_tag .eq. MPI_ANY_TAG .and. 
     *    status(MPI_ERROR) .eq. MPI_SUCCESS) then
         print *, '[', rank, '] Unexpected NULL status in ', name
         call MPI_Abort( MPI_COMM_WORLD, 104, ierr )
      end if
      if (recv_src .ne. source) then
         print *, '[', rank, '] Unexpected source:', recv_src, 
     *            ' in ', name
         call MPI_Abort(MPI_COMM_WORLD, 101, ierr)
         end if

      if (recv_tag .ne. tag) then
         print *, '[', rank, '] Unexpected tag:', recv_tag, ' in ', name
         call MPI_Abort(MPI_COMM_WORLD, 102, ierr)
         end if

      if (recv_count .ne. count) then
         print *, '[', rank, '] Unexpected count:', recv_count,
     *            ' in ', name
         call MPI_Abort(MPI_COMM_WORLD, 103, ierr)
         end if

      call verify_test_data(recv_buf, count, n, name )

      end
c------------------------------------------------------------------------------
c
c  Check that requests have been set to null
c
c------------------------------------------------------------------------------
      subroutine rq_check( requests, n, msg )
      include 'mpif.h'
      integer n, requests(n)
      character*(*) msg
      integer i
c
      do 10 i=1, n
         if (requests(i) .ne. MPI_REQUEST_NULL) then
            print *, 'Nonnull request in ', msg
         endif
 10   continue
c      
      end
c------------------------------------------------------------------------------
c
c  Initialize test data buffer with integral sequence.
c
c------------------------------------------------------------------------------
      subroutine init_test_data(buf,n)
      integer n
      real buf(n)
      integer i

      do 10 i = 1, n
         buf(i) = REAL(i)
 10    continue
      end

c------------------------------------------------------------------------------
c
c  Clear test data buffer
c
c------------------------------------------------------------------------------
      subroutine clear_test_data(buf, n)
      integer n
      real buf(n)
      integer i

      do 10 i = 1, n
         buf(i) = 0.
 10   continue

      end

c------------------------------------------------------------------------------
c
c  Verify test data buffer
c
c------------------------------------------------------------------------------
      subroutine verify_test_data(buf, count, n, name)
      include 'mpif.h'
      integer n
      real buf(n)
      character *(*) name

      integer count, ierr, i

      do 10 i = 1, count
         if (buf(i) .ne. REAL(i)) then
            print 100, buf(i), i, count, name
            call MPI_Abort(MPI_COMM_WORLD, 108, ierr)
            endif
 10       continue

      do 20 i = count + 1, n
         if (buf(i) .ne. 0.) then
            print 100, buf(i), i, n, name
            call MPI_Abort(MPI_COMM_WORLD, 109, ierr)
            endif
 20       continue

100   format('Invalid data', f6.1, ' at ', i4, ' of ', i4, ' in ', a)

      end
