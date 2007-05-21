
      subroutine args(i,argv)
C reads the options from the command line
      integer*4 i
      integer i1
C DO NOT declare argv any bigger than 80
      character*80 argv

      i1 = i

C The following needs to be uncommented on the HP
C$HP9000_800 INTRINSICS ON
      call getarg(i1,argv)
C$HP9000_800 INTRINSICS OFF

      end


      subroutine numargc(icnt)
C retrieves the number of command line arguments
      integer*4 icnt
      icnt = iargc() + 1
      end

