      PROGRAM MAIN

      include 'mpif.h'

C. Data layout
C. Number of tests
      integer PM_GLOBAL_ERROR, PM_NUM_NODES
      integer PM_MAX_TESTS
      parameter (PM_MAX_TESTS=3)
C. Test data
      integer PM_TEST_INTEGER, fuzzy, Error, FazAttr
      integer PM_RANK_SELF
      integer Faz_World, FazTag
      integer errs
      parameter (PM_TEST_INTEGER=12345)
      logical FazFlag
      external FazCreate, FazDelete
C
C. Initialize MPI
      errs = 0
      call MPI_INIT(PM_GLOBAL_ERROR)

      PM_GLOBAL_ERROR = MPI_SUCCESS
C. Find out the number of processes
      call MPI_COMM_SIZE (MPI_COMM_WORLD,PM_NUM_NODES,PM_GLOBAL_ERROR)
      call MPI_COMM_RANK (MPI_COMM_WORLD,PM_RANK_SELF,PM_GLOBAL_ERROR)

      
      call MPI_keyval_create ( FazCreate, FazDelete, FazTag,
     &                         fuzzy, Error )

C. Make sure that we can get an attribute that hasn't been set yet (flag
C. is false)
      call MPI_attr_get (MPI_COMM_WORLD, FazTag, FazAttr, 
     &                   FazFlag, Error)

      if (FazFlag) then
         errs = errs + 1
         print *, 'Did not get flag==false when attr_get of key that'
         print *, 'had not had a value set with attr_put'
      endif

      FazAttr = 120
      call MPI_attr_put (MPI_COMM_WORLD, FazTag, FazAttr, Error)

C. Check that the put worked
      call MPI_attr_get (MPI_COMM_WORLD, FazTag, FazAttr, 
     &                   FazFlag, Error)

      if (FazAttr .ne. 120) then
         errs = errs + 1
         print 1, ' Proc=',PM_Rank_self, ' ATTR=', FazAttr
      endif
C. Duplicate the Communicator and it's cached attributes

      call MPI_Comm_Dup (MPI_COMM_WORLD, Faz_WORLD, Error)


      call MPI_Attr_Get ( Faz_WORLD, FazTag, FazAttr, 
     &                    FazFlag, Error)

      if (FazFlag) then
        if (FazAttr .ne. 121) then 
           errs = errs + 1
           print 1, ' T-Flag, Proc=',PM_Rank_self,' ATTR=', FazAttr
        endif
      else
         errs = errs + 1
         print 1, ' F-Flag, Proc=',PM_Rank_self,' ATTR=',FazAttr
      end if
 1    format( a, i5, a, i5 )

C. Clean up MPI
      if (PM_Rank_self .eq. 0) then
         if (errs .eq. 0) then
            print *, ' No Errors'
         else
            print *, ' Found ', errs, ' errors'
         endif
      endif
      call MPI_Comm_free( Faz_WORLD, Error )
      call MPI_FINALIZE (PM_GLOBAL_ERROR)

      end
C
C MPI 1.1 changed these from functions to subroutines.
C
      SUBROUTINE FazCreate (comm, keyval, fuzzy, 
     &                    attr_in, attr_out, flag, ierr )
      INTEGER comm, keyval, fuzzy, attr_in, attr_out
      LOGICAL flag
      include 'mpif.h'
      attr_out = attr_in + 1
      flag = .true.
      ierr = MPI_SUCCESS
      END

      SUBROUTINE FazDelete (comm, keyval, attr, extra, ierr )
      INTEGER comm, keyval, attr, extra, ierr
      include 'mpif.h'
      ierr = MPI_SUCCESS
      if (keyval .ne. MPI_KEYVAL_INVALID)then
         attr = attr -  1
      end if 
      END
