c**********************************************************************
c   twod.f - a solution to the Poisson problem by using Jacobi 
c   interation on a 2-d decomposition
c     
c   .... the rest of this is from pi3.f to show the style ...
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
c
c     This code is included (without the prints) because one version of
c     MPICH SEGV'ed (probably because of errors in handling send/recv of
c     MPI_PROC_NULL source/destination).
c
c****************************************************************************
      program main
      include "mpif.h"
      integer maxn
      parameter (maxn = 128)
      double precision  a(maxn,maxn), b(maxn,maxn), f(maxn,maxn)
      integer nx, ny
      integer myid, numprocs, it, rc, comm2d, ierr, stride
      integer nbrleft, nbrright, nbrtop, nbrbottom
      integer sx, ex, sy, ey
      integer dims(2)
      logical periods(2)
      double precision diff2d, diffnorm, dwork
      double precision t1, t2
      external diff2d
      data periods/2*.false./

      call MPI_INIT( ierr )
      call MPI_COMM_RANK( MPI_COMM_WORLD, myid, ierr )
      call MPI_COMM_SIZE( MPI_COMM_WORLD, numprocs, ierr )
c      print *, "Process ", myid, " of ", numprocs, " is alive"
      if (myid .eq. 0) then
c
c         Get the size of the problem
c
c          print *, 'Enter nx'
c          read *, nx
           nx = 10
      endif
c      print *, 'About to do bcast on ', myid
      call MPI_BCAST(nx,1,MPI_INTEGER,0,MPI_COMM_WORLD,ierr)
      ny   = nx
c
c Get a new communicator for a decomposition of the domain.  Let MPI
c find a "good" decomposition
c
      dims(1) = 0
      dims(2) = 0
      call MPI_DIMS_CREATE( numprocs, 2, dims, ierr )
      call MPI_CART_CREATE( MPI_COMM_WORLD, 2, dims, periods, .true.,
     *                    comm2d, ierr )
c
c Get my position in this communicator
c 
      call MPI_COMM_RANK( comm2d, myid, ierr )
c      print *, "Process ", myid, " of ", numprocs, " is alive"
c
c My neighbors are now +/- 1 with my rank.  Handle the case of the 
c boundaries by using MPI_PROCNULL.
      call fnd2dnbrs( comm2d, nbrleft, nbrright, nbrtop, nbrbottom )
c      print *, "Process ", myid, ":", 
c     *     nbrleft, nbrright, nbrtop, nbrbottom
c
c Compute the decomposition
c     
      call fnd2ddecomp( comm2d, nx, sx, ex, sy, ey )
c      print *, "Process ", myid, ":", sx, ex, sy, ey
c
c Create a new, "strided" datatype for the exchange in the "non-contiguous"
c direction
c
      call mpi_Type_vector( ey-sy+1, 1, ex-sx+3, 
     $                      MPI_DOUBLE_PRECISION, stride, ierr )
      call mpi_Type_commit( stride, ierr )
c
c
c Initialize the right-hand-side (f) and the initial solution guess (a)
c
      call twodinit( a, b, f, nx, sx, ex, sy, ey )
c
c Actually do the computation.  Note the use of a collective operation to
c check for convergence, and a do-loop to bound the number of iterations.
c
      call MPI_BARRIER( MPI_COMM_WORLD, ierr )
      t1 = MPI_WTIME()
      do 10 it=1, 100
        call exchng2( a, b, sx, ex, sy, ey, comm2d, stride, 
     $                nbrleft, nbrright, nbrtop, nbrbottom )
        call sweep2d( b, f, nx, sx, ex, sy, ey, a )
        call exchng2( b, a, sx, ex, sy, ey, comm2d, stride, 
     $                nbrleft, nbrright, nbrtop, nbrbottom )
        call sweep2d( a, f, nx, sx, ex, sy, ey, b )
        dwork = diff2d( a, b, nx, sx, ex, sy, ey )
        call MPI_Allreduce( dwork, diffnorm, 1, MPI_DOUBLE_PRECISION, 
     $                     MPI_SUM, comm2d, ierr )
        if (diffnorm .lt. 1.0e-5) goto 20
c        if (myid .eq. 0) print *, 2*it, ' Difference is ', diffnorm
10     continue
      if (myid .eq. 0) print *, 'Failed to converge'
20    continue
      t2 = MPI_WTIME()
c      if (myid .eq. 0) then
c         print *, 'Converged after ', 2*it, ' Iterations in ', t2 - t1,
c     $        ' secs '
c      endif
c
c
      call MPI_Type_free( stride, ierr )
      call MPI_Comm_free( comm2d, ierr )
      if (myid .eq. 0) then
         print *, ' No Errors'
      endif
      call MPI_FINALIZE(rc)
      end
c
c Perform a Jacobi sweep for a 2-d decomposition
c
      subroutine sweep2d( a, f, n, sx, ex, sy, ey, b )
      integer n, sx, ex, sy, ey
      double precision a(sx-1:ex+1, sy-1:ey+1), f(sx-1:ex+1, sy-1:ey+1),
     +                 b(sx-1:ex+1, sy-1:ey+1)
c
      integer i, j
      double precision h
