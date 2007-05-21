            program temp

      integer   i
      character*80 tempchar

      i = iargc()
      print *,'iargc=',i

      i = 0
      call getarg(i,tempchar)
      print *,'tempchar0=',tempchar

      i = 1
      call getarg(i,tempchar)
      print *,'tempchar1=',tempchar

      end
