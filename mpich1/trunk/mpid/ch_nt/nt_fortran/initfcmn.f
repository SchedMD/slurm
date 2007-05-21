      subroutine mpir_init_fcm()
C      include '..\..\..\include\mpif.h'
C     By eliminating the need for mpif.h, we make compilation simpler, 
C     plus we eliminate external references to routines such as 
C     mpi_dup_fn and mpi_null_delete_fn, which may otherwise generate
C     unresolved references
      INTEGER MPI_STATUS_SIZE
C     This is not 
      PARAMETER(MPI_STATUS_SIZE=4)
      INTEGER MPI_BOTTOM, MPI_STATUS_IGNORE(MPI_STATUS_SIZE)
      INTEGER MPI_STATUSES_IGNORE(MPI_STATUS_SIZE)
CDEC$ ATTRIBUTES DLLEXPORT :: /MPIPRIV/ 
      COMMON /MPIPRIV/ MPI_BOTTOM,MPI_STATUS_IGNORE,MPI_STATUSES_IGNORE      
      SAVE /MPIPRIV/
C
C     Tell C where MPI_BOTTOM is
      call mpir_init_bottom( MPI_BOTTOM )
      return
      end
C
      subroutine mpir_init_flog( itrue, ifalse )
      logical itrue, ifalse
C
      itrue  = .TRUE.
      ifalse = .FALSE.
      return
      end
