      program main
C
C Test get processor name
C
      include 'mpif.h'
      character*(MPI_MAX_PROCESSOR_NAME) name
      integer  resultlen, ierr

      call MPI_Init( ierr )
      name = " "
      call MPI_Get_processor_name( name, resultlen, ierr )
C     Check that name contains only printing characters */
C      do i=1, resultlen
C      enddo
      errs = 0
      do i=resultlen+1, MPI_MAX_PROCESSOR_NAME
         if (name(i:i) .ne. " ") then
            errs = errs + 1
         endif
      enddo
      if (errs .gt. 0) then
         print *, 'Non-blanks after name'
      else
         print *, ' No Errors'
      endif
      call MPI_Finalize( ierr )
      end
