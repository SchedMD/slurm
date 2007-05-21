      program simpglob

      include 'p4f.h'

      call p4init()
      call p4crpg()
      call fslave()
      call p4cleanup()
      print *,'exiting pgm'
      end


      subroutine fslave()

      include 'p4f.h'

      character*40 buffer
      integer*4 ASIZE
      parameter (ASIZE = 10)
      real*8  a(ASIZE)
      integer*4 i, procid, itype, iasize, idblsize, ip4dbl, rc

      procid = p4myid()

      print 200,'slave ',procid,' has started'
 200  format(a,i2,a)
      call p4flush

      do 10 i = 1,ASIZE
          a(i) = i
 10   continue

      itype = 44
      iasize = ASIZE
      idblsize = 8
      ip4dbl = P4DBL
      call p4globop(itype,a,iasize,idblsize,p4dblsumop,ip4dbl,rc)

      do 20 i = 1,ASIZE
          print 300,a(i)
          call p4flush
 20   continue
300   format(f8.0)

      print 500,'slave ',procid,' is exiting'
 500  format(a,i2,a)
      call p4flush

      end
