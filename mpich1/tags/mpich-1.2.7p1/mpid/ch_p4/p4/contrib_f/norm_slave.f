      subroutine fslave ()

      include 'p4f.h'

*-----
*  file:- norm_slave.f
*
*  Test program for new p4 fortran library.
*    Receive vector from master,
*    compute sum norm,
*    send result back to master.
*
*  features:-
*    max vector length <= 200   (controlled by `NMAX')
*    4 data byte per integer    (controlled by `LENINT')
*    4 data byte per item       (real; controlled by `LENREAL')
*
*  Debug code disabled by `c$$$'.
*  status:- plain vanilla, no error control
*  Volker Kurz, ANL & U Frankfurt, 03-Oct-91
*-----
*     .. p4 routines ..
      external   p4myid, p4recv, p4send
*
*     .. constants ..
      integer    NMAX,      LENINT,   LENREAL,   OFF,    MASTER
      parameter (NMAX=200 , LENINT=4, LENREAL=4, OFF=-1, MASTER=0)
      integer    TAGCNT,    TAGDAT,    TAGNEW,    TAGEND
      parameter (TAGCNT=10, TAGDAT=20, TAGNEW=30, TAGEND=40)
      real       ZERO
      parameter (ZERO=0.0)
*
*     .. variables and arrays ..
      integer    myid, n, i, itype, iretcd, ireclen, iproc, msglen
      real       v(NMAX), rnorm
*-----
      myid = p4myid()
c$$$      write (*,*) 'slave ', myid, ' ready.'
c$$$      call flush ()
*
*  Outer loop for different matrices
*  ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
 1    continue
*
*     .. receive vector length ..
      itype = OFF
      iproc = MASTER
      call p4recv (itype, iproc, n, LENINT, ireclen, iretcd)
c$$$      write (*,*) 'retcod from recv = ', iretcd, ' id = ', myid
c$$$      write (*,*) 'ireclen from recv = ', ireclen, ' id = ', myid
c$$$      call flush
      if (itype.eq.TAGEND) then
         write (*,*) 'slave', myid, ' ended normally'
         return
      elseif (itype.ne.TAGCNT) then
         write (*,*)
     $        'slave', myid, ' received unexpected data type:', itype
         return
      endif

*  Main loop for calculation
*  ~~~~~~~~~~~~~~~~~~~~~~~~~
*  Receive one vector at a time from 'MASTER'.  
*  Decide upon data type 'itype', whether norm shall be computed
*  or program terminated.
*  Watch out for unknown data type.
*
 2    continue
*        .. receive vector ..
         itype = OFF
         iproc = MASTER
         msglen = n*LENREAL
c$$$         write (*,*) 'recving vec  id = ', myid
c$$$         call flush
         call p4recv (itype, iproc, v, msglen, ireclen, iretcd)
c$$$         write (*,*) 'recvd vec  id = ', myid
c$$$         call flush
         if (itype.eq.TAGDAT) then
*           .. compute norm ..
            rnorm = ZERO
            do 22 i=1,n
               rnorm = rnorm + abs (v(i))
 22         continue 
*           .. send result to host ..
            call p4send (TAGDAT, MASTER, rnorm, LENREAL, iretcd)
         elseif (itype.eq.TAGNEW) then
*           .. end of outer loop for matrices ..
            goto 1
         elseif (itype.eq.TAGEND) then
            write (*,*) 'slave', myid, ' ended normally'
            return
         else
            write (*,*)
     $           'slave', myid, ' received unexpected data type:', itype
            return
         endif
      goto 2
*-----
      end
