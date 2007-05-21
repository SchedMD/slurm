      program systest

      include 'p4f.h'

      call p4init()

      call p4crpg()
      if (p4myid() .eq. 0) then
	  call fmaster()
      else
	  call fslave()
      endif
      call p4cleanup()
      print *,'mainline exiting normally'
      end


      subroutine fmaster()

      include 'p4f.h'

      integer*4 i,toid,slaves,type,from,retcde,recvlen,buflen
      character*40 buffer
      integer*4 TAGCNT, TAGDAT, TAGEND, ALOG_TRUNCATE, SENDING
      parameter (TAGCNT = 10)
      parameter (TAGDAT = 20)
      parameter (TAGEND = 30)
      parameter (ALOG_TRUNCATE = 0)
      parameter (SENDING = 99)

      print 11,'Entering fmaster'
11    format(a)

      call alogfenable()
      call alogfmaster(p4myid(),ALOG_TRUNCATE)
      call alogfdefine(SENDING,'Sending','')

      slaves = p4ntotids() - 1
      length = 0
      buflen = 40

      do 10 i = 1,slaves
	 call alogflog(p4myid(),SENDING,TAGCNT,'')
         call p4sendr(TAGCNT,i,buffer,length,retcde)
10    continue

20    print *,'Enter a string: '
      read (*,99,end=50) buffer
99    format(a40)

      do 30 length=40,1,-1
         if(buffer(length:length) .ne. ' ') goto 40
30    continue
      length = 0
40    continue

      call alogflog(p4myid(),SENDING,TAGDAT,'')
      toid = 1
      call p4send(TAGDAT,toid,buffer,length,retcde)
      buffer = ' '
      type = TAGDAT
      from = -1
      call p4recv(type,from,buffer,buflen,recvlen,retcde)

      print *,'MASTER receives from=',from,' buffer=',buffer
      length = 0
      goto 20
50    continue

      do 60 i = 1,slaves
	 call alogflog(p4myid(),SENDING,TAGEND,'')
         call p4sendr(TAGEND,i,buffer,buflen,retcde)
60    continue

      call alogfoutput()
      print *,'Master exiting normally'
      end


      subroutine fslave()

      include 'p4f.h'

      character*40 buffer
      integer*4 type, from, next, done, procid, length, buflen
      integer*4 numsl, retcde, recvlen
      integer*4 TAGCNT, TAGDAT, TAGEND, ALOG_TRUNCATE, SENDING
      parameter (TAGCNT = 10)
      parameter (TAGDAT = 20)
      parameter (TAGEND = 30)
      parameter (ALOG_TRUNCATE = 0)
      parameter (SENDING = 99)

      numsl = p4ntotids() - 1
      procid = p4myid()
      buflen = 40

C     print 200,'slave ',procid,' has started'
C200  format(a,i2,a)
C     call p4flush

      if (procid .eq. numsl) then
         next = 0
      else
         next = procid + 1
      endif

      call alogfsetup(p4myid(),ALOG_TRUNCATE)

C     print 201,'slave ',procid,' next = ',next
C201  format(a,i2,a,i2)
C     call p4flush

      length = 40
      from = -1
      type = TAGCNT
      call p4recv(type,from,buffer,length,recvlen,retcde)
      done = 0

50    if (done .ne. 0) goto 100

         buffer = ' '
         length = 40
         from = -1
         type = -1
         call p4recv(type,from,buffer,length,recvlen,retcde)
         if (type .eq. TAGEND) then
            done = 1
         else
	    call alogflog(p4myid(),SENDING,TAGDAT,'')
            call p4send(TAGDAT,next,buffer,recvlen,retcde)
         endif
         goto 50

100   continue

      call alogfoutput()

      end
