      integer*4 p4myid
      integer*4 p4ntotids
      integer*4 p4nslaves
      integer*4 p4myclid
      integer*4 p4nclids
      integer*4 p4clock
      integer*4 p4ustimer

      integer*4 P4NOX, P4INT, P4LNG, P4FLT, P4DBL
      parameter (P4NOX = 0, P4INT = 1, P4LNG = 2)
      parameter (P4FLT = 3, P4DBL = 4)

      external p4dblsumop
      external p4dblmultop
      external p4dblmaxop
      external p4dblminop
      external p4dblabsmaxop
      external p4dblabsminop

      external p4fltsumop
      external p4fltmultop
      external p4fltmaxop
      external p4fltminop
      external p4fltabsmaxop
      external p4fltabsminop

      external p4intsumop
      external p4intmultop
      external p4intmaxop
      external p4intminop
      external p4intabsmaxop
      external p4intabsminop
