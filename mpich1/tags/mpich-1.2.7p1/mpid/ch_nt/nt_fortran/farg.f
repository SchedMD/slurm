      integer function mpir_iargc()
	USE DFLIB
      mpir_iargc = nargs()
      return
      end
c     
      subroutine mpir_getarg( i, s )
      USE DFLIB
      integer       i
      character*(*) s
      call getarg(i,s)
      return
      end
