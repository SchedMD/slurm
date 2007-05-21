      PROGRAM MAIN
      INCLUDE 'mpif.h'
      
      INTEGER LDA
      PARAMETER (LDA=2)
      INTEGER myid,IERR,NPROCS, stat(MPI_STATUS_SIZE)
      COMPLEX A(2,2)

      CALL MPI_INIT(IERR)
      CALL MPI_COMM_RANK(MPI_COMM_WORLD,myid,IERR)
      CALL MPI_COMM_SIZE(MPI_COMM_WORLD,NPROCS,IERR)
      
      J0 = 1
      J1 = 2
      
      IF (myid .EQ. 0) THEN
         A(1,1) = CMPLX(1,1)
         A(2,1) = CMPLX(2,1)
         A(1,2) = CMPLX(1,2)
         A(2,2) = CMPLX(2,2)
         CALL MPI_SEND(A(1,1),LDA*(J1-J0+1),MPI_COMPLEX,1,
     +                 0,MPI_COMM_WORLD,IERR)
      ELSE      
         CALL MPI_RECV(A(1,1),LDA*(J1-J0+1),MPI_COMPLEX,
     +             0,MPI_ANY_TAG,MPI_COMM_WORLD,stat,IERR)
         PRINT *,'Received A'
         PRINT *,'A(1,1) = ',A(1,1),' A(1,2) = ',A(1,2)
         PRINT *,'A(2,1) = ',A(2,1),' A(2,2) = ',A(2,2)
      ENDIF

      CALL MPI_FINALIZE(IERR) 

      STOP
      END