c
      h = 1.0d0 / dble(n+1)
      do 10 i=sx, ex
         do 10 j=sy, ey
            b(i,j) = 0.25 * (a(i-1,j)+a(i,j+1)+a(i,j-1)+a(i+1,j)) - 
     +               h * h * f(i,j)
 10   continue
      return
      end

       subroutine exchng2( a, b, sx, ex, sy, ey, 
     $                     comm2d, stride, 
     $                     nbrleft, nbrright, nbrtop, nbrbottom  )
       include "mpif.h"
       integer sx, ex, sy, ey, stride
       double precision a(sx-1:ex+1, sy-1:ey+1), 
     $                  b(sx-1:ex+1, sy-1:ey+1)
       integer nbrleft, nbrright, nbrtop, nbrbottom, comm2d
       integer status(MPI_STATUS_SIZE), ierr, nx
c
       nx = ex - sx + 1
c  These are just like the 1-d versions, except for less data
        call MPI_SENDRECV( b(ex,sy),  nx, MPI_DOUBLE_PRECISION, 
     $                    nbrtop, 0,
     $                    a(sx-1,sy), nx, MPI_DOUBLE_PRECISION, 
     $                    nbrbottom, 0, comm2d, status, ierr )
        call MPI_SENDRECV( b(sx,sy),  nx, MPI_DOUBLE_PRECISION,
     $                    nbrbottom, 1,
     $                    a(ex+1,sy), nx, MPI_DOUBLE_PRECISION, 
     $                    nbrtop, 1, comm2d, status, ierr )
c
c This uses the "strided" datatype
        call MPI_SENDRECV( b(sx,ey),  1, stride, nbrright,  0,
     $                     a(sx,sy-1), 1, stride, nbrleft,  0, 
     $                     comm2d, status, ierr )
        call MPI_SENDRECV( b(sx,sy),  1, stride, nbrleft,   1,
     $                     a(sx,ey+1), 1, stride, nbrright, 1, 
     $                     comm2d, status, ierr )
        return
        end

c
c  The rest of the 2-d program
c
      double precision function diff2d( a, b, nx, sx, ex, sy, ey )
      integer nx, sx, ex, sy, ey
      double precision a(sx-1:ex+1, sy-1:ey+1), b(sx-1:ex+1, sy-1:ey+1)
c
      double precision sum
      integer i, j
c
      sum = 0.0d0
      do 10 j=sy,ey
         do 10 i=sx,ex
            sum = sum + (a(i,j) - b(i,j)) ** 2
 10      continue
c      
      diff2d = sum
      return
      end
      subroutine twodinit( a, b, f, nx, sx, ex, sy, ey )
      integer nx, sx, ex, sy, ey
      double precision a(sx-1:ex+1, sy-1:ey+1), b(sx-1:ex+1, sy-1:ey+1),
     &                 f(sx-1:ex+1, sy-1:ey+1)
c
      integer i, j
c
      do 10 j=sy-1,ey+1
         do 10 i=sx-1,ex+1
            a(i,j) = 0.0d0
            b(i,j) = 0.0d0
            f(i,j) = 0.0d0
 10      continue
c      
c    Handle boundary conditions
c
      if (sx .eq. 1) then 
         do 20 j=sy,ey
            a(0,j) = 1.0d0
            b(0,j) = 1.0d0
 20      continue
      endif
      if (ex .eq. nx) then
         do 21 j=sy,ey
            a(nx+1,j) = 0.0d0
            b(nx+1,j) = 0.0d0
 21      continue
      endif 
      if (sy .eq. 1) then
         do 30 i=sx,ex
            a(i,0) = 1.0d0
            b(i,0) = 1.0d0
 30      continue 
      endif
c
      return
      end

c
c  This file contains a routine for producing a decomposition of a 1-d array
c  when given a number of processors.  It may be used in "direct" product
c  decomposition.  The values returned assume a "global" domain in [1:n]
c
      subroutine MPE_DECOMP1D( n, numprocs, myid, s, e )
      integer n, numprocs, myid, s, e
      integer nlocal
      integer deficit
c
      nlocal  = n / numprocs
      s       = myid * nlocal + 1
      deficit = mod(n,numprocs)
      s       = s + min(myid,deficit)
      if (myid .lt. deficit) then
          nlocal = nlocal + 1
      endif
      e = s + nlocal - 1
      if (e .gt. n .or. myid .eq. numprocs-1) e = n
      return
      end
c
c This routine show how to determine the neighbors in a 2-d decomposition of
c the domain. This assumes that MPI_Cart_create has already been called 
c
      subroutine fnd2dnbrs( comm2d, 
     $                      nbrleft, nbrright, nbrtop, nbrbottom )
      integer comm2d, nbrleft, nbrright, nbrtop, nbrbottom
c
      integer ierr
c
      call MPI_Cart_shift( comm2d, 0,  1, nbrleft,   nbrright, ierr )
      call MPI_Cart_shift( comm2d, 1,  1, nbrbottom, nbrtop,   ierr )
c
      return
      end
c
c Note: THIS IS A TEST PROGRAM.  THE ACTUAL VALUES MOVED ARE NOT
c CORRECT FOR A POISSON SOLVER.
c
      subroutine fnd2ddecomp( comm2d, n, sx, ex, sy, ey )
      integer comm2d
      integer n, sx, ex, sy, ey
      integer dims(2), coords(2), ierr
      logical periods(2)
c
      call MPI_Cart_get( comm2d, 2, dims, periods, coords, ierr )

      call MPE_DECOMP1D( n, dims(1), coords(1), sx, ex )
      call MPE_DECOMP1D( n, dims(2), coords(2), sy, ey )
c
      return
      end
