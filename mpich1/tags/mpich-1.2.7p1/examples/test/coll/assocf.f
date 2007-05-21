C
C Thanks to zollweg@tc.cornell.edu (John A. Zollweg) for this test 
C which detected a problem in one version of the IBM product 
C implementation of MPI.  The source of the problem in that implementation
C was assuming that floating point arithmetic was associative (it isn't
C even commutative on IBM hardware).
C
C This program was designed for IEEE and may be uninteresting on other
C systems.  Note that since it is testing that the same VALUE is
C delivered at each system, it will run correctly on all systems.
C
      PROGRAM ALLREDUCE
      include 'mpif.h'
      real*8 myval(4), sum, recvbuf(4)
      integer ier, me, size, tsize, dtype, i, errors, toterr
      data myval /-12830196119319614d0,9154042893114674d0,
     &2371516219785616d0,1304637006419324.8d0/
      call MPI_INIT(ier)
      call MPI_COMM_SIZE(MPI_COMM_WORLD,size,ier)
      if (size.ne.4) then
         print *,"This test case must be run as a four-way job"
         call MPI_FINALIZE(ier)
         stop
      end if   
      call MPI_TYPE_SIZE( MPI_REAL, tsize, ier )
      if (tsize .eq. 8) then
         dtype = MPI_REAL
      else 
         call MPI_TYPE_SIZE( MPI_DOUBLE_PRECISION, tsize, ier )
         if (tsize .ne. 8) then
            print *, " Can not test allreduce without an 8 byte"
            print *, " floating double type."
            call MPI_FINALIZE(ier)
            stop
         endif
         dtype = MPI_DOUBLE_PRECISION
      endif
      call MPI_COMM_RANK(MPI_COMM_WORLD,me,ier)
      call MPI_ALLREDUCE(myval(me+1),sum,1,dtype,MPI_SUM,
     &MPI_COMM_WORLD,ier)
C
C     collect the values and make sure that they are all the same BITWISE
C     We could use Gather, but this gives us an added test.
C
      do 5 i=1,4
         recvbuf(i) = i
 5    continue
      call MPI_ALLGATHER( sum, 1, dtype, recvbuf, 1, dtype,
     &                    MPI_COMM_WORLD, ier )
      errors = 0
      do 10 i=2,4
C         print *, "recvbuf(",i,") = ", recvbuf(i), " on ", me
         if (recvbuf(1) .ne. recvbuf(i)) then
               errors = errors + 1
               print *, "Inconsistent values for ", i, "th entry on ",
     &                  me
               print *, recvbuf(1), " not equal to ", recvbuf(i)
          endif
 10   continue
      call MPI_ALLREDUCE( errors, toterr, 1, MPI_INTEGER, MPI_SUM,
     &                    MPI_COMM_WORLD, ier )
      if (me .eq. 0) then
         if (toterr .gt. 0) then
            print *, " FAILED with ", toterr, " errors."
         else
            print *, " No Errors"
         endif
      endif
C      print *," The value of the sum on node ",me,"is",sum
      call MPI_FINALIZE(ier)
C     Calling stop can generate unwanted noise on some systems, and is not
C     required.
      end
