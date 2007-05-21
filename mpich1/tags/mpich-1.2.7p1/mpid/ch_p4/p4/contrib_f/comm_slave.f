      subroutine fslave()

      include 'p4f.h'

      real a(600000)
      integer type, from, next, procid, length, numsl, retcde, recvlen
      integer tagcnt, tagdat, tagend, done
      parameter (tagcnt = 10)
      parameter (tagdat = 20)
      parameter (tagend = 30)

      numsl = p4ntotids() - 1
      procid = p4myid()
      print *,'slave ',procid,' has started'
      call p4flush()
      if (procid .eq. numsl) then
	     next = 0
      else
	     next = procid + 1
      endif
      length = 8
      type = tagcnt
      from = -1
      call p4recv(type,from,n,length,recvlen,retcde)
      print *,'SLAVE ',procid,' received from = ',from,' type = ',type
      call p4flush()

c Initialize socket connections between the slaves 
      type = tagcnt
      from = -1
      call p4recv(type,from,a,length,recvlen,retcde)
      call p4sendr(tagcnt,next,a,recvlen,retcde)

      length = 600000
c
	  if (procid .eq. 1) then
	     do 10 i = 6,19
		    do 11 k = 1,n
      type = i
      from = -1
      call p4recv(type,from,a,length,recvlen,retcde)
11          continue
10       continue
	     do 22 i = 6,19
			do 21 k = 1,n
      type = i
      from = -1
      call p4recv(type,from,a,length,recvlen,retcde)
21          continue
22       continue
	     do 23 i = 6,19
			do 24 k = 1,n
      type = i
      from = -1
      call p4recv(type,from,a,length,recvlen,retcde)
      call p4send(i,0,a,recvlen,retcde)
24          continue
23       continue
	     do 27 i = 6,19
			do 28 k = 1,n
      type = i
      from = -1
      call p4recv(type,from,a,length,recvlen,retcde)
      done = p4clock()
      call p4send(i,0,done,4,retcde)
28          continue
27       continue
      endif
c
	  do 30 i = 6,19
		 do 31 k = 1,n
      type = i
      from = -1
      call p4recv(type,from,a,length,recvlen,retcde)
      call p4send(i,next,a,recvlen,retcde)
31       continue
30    continue
c
	  do 40 i = 6,19
		 do 41 k = 1,n
      type = i
      from = -1
      call p4recv(type,from,a,length,recvlen,retcde)
41       continue
40    continue
c
      length = 0
      type = tagend
      from = 0
      call p4recv(type,from,a,length,recvlen,retcde)
999   print *,'Slave ',procid,' has been cleaned up'
      call p4flush()
      end
