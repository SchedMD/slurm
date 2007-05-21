      program master

      include 'p4f.h'

      integer*4 i,slaves,type,from,retcde,recvlen,buflen,loops,time
      character*500 buffer
      integer*4 TAGCNT, TAGDAT, TAGEND
      parameter (TAGCNT = 10)
      parameter (TAGDAT = 20)
      parameter (TAGEND = 30)

      call p4init()
      call p4crpg()

      slaves = p4ntotids() - 1
      length = 0
      buflen = 0

      do 10 i = 1,slaves
         call p4sendr(TAGCNT,i,buffer,length,retcde)
10    continue

20    print *,'How long is the message: '
      read (*,*,end=50) buflen
      buflen = MIN(500,buflen)

25    print *,'How many times around the loop? '
      read (*,*,end=50) loops

      length = buflen 
      time = p4clock()
      do 45 j=1,loops

C send in reverse order of the slaves
      call p4send(TAGDAT,slaves,buffer,length,retcde)
      buffer = ' ' 
      type = TAGDAT
      from = -1
      call p4recv(type,from,buffer,buflen,recvlen,type,retcde)

      print *,'Loop number = ',j
      print 101,'MASTER receives from = ',from,' length = ',recvlen
101   format(a,i2,a,i6,/)
      length = recvlen 
45    continue 
      oldtime = time
      time = p4clock()
      print *,'Time used: ',time-oldtime, ' milliseconds'
      length = 0
50    continue

      do 60 i = 1,slaves
         call p4sendr(TAGEND,i,buffer,buflen,retcde)
60    continue

      call p4cleanup()
      print *,'Master exiting normally'
      end
