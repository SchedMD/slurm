      program m1

      include 'p4f.h'

*-----
*  file:-  norm_master.f
*  related files:-  
*    norm_slave.f         (slave program)
*    norm_<hostname>.pg   (process group file)
*
*  Test program for new p4 library.
*    Generate matrix,
*    distribute columns to slaves,
*    get sum norm of columns, verify and display result.
*
*  features:-
*    max vector length <= `NMAX'.  If you increase this value,
*            be sure to increase the same value in slave program, too.
*    4 data byte per integer (controlled by `LENINT')
*    4 data byte per item    (real; controlled by `LENREAL')
*  
*  Debug code disabled by `c$$$'.
*  status:- plain vanilla, no error control
*  Volker Kurz, ANL & U Frankfurt, 04-Oct-91
*-----
*     .. p4 routines ..
      external   p4init, p4crpg, p4ntotids, p4send, p4recv, p4cleanup,
     $     p4clock
*
*     .. constants ..
      integer    NMAX,      LENINT,   LENREAL,   OFF
      parameter (NMAX=200 , LENINT=4, LENREAL=4, OFF=-1)
      integer    TAGCNT,    TAGDAT,    TAGNEW,    TAGEND
      parameter (TAGCNT=10, TAGDAT=20, TAGNEW=30, TAGEND=40)
*
*     .. variables and arrays ..
      integer    nslaves, i, k, mnsl, time1, time2,
     $     iretcd, ireclen, ip, msglen, itype
      real       a(NMAX, NMAX), r(NMAX), rnorm
      logical    errors
*-----
*
      write (*,*) 'setting up parallel environment...'
*
      call p4init ()
      call p4crpg ()
      nslaves = p4ntotids() - 1
*
      write (*,*) 'initializing matrix...'
*
      do 1 k=1,NMAX
         a(1,k) = float (k+1)
         do 11 i=2,NMAX
            a(i,k) = 1.0
 11      continue
 1    continue
*-----------------------------------------------------------------------
*  Beginning of main loop
*  ~~~~~~~~~~~~~~~~~~~~~~
 2    continue
*
*  Ask for number of rows and columns.
*  Provide a condition to end the program.
*  Watch out, if they want to fool us by entering too big numbers.
*  Calculate message length for slave data.
*  Broadcast vector length to slaves.
*
      write (*,9993) NMAX
      read (*,*) m
      if (m.lt.1) then
         goto 7
      elseif (m.gt.NMAX) then
         m = NMAX
         write (*,*) 'value truncated to m = ',m
      endif
      write (*,9995) NMAX
      read (*,*) n
      if (n.lt.1) then
         goto 7
      elseif (n.gt.NMAX) then
         n = NMAX
         write (*,*) 'value truncated to n = ',n
      endif
*
      msglen = m * LENREAL
      time1 = p4clock()
*
      do 3 i=1,nslaves
         call p4send (TAGCNT, i, m, LENINT, iretcd)
 3    continue
*
*  Compute norms of columns
*  ~~~~~~~~~~~~~~~~~~~~~~~~
*  Process columns by strips of length 'nslaves'.
*  'mnsl' ensures that only 'n' columns are processed.
*  1st sweep dials out columns,
*  2nd sweep collects results.
*  
      do 4 i=1,n,nslaves
*        .. distribute columns to slaves ..
         mnsl = min ( n-i+1, nslaves ) 
         do 41 k=1,mnsl
*           .. send column i+k-1 to slave k ..
            call p4send (TAGDAT, k, a(1,i+k-1), msglen, iretcd)
 41      continue
         do 42 k=1,mnsl
*           .. receive norm from slave ..
            itype = TAGDAT
            ip = OFF
            call p4recv (itype, ip, rnorm, LENREAL, ireclen, iretcd)
*           .. result is for column i+ip-1 ..
            r(i+ip-1) = rnorm
 42      continue
 4    continue 
      time2 = p4clock()
*
*  Check results and report any errors.  
*  Norm of column k is:  #rows + k
*  Note: There should be no rounding errors!
*
      errors = .FALSE.
      do 5 k=1,n
         if (r(k).ne.m+k) then
            write (*,9999) k, m+k, r(k)
            errors = .TRUE.
         endif
c$$$         write (*,*) 'norm of col', k,' is:', r(k)
 5    continue
      if (errors) then
         write (*,*) 'mission completed, error(s) detected.'
      else
         write (*,*) 'mission completed, no errors detected.'
      endif
      write (*,9997) float (time2-time1) / 1000.0
*
*  Tell slaves to get ready for a new session.
*  Data is ignored, when slave detects data type TAGNEW.
*
      do 6 i=1,nslaves
         call p4send (TAGNEW, i, m, LENINT, iretcd)
 6    continue
*
*  End of main loop
*  ~~~~~~~~~~~~~~~~
      goto 2
 7    continue
*------------------------------------------------------------------------------
*
*  Terminate parallel session
*  ~~~~~~~~~~~~~~~~~~~~~~~~~~
*  Broadcast TAGEND signal to all slaves.
*  A slave will terminate, when TAGEND is detected,
*  the contents of the message will be ignored.
*
      do 8 i=1,nslaves
         call p4send (TAGEND, i, n, LENINT, iretcd)
 8    continue
      call p4cleanup
*-----
      stop
 9993 format(1x,'-----'/
     $     ' input number of rows (1 <= m <=',i7,')'/
     $     ' (any value <= 0 terminates program)')
 9995 format(1x,
     $     ' input number of columns (1 <= n <=',i7,')'/
     $     ' (any value <= 0 terminates program)')
 9997 format(1x,'time :',f12.3,' s')
 9999 format(1x,'Error in column ',i7/
     $     '   norm should be       :',i10/
     $     '   but computed value is:',e14.4)
      end
