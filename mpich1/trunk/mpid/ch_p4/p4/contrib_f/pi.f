
c**********************************************************************
c  This program calculates the value of pi, using numerical integration
c  with parallel processing.  The user selects the number of points of
c  integration.  By selecting more points you get more accurate results
c  at the expense of additional computation
c
c  p4 calls have been used to promote portability of source code.
c
c   Each node: 
c    1) receives the number of rectangles used in the approximation.
c    2) calculates the areas of it's rectangles.
c    3) Synchronizes for a global summation.
c   Node 0 prints the result.
c
c  Constants:
c    
c    NODEPID     always zero for iPSC/860 nodes
c    SIZETYPE    initial message to the cube
c    ALLNODES    used to load all nodes in cube with a node process
c    INTSIZ      four bytes for an integer
c    DBLSIZ      eight bytes for double precision
c
c  Variables:
c
c    pi  the calculated result
c    n   number of points of integration.  
c    x           midpoint of each rectangle's interval
c    f           function to integrate
c    sum,pi      area of rectangles
c    tmp         temporary scratch space for global summation
c    i           do loop index
c****************************************************************************

      include 'p4f.h'

      double precision  pi, pi25dt, h, sum, x, f, a, temp
      integer allnodes,nodepid,sizetype,intsiz,dblsiz,anynode
      integer n, mynod, nodes, i, retcode, recvlen, masternode
      integer sumtype      
      parameter(PI25DT = 3.141592653589793238462643d0)
clw      parameter(INTSIZ=4,DBLSIZ=8,SIZETYPE=10,ALLNODES=-1,NODEPID=0)
clw      parameter(ANYNODE=-1,SUMTYPE=17,MASTERNODE=0)
      parameter(INTSIZ=4,DBLSIZ=8,ALLNODES=-1,NODEPID=0)
      parameter(ANYNODE=-1,SUMTYPE=17)

c --  function to intergrate

      f(a) = 4.d0 / (1.d0 + a*a)

c --  use assignments, not parameters, for variables used in p4recv

      SIZETYPE=10
      MASTERNODE=0
      
      call p4init()
      call p4crpg()

      mynod = p4myid()
      nodes = p4ntotids()

10    if ( mynod .eq. 0 ) then
         write(6,98)
98       format('Enter the number of intervals: (0 quits)')
         read(5,99)n
99       format(i10)

clw --   this way to send to each individual node to
clw --   distribute the work

clw         do i=1,nodes-1
clw            call p4send(SIZETYPE,i,n,INTSIZ,retcode)
clw         enddo

clw --   this way to let p4 send to all other nodes

         call p4brdcst(SIZETYPE,n,INTSIZ,retcode)
      else
   
c --  Get the number of points of integration

         call p4recv(SIZETYPE, MASTERNODE, n, INTSIZ, 
     >               recvlen, retcode)
      endif

c --  everyone check for quit signal

      if ( n .le. 0 ) goto 30

c --  calculate the interval size

      h = 1.0d0/n

      sum  = 0.0d0
      do 20 i = mynod+1, n, nodes
         x = h * (dble(i) - 0.5d0)
         sum = sum + f(x)
20    continue
      pi = h * sum

c --  collect all the partial sums

clw --   first is the long way, each term with each send

clw      if (mynod .ne. 0) then
clw         call p4sendr(SUMTYPE,MASTERNODE,pi,DBLSIZ,retcode)
clw      else
clw         do i=1,nodes-1
clw            call p4recv(SUMTYPE,i,temp,DBLSIZ,recvlen,retcode)
clw            pi = pi + temp
clw         enddo
clw      endif

clw --   this way uses a global sum operator to get pi

         call p4globop(SUMTYPE,pi,1,DBLSIZ,p4dblsumop,P4DBL,retcode)

c --  node 0 prints the answer.

      if (mynod .eq. 0) then
         write(6, 97) pi, abs(pi - PI25DT)
      endif
      goto 10

97    format('  pi is approximately: ', F18.16,'  Error is: ', F18.16)
30    call p4cleanup()
      end

