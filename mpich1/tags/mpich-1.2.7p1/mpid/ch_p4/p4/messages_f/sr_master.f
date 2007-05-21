      program master

      include 'p4f.h'

      integer*4 i,slaves,type,from,retcde,recvlen,buflen
      character*40 buffer
      integer*4 TAGCNT, TAGDAT, TAGEND
      parameter (TAGCNT = 10)
      parameter (TAGDAT = 20)
      parameter (TAGEND = 30)

      call p4init()
      call p4crpg()

      slaves = p4ntotids() - 1
      length = 0
      buflen = 40

      do 10 i = 1,slaves
         call p4sendr(TAGCNT,i,buffer,length,retcde)
10    continue

20    print *,'Type a string: '
      read (*,99,end=50) buffer
99    format(a40)

      do 30 length=40,1,-1
         if(buffer(length:length) .ne. ' ') goto 40
30    continue
      length = 0
40    continue

      toid = 1
      call p4send(TAGDAT,toid,buffer,length,retcde)
      buffer = ' '
      type = TAGDAT
      from = -1
      call p4recv(type,from,buffer,buflen,recvlen,retcde)

      print *,'MASTER receives= ',buffer,' from ',from
      length = 0
      goto 20
50    continue

      do 60 i = 1,slaves
         call p4sendr(TAGEND,i,buffer,buflen,retcde)
60    continue

      call p4cleanup()
      print *,'Master exiting normally'
      end
