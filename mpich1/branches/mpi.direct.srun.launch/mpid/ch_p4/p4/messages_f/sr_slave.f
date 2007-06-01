      subroutine fslave()

      include 'p4f.h'

      character*40 buffer
      integer*4 type, from, next, done, procid, length, buflen
      integer*4 numsl, retcde, recvlen
      integer*4 TAGCNT, TAGDAT, TAGEND
      parameter (TAGCNT = 10)
      parameter (TAGDAT = 20)
      parameter (TAGEND = 30)

      numsl = p4ntotids() - 1
      procid = p4myid()
      buflen = 40

      print 200,'slave ',procid,' has started'
 200  format(a,i2,a)
      call p4flush

      if (procid .eq. numsl) then
         next = 0
      else
         next = procid + 1
      endif

      print 201,'slave ',procid,' next = ',next
 201  format(a,i2,a,i2)
      call p4flush

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
                print *,'SLAVE ',procid,' sending msg to ',next
                call p4flush
            call p4send(TAGDAT,next,buffer,recvlen,retcde)
         endif
         goto 50

100   continue

      end
