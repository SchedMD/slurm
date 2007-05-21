c
c lnblnk = last non-blank of the input string str
c This should supercede the one in the C library
c
      integer function lnblnk( str )

      implicit none

c Input/Output : modified argument
      character*(*) str

      integer iptr

      if ( len( str ) .gt. 0 ) then
         iptr = len( str )
         do while ( iptr .gt. 0 .and. str( iptr:iptr ) .eq. ' ' )  
            iptr = iptr - 1
         enddo
         lnblnk = iptr
      else
         lnblnk = 0
      endif

      return
      end


      Program TestGraphics

      implicit none

c ** include files
      include 'mpef.h'
      include 'mpif.h'

c ** function name declaration
C     integer lnblnk

c ** local variables
C     integer ii, str_len
C     character*(80) displayname
      integer ierr, mp_size, my_rank, my_color
      integer graph
      character chr

c ** program body

      call MPI_Init( ierr )
      call MPI_Comm_size( MPI_COMM_WORLD, mp_size, ierr )
      call MPI_Comm_rank( MPI_COMM_WORLD, my_rank, ierr )

C     do ii = 1, 80
C         displayname( ii:ii ) = ' '
C     enddo
C     if ( my_rank .eq. 0 ) then
C         call getenv( 'DISPLAY', displayname )
C         str_len = lnblnk( displayname ) + 1
C         displayname( str_len:str_len ) = char( 0 )
C     endif
C     call MPI_Bcast( str_len, 1, MPI_INTEGER,
C    &                0, MPI_COMM_WORLD, ierr )
C     call MPI_Bcast( displayname, str_len, MPI_CHARACTER,
C    &                0, MPI_COMM_WORLD, ierr )
C     write(6,'(i3," : DISPLAY at process 0 = ",A,i3)') my_rank,
C    &                                   displayname(1:str_len),
C    &                                   str_len
C     call MPI_Barrier( MPI_COMM_WORLD, ierr )
C     call MPE_Open_graphics( graph, MPI_COMM_WORLD, displayname,
C    &                        -1, -1, 400, 400, 0, ierr )

      call MPE_Open_graphics( graph, MPI_COMM_WORLD, " ",
     &                        -1, -1, 400, 400, 0, ierr )
      write(6,'(i3," : MPE_Open_graphics = ",i3)') my_rank, ierr

      my_color = my_rank + 1
      if ( my_rank .eq. 0 ) then
          call MPE_Draw_string( graph, 187, 205, MPE_BLUE, "Hello",
     &                          ierr )
      endif
      call MPE_Draw_circle( graph, 200, 200, 20+my_rank*5, my_color,
     &                      ierr )
      call MPE_Update( graph, ierr )
C     call MPI_Barrier( MPI_COMM_WORLD, ierr )
C     write(6,'(i3," : MPE_Update = ",i3)') my_rank, ierr

C     call sleep( 15 )
      if ( my_rank .eq. 0 ) then
C         The following is non-portable; the $ asks the Fortran runtime
C         not to generate a newline at the end of the line.  If this
C         causes problems, replace this line with a simple print statement
          write(6,'(A,$)') 'Hit any key then return to terminate  '
          read(5,'(A)') chr
      endif
      call MPI_Barrier( MPI_COMM_WORLD, ierr )

      call MPE_Close_graphics( graph, ierr )

      call MPI_Finalize( ierr )

      end
