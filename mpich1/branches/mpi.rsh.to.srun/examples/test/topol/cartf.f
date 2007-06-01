c
c     From Craig Douglas, modified by Bill Gropp (based on code in Using
c     MPI).
c     This code tests some topology routines and sendrecv with some
c     MPI_PROC_NULL source/destinations.  It should be run with 4
c     processes 
c
        program main
        include 'mpif.h'
        integer maxn
        parameter (maxn = 35)
        double precision  a(maxn,maxn)
        integer nx, ny
        integer myid, newid, numprocs, comm2d, ierr, stride
        integer nbrleft, nbrright, nbrtop, nbrbottom
        integer sx, ex, sy, ey
        integer dims(2), coords(2)
        integer nerrs, toterrs
        logical periods(2)
        logical verbose
        data periods/2*.false./
        data verbose/.false./
c
        call MPI_INIT( ierr )
        call MPI_COMM_RANK( MPI_COMM_WORLD, myid, ierr )
        call MPI_COMM_SIZE( MPI_COMM_WORLD, numprocs, ierr )
c        print *, "Process ", myid, " of ", numprocs, " is alive"
        if (numprocs .ne. 4) then
           print *, "This test requires exactly four processes"
           call MPI_Abort( MPI_COMM_WORLD, 1, ierr )
        endif 
        nx = 8
        ny = 4
        dims(1) = 0
        dims(2) = 0
        call MPI_DIMS_CREATE( numprocs, 2, dims, ierr )
        call MPI_CART_CREATE( MPI_COMM_WORLD, 2, dims, periods, .true.,
     *                      comm2d, ierr )
        call MPI_COMM_RANK( comm2d, newid, ierr )
        if (verbose) then
           print *, "Process ", myid, " of ", numprocs, " is now ",
     $          newid
        endif
        myid = newid
        call MPI_Cart_shift( comm2d, 0,  1, nbrleft,   nbrright, ierr )
        call MPI_Cart_shift( comm2d, 1,  1, nbrbottom, nbrtop,   ierr )
        if (verbose) then
            print *, "Process ", myid, " has nbrs", nbrleft, nbrright,
     &            nbrtop, nbrbottom
        endif
        call MPI_Cart_get( comm2d, 2, dims, periods, coords, ierr )
        call MPE_DECOMP1D( nx, dims(1), coords(1), sx, ex )
        call MPE_DECOMP1D( ny, dims(2), coords(2), sy, ey )
c
c       Fortran allows print to include * and , in the output!
c       So, we use an explicit Format
        if ( myid .eq. 0 )
     &    print 10, dims(1), dims(2)
 10     format( " Dims: ", i4, i4 )
        if (verbose) then
           print *, "Process ", myid, " has coords of ", coords
           print *, "Process ", myid, " has sx,ex/sy,ey ", sx,
     $          ex, sy, ey
        endif
        call MPI_TYPE_VECTOR( ey-sy+3, 1, ex-sx+3,
     $                        MPI_DOUBLE_PRECISION, stride, ierr )
        call MPI_TYPE_COMMIT( stride, ierr )
        call setupv( myid, a, sx, ex, sy, ey )
        call MPI_BARRIER( MPI_COMM_WORLD, ierr )
c
        call exchng2( myid, a, sx, ex, sy, ey, comm2d, stride,
     $                nbrleft, nbrright, nbrtop, nbrbottom )
c
c     Check results
c
        call checkval( a, sx, ex, sy, ey, nx, ny, nerrs )
c
        call mpi_allreduce( nerrs, toterrs, 1, MPI_INTEGER, MPI_SUM,
     $       MPI_COMM_WORLD, ierr )
        if (myid .eq. 0) then
           print *, " Total errors = ", toterrs
        endif
        call MPI_TYPE_FREE( stride, ierr )
        call MPI_COMM_FREE( comm2d, ierr )
c        call prv( -1, -1, -1, a, sx, ex, sy, ey )
        call MPI_FINALIZE(ierr)
        end
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
        subroutine exchng2( myid, v, sx, ex, sy, ey,
     $                      comm2d, stride,
     $                      nbrleft, nbrright, nbrtop, nbrbottom  )
        include "mpif.h"
        integer myid, sx, ex, sy, ey, stride
        double precision v(sx-1:ex+1,sy-1:ey+1)
        integer nbrleft, nbrright, nbrtop, nbrbottom, comm2d
        integer status(MPI_STATUS_SIZE), ierr, nx
c
        nx = ex - sx + 1
c  These are just like the 1-d versions, except for less data
c        call prv( myid, -1, -1, v, sx, ex, sy, ey )
        call MPI_SENDRECV( v(sx,ey),  nx, MPI_DOUBLE_PRECISION,
     $                    nbrtop, 0,
     $                    v(sx,sy-1), nx, MPI_DOUBLE_PRECISION,
     $                    nbrbottom, 0, comm2d, status, ierr )
c        call prv( myid, nbrtop, nbrbottom, v, sx, ex, sy, ey )
        call MPI_SENDRECV( v(sx,sy),  nx, MPI_DOUBLE_PRECISION,
     $                    nbrbottom, 1,
     $                    v(sx,ey+1), nx, MPI_DOUBLE_PRECISION,
     $                    nbrtop, 1, comm2d, status, ierr )
