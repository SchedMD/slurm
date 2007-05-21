      program temp

      include 'p4f.h'

      call p4init()
      call p4crpg()
      if (p4myid() .eq. 0) then
	  call fmaster()
      else
	  call fworker()
      endif
      call p4cleanup()
      print *,'mainline exiting normally'
      end


      subroutine fmaster()

      include 'p4f.h'

      integer*4 i,slaves,toid,type,dtype,from,retcde,recvlen,buflen,length
      double precision buffer(12)
      integer*4 TAGCNT, TAGDAT, TAGEND
      parameter (TAGCNT = 10)
      parameter (TAGDAT = 20)
      parameter (TAGEND = 30)

      print 11,'Entering fmaster'
11    format(a)
      slaves = p4ntotids() - 1
      length = 96

      i = 1
      call p4brdcstx(TAGCNT,buffer,length,P4DBL,retcde)

      print *,'Master exiting normally'
      end


      subroutine fworker()

      include 'p4f.h'

      double precision buffer(12)
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

      print 201,'slave ',procid,' next = ',next
 201  format(a,i2,a,i2)
      call p4flush

      length = 96
      from = -1
      type = TAGCNT
      call p4recv(type,from,buffer,length,recvlen,retcde)
      call p4flush
      done = 0

      end
