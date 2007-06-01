      program master

      include 'p4f.h'

      integer i,slaves,type,from,start,done,time,retcde,recvlen
	  real a(600000)
      integer tagcnt, tagdat, tagend
      parameter (tagcnt = 10)
      parameter (tagdat = 20)
      parameter (tagend = 30)

      call p4init()
      call p4crpg()
      call p4setavlbuf(1,100)
      print *,'Master has started'

      n = 50
	  print *,'Enter interation count:'
	  read(*,*) n
      slaves = p4ntotids() - 1
      length = 8

      do 10 i = 1,slaves
         print *,'MASTER sending control message to ',i
      call p4sendr(tagcnt,i,n,length,retcde)
10    continue

C Initialize socket connections between the slaves
      call p4sendr(tagcnt,1,a,length,retcde)
      type = tagcnt
      from = -1
      call p4recv(type,from,a,length,recvlen,retcde)

      print *,'Number of slaves = ',slaves
      print *,' '
	  print *,'SEND to one slave only'         
	  do 55 i = 6,19
         length = 2**i  
		 time = 0
	     do 51 k = 1,n
      start = p4clock()
      call p4sendr(i,1,a,length,retcde)
      done = p4clock()
            time = done - start + time
51       continue
         time = time / n
         print *,'Total time to send message = ',time,
     &    '  bytes = ',length
C        print *,'Buffer pools------------------'
C        call p4avlbufs()
55    continue

      print *,' '
	  print *,'SENDR to one slave only'
	  do 60 i = 6,19
         length = 2**i  
		 time = 0
	     do 61 k = 1,n
      start = p4clock()
      call p4sendr(i,1,a,length,retcde)
      done = p4clock()
            time = done - start + time
61       continue
	     time = time / n
         print *,'Total time to send and acknowledge message = ',time,
     &    '  bytes = ',length
60    continue

      print *,' '
	  print *,'SEND and RECEIVE sent message from one slave only'
	  do 65 i = 6,19
         length = 2**i  
         time = 0
	     do 66 k = 1,n
      start = p4clock()
      call p4send(i,1,a,length,retcde)
      type = i
      from = -1
      call p4recv(type,from,a,length,recvlen,retcde)
      done = p4clock()
            time = done - start + time
66       continue
         time = time / n
         print *,'Total time for send and receive = ',time,
     &	 '  bytes = ',length
65    continue
 
      print *,' '
	  print *,'SEND message and RECEIVE time from one slave'
	  do 67 i = 6,19
         length = 2**i  
		 time = 0
		 do 68 k = 1,n
      start = p4clock()
      call p4send(i,1,a,length,retcde)
      type = i
      from = -1
      call p4recv(type,from,done,4,recvlen,retcde)
            time = done - start + time
68       continue
		 time = time / n
         print *,'Total time for send and receive slave = ',time,
     &	 '  bytes = ',length
67    continue

      print *,' '
	  print *,'SEND message around a ring of slaves'
	  do 70 i = 6,19
         length = 2**i  
		 time = 0
		 do 72 k = 1,n
      start = p4clock()
      call p4send(i,1,a,length,retcde)
      type = i
      from = -1
      call p4recv(type,from,a,length,recvlen,retcde)
      done = p4clock()
            time = done - start + time
72       continue
         time = time / n
         print *,'Total time for broadcast = ',time,'  bytes = ',length
70    continue

      print *,' '
	  print *,'SENDR message to each slave sequentially'
	  do 80 i = 6,19
         length = 2**i  
		 time = 0
		 do 81 k = 1,n
      start = p4clock()
	        do 82 j = 1,slaves
      call p4sendr(i,j,a,length,retcde)
82          continue
      done = p4clock()
         time = done - start + time
81       continue
		 time = time / n
         print *,'Total time for broadcast = ',time,'  bytes = ',length
80    continue

      length = 0
      do 90 i = 1,slaves
	  print *,'Master sending end message to ',i
      call p4sendr(tagend,i,a,length,retcde)
90    continue

999     call p4cleanup()
      print *,'Master exiting and has cleaned up'
      end