c        call prv( myid, nbrbottom, nbrtop, v, sx, ex, sy, ey )
c This uses the "strided" datatype
c       v(ex,sy-1) = -100 - myid
        call MPI_SENDRECV( v(ex,sy-1),  1, stride, nbrright,  2,
     $                     v(sx-1,sy-1), 1, stride, nbrleft,  2,
     $                     comm2d, status, ierr )
c        call prv( myid, nbrright, nbrleft, v, sx, ex, sy, ey )
c       v(sx,sy-1) = -200 - myid
        call MPI_SENDRECV( v(sx,sy-1),  1, stride, nbrleft,   3,
     $                     v(ex+1,sy-1), 1, stride, nbrright, 3,
     $                     comm2d, status, ierr )
c        call prv( myid, nbrleft, nbrright, v, sx, ex, sy, ey )
        return
        end
        subroutine prv( myid, n1, n2, v, sx, ex, sy, ey )
c***********************************************************************
c
c       Print a matrix of numbers.
c
c***********************************************************************
        integer myid, n1, n2, sx, ex, sy, ey
        double precision  v(sx-1:ex+1,sy-1:ey+1)
        integer count, i, j
        save count
        character*5 fname
        data count  / 0 /
        if ( myid .lt. 0 ) then
            close( 11 )
            return
        endif
        write (fname,'(''foo.'',i1)') myid
        if ( count .eq. 0 )
     &      open( 11, file=fname, status='UNKNOWN' )
        write (11,*) '----------------------------------------'
        if ( count .eq. 0 ) then
            write (11,*) 'sx ', sx
            write (11,*) 'ex ', ex
            write (11,*) 'sy ', sy
            write (11,*) 'ey ', ey
            write (11,*) '----------------------------------------'
        endif
        count = count + 1
        write (11,*) 'count,n1,n2: ', count, n1, n2
        do j = ey+1,sy-1,-1
            write (11,1) j, (v(i,j), i = sx-1,ex+1)
        enddo
        return
 1      Format( i3, 20f7.0 )
c1      Format( i3, 1p, 20d10.1 )
        end
        subroutine setupv( myid, v, sx, ex, sy, ey )
        integer myid, sx, ex, sy, ey
        double precision  v(sx-1:ex+1,sy-1:ey+1)
        integer i, j, k
c        write (*,*) 'setupv: ', myid, sx, ex, sy, ey
        do j = sy,ey
            k = j * 1000.0
            do i = sx,ex
                v(i,j)    = i + k
                v(i,sy-1) = 0
                v(i,ey+1) = 0
            enddo
            v(sx-1,j) = 0
            v(ex+1,j) = 0
        enddo
        return
        end
c***********************************************************************
      subroutine checkval( a, sx, ex, sy, ey, nx, ny, errs )
      integer sx, ex, sy, ey, nx, ny
      double precision a(sx-1:ex+1,sy-1:ey+1)
      integer i, j, k
      integer errs
c
c     Check interior
c
      errs = 0
      do 10 j=sy,ey
         k = j * 1000
         do 10 i=sx,ex
            if (a(i,j) .ne. i + k ) then
               errs = errs + 1
               print *, "error at (", i, ",", j, ") = ", a(i,j)
            endif
 10   continue
c
c     Check the boundaries
c      
      i = sx - 1
      if (sx .eq. 1) then
         do 20 j=sy,ey
            if (a(i,j) .ne. 0.0) then
               errs = errs + 1
               print *, "error at (", i, ",", j, ") = ", a(i,j)
            endif
 20      continue
      else
         do 30 j=sy,ey
            if (a(i,j) .ne. i + j * 1000) then
               errs = errs + 1
               print *, "error at (", i, ",", j, ") = ", a(i,j)
            endif
 30      continue
      endif
      i = ex + 1
      if (ex .eq. nx) then
         do 40 j=sy,ey
            if (a(i,j) .ne. 0.0) then
               errs = errs + 1
               print *, "error at (", i, ",", j, ") = ", a(i,j)
            endif
 40      continue
      else
         do 50 j=sy,ey
            if (a(i,j) .ne. i + j * 1000) then
               errs = errs + 1
               print *, "error at (", i, ",", j, ") = ", a(i,j)
            endif
 50      continue
      endif
      j = sy - 1
      if (sy .eq. 1) then
         do 60 i=sx,ex
            if (a(i,j) .ne. 0.0) then
               errs = errs + 1
               print *, "error at (", i, ",", j, ") = ", a(i,j)
            endif
 60      continue
      else
         do 70 i=sx,ex
            if (a(i,j) .ne. i + j * 1000) then
               errs = errs + 1
               print *, "error at (", i, ",", j, ") = ", a(i,j)
            endif
 70      continue
      endif
      j = ey + 1
      if (ey .eq. ny) then
         do 80 i=sx,ex
            if (a(i,j) .ne. 0.0) then
               errs = errs + 1
               print *, "error at (", i, ",", j, ") = ", a(i,j)
            endif
 80      continue
      else
         do 90 i=sx,ex
            if (a(i,j) .ne. i + j * 1000) then
               errs = errs + 1
               print *, "error at (", i, ",", j, ") = ", a(i,j)
            endif
 90      continue
      endif
      return 
      end
