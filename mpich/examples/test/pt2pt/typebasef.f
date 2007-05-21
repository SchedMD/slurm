C
C Fortran program to test the basic Fortran types
C 
      subroutine SetupBasicTypes( basictypes, basicnames )
      include 'mpif.h'
      integer basictypes(*)
      character*40 basicnames(*)
C
      basictypes(1) = MPI_INTEGER
      basictypes(2) = MPI_REAL
      basictypes(3) = MPI_DOUBLE_PRECISION
      basictypes(4) = MPI_COMPLEX
      basictypes(5) = MPI_LOGICAL
      basictypes(6) = MPI_CHARACTER
      basictypes(7) = MPI_BYTE
      basictypes(8) = MPI_PACKED
C      
      basicnames(1) = 'INTEGER'
      basicnames(2) = 'REAL'
      basicnames(3) = 'DOUBLE PRECISION'
      basicnames(4) = 'COMPLEX'
      basicnames(5) = 'LOGICAL'
      basicnames(6) = 'CHARACTER'
      basicnames(7) = 'BYTE'
      basicnames(8) = 'PACKED'
C
      return
      end
C
      program main
      include 'mpif.h'
      integer basictypes(8)
      character*40 basicnames(8)
      integer i, errcnt, ierr
      integer size, extent, ub, lb
C
      call mpi_init(ierr)
C
      call SetupBasicTypes( basictypes, basicnames )
C
      errcnt = 0
      do 10 i=1,8 
         call MPI_Type_size( BasicTypes(i), size, ierr )
         call MPI_Type_extent( BasicTypes(i), extent, ierr )
         call MPI_Type_lb( BasicTypes(i), lb, ierr )
         call MPI_Type_ub( BasicTypes(i), ub, ierr )
         if (size .ne. extent) then
	    errcnt = errcnt + 1
            print *, "size (", size, ") != extent (", extent, 
     *         ") for basic type ", basicnames(i)
	 endif
         if (lb .ne. 0) then
            errcnt = errcnt + 1
            print *, "Lowerbound of ", basicnames(i), " was ", lb, 
     *         " instead of 0" 
         endif
         if (ub .ne. extent) then
            errcnt = errcnt + 1
            print *, "Upperbound of ", basicnames(i), " was ",
     *        ub, " instead of ", extent
         endif
 10   continue
C
      if (errcnt .gt. 0) then
         print *, "Found ", errcnt, " errors in testing Fortran types"
      else
         print *, " Found no errors in basic Fortran "
      endif
C
      call mpi_finalize(ierr)
      end
