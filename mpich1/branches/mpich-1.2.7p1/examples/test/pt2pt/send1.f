C
C Test program from Kevin Maguire (K.Maguire@dl.ac.uk); hung earlier
C T3D verions.  Modified by WDG to be Fortran 77
C
      PROGRAM TEST
      IMPLICIT NONE
      
      INCLUDE 'mpif.h'
      
      INTEGER STRT,STOP,STEP
      PARAMETER ( STRT = 1 , STOP = 1000 , STEP = 10 )
      
      INTEGER MAX_MESS
      PARAMETER (MAX_MESS = STOP)
      
      INTEGER NUM_LOOPS
      PARAMETER (NUM_LOOPS = 5)

      LOGICAL VERBOSE
      PARAMETER (VERBOSE = .FALSE.)

      REAL MESSAGE1(MAX_MESS),MESSAGE2(MAX_MESS)

      INTEGER MES_SIZE,MES_NUM,ID,IERR
      INTEGER TO1,FROM1,MES_ID1
      INTEGER TO2,FROM2,MES_ID2
      INTEGER INODE,ITOTNODE
      INTEGER STATUS(MPI_STATUS_SIZE)
      
      INTEGER TAG_UP_BD
      LOGICAL FLAG
      
      CALL MPI_INIT(IERR)
      CALL MPI_COMM_RANK
     $     (MPI_COMM_WORLD,INODE,IERR)
      CALL MPI_COMM_SIZE
     $     (MPI_COMM_WORLD,ITOTNODE,IERR)
      CALL MPI_ATTR_GET
     $     (MPI_COMM_WORLD,MPI_TAG_UB,TAG_UP_BD,FLAG,IERR)
      
      IF (.NOT.FLAG) STOP
      
      CALL MPI_BARRIER(MPI_COMM_WORLD,IERR)
      
      ID = 0

      DO 10 MES_SIZE=STRT,STOP,STEP

         DO 20 MES_NUM=1,NUM_LOOPS

            MESSAGE1(1) =  1.
            MESSAGE2(1) =  2.

            MES_ID1 = ID
            ID = ID + 100
            IF (ID.GE.TAG_UP_BD) ID = 0
            FROM1   = 0
            TO1     = ITOTNODE-1
            
            MES_ID2 = ID
            ID = ID + 100
            IF (ID.GE.TAG_UP_BD) ID = 0
            FROM2   = ITOTNODE-1
            TO2     = 0

            IF (INODE.EQ.0) THEN

               CALL MPI_SEND(
     $              MESSAGE1,MES_SIZE,MPI_REAL,
     $              TO1,MES_ID1,MPI_COMM_WORLD,
     $              IERR)

               CALL MPI_RECV(
     $              MESSAGE2,MES_SIZE,MPI_REAL,
     $              FROM2,MES_ID2,MPI_COMM_WORLD,
     $              STATUS,IERR)

            ENDIF

            IF (INODE.EQ.(ITOTNODE-1)) THEN

               CALL MPI_RECV(
     $              MESSAGE1,MES_SIZE,MPI_REAL,
     $              FROM1,MES_ID1,MPI_COMM_WORLD,
     $              STATUS,IERR)
               
               CALL MPI_SEND(
     $              MESSAGE2,MES_SIZE,MPI_REAL,
     $              TO2,MES_ID2,MPI_COMM_WORLD,
     $              IERR)

            ENDIF

            CALL MPI_BARRIER(MPI_COMM_WORLD,IERR)

            IF (INODE.EQ.0 .AND. VERBOSE) THEN 
               WRITE (*,'(5I10)')
     $              MES_SIZE,MES_NUM,TO1,FROM1,MES_ID1
               WRITE (*,'(5I10)')
     $              MES_SIZE,MES_NUM,TO2,FROM2,MES_ID2
               WRITE (*,'(5I10)')
            ENDIF

 20      CONTINUE

 10   CONTINUE
      IF (INODE.EQ.0) THEN 
C        If we get here at all, we're ok
         PRINT *, ' No Errors'
      ENDIF
      CALL MPI_FINALIZE(IERR)

      END
