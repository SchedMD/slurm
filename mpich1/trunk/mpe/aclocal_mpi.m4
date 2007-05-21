dnl
dnl PAC_MPI_LINK_CC_FUNC( MPI_CC, MPI_CFLAGS, MPI_LIBS,
dnl                       MPI_VARS, MPI_FUNC,
dnl                       [action if working], [action if not working] )
dnl - MPI_CC     is the MPI parallel compiler, like mpich/mpicc, AIX/mpcc
dnl - MPI_CFLAGS is the CFLAGS to MPI_CC, like "-I/usr/include" for mpi.h
dnl - MPI_CLIBS  is the LIBS to MPI_CC, like "-L/usr/lib -lmpi" for libmpi.a
dnl - MPI_VARS   is the the declaration of variables needed to call MPI_FUNC
dnl - MPI_FUNC   is the body of MPI function call to be checked for existence
dnl              e.g.  MPI_VARS="MPI_Request request; MPI_Fint a;"
dnl                    MPI_FUNC="a = MPI_Request_c2f( request );"
dnl              if MPI_FUNC is empty, assume linking with basic MPI program.
dnl              i.e. check if MPI definitions are valid
dnl
AC_DEFUN(PAC_MPI_LINK_CC_FUNC,[
dnl - set local parallel compiler environments
dnl   so input variables can be CC, CFLAGS or LIBS
    pac_MPI_CC="$1"
    pac_MPI_CFLAGS="$2"
    pac_MPI_LIBS="$3"
    AC_LANG_SAVE
    AC_LANG_C
dnl - save the original environment
    pac_saved_CC="$CC"
    pac_saved_CFLAGS="$CFLAGS"
    pac_saved_LIBS="$LIBS"
dnl - set the parallel compiler environment
    CC="$pac_MPI_CC"
    CFLAGS="$pac_MPI_CFLAGS"
    LIBS="$pac_MPI_LIBS"
    AC_TRY_LINK( [#include "mpi.h"], [
    int argc; char **argv;
    $4 ; 
    MPI_Init(&argc, &argv);
    $5 ;
    MPI_Finalize();
                 ], pac_mpi_working=yes, pac_mpi_working=no )
    CC="$pac_saved_CC"
    CFLAGS="$pac_saved_CFLAGS"
    LIBS="$pac_saved_LIBS"
    AC_LANG_RESTORE
    if test "$pac_mpi_working" = "yes" ; then
       ifelse([$6],,:,[$6])
    else
       ifelse([$7],,:,[$7])
    fi
])dnl
dnl
dnl PAC_MPI_LINK_F77_FUNC( MPI_F77, MPI_FFLAGS, MPI_LIBS,
dnl                        MPI_VARS, MPI_FUNC,
dnl                        [action if working], [action if not working] )
dnl - MPI_F77    is the MPI parallel compiler, like mpich/mpif77, AIX/mpxlf
dnl - MPI_FFLAGS is the FFLAGS to MPI_F77, like "-I/usr/include" for mpif.h
dnl - MPI_LIBS   is the LIBS to MPI_F77, like "-L/usr/lib -lmpi" for libmpi.a
dnl - MPI_VARS   is the the declaration of variables needed to call MPI_FUNC
dnl - MPI_FUNC   is the body of MPI function call to be checked for existence
dnl              e.g.  MPI_VARS="MPI_Request request; MPI_Fint a;"
dnl                    MPI_FUNC="a = MPI_Request_c2f( request );"
dnl              if MPI_FUNC is empty, assume linking with basic MPI program.
dnl              i.e. check if MPI definitions are valid
dnl
AC_DEFUN(PAC_MPI_LINK_F77_FUNC,[
dnl - set local parallel compiler environments
dnl   so input variables can be F77, FFLAGS or LIBS
    pac_MPI_F77="$1"
    pac_MPI_FFLAGS="$2"
    pac_MPI_LIBS="$3"
    AC_LANG_SAVE
    AC_LANG_FORTRAN77
dnl - save the original environment
    pac_saved_F77="$F77"
    pac_saved_FFLAGS="$FFLAGS"
    pac_saved_LIBS="$LIBS"
dnl - set the parallel compiler environment for TRY_LINK
    F77="$pac_MPI_F77"
    FFLAGS="$pac_MPI_FFLAGS"
    LIBS="$pac_MPI_LIBS"
    AC_TRY_LINK( , [ mpiftest()
      end
	subroutine mpiftest()
	include 'mpif.h'
	integer pac_ierr
	$4
	call MPI_Init( pac_ierr )
	$5
	call MPI_Finalize( pac_ierr )
	return
                 ], pac_mpi_working=yes, pac_mpi_working=no )
    F77="$pac_saved_F77"
    FFLAGS="$pac_saved_FFLAGS"
    LIBS="$pac_saved_LIBS"
    AC_LANG_RESTORE
    if test "$pac_mpi_working" = "yes" ; then
       ifelse([$6],,:,[$6])
    else
       ifelse([$7],,:,[$7])
    fi
])dnl
dnl
dnl PAC_MPI_RUN_CC_PGM( MPI_CC, MPI_CFLAGS,
dnl                     SER_CC, SER_CFLAGS, SER_LIBS,
dnl                     PGM_BODY,
dnl                     [action if working], [action if not working] )
dnl - MPI_CC     MPI parallel compiler for compilation of the PGM_BODY
dnl              e.g. mpich/mpicc & AIX/mpcc
dnl - MPI_CFLAGS FFLAGS to MPI_CC for compilation of the PGM_BODY
dnl              e.g. "-I/usr/include" for mpi.h
dnl - SER_CC     serial compiler, like AIX/xlc, to generate of serial exeutable
dnl - SER_CFLAGS FFLAGS to SER_CC, used to generate serial executable
dnl - SER_LIBS   LIBS to SER_CC, used to generate serial executable
dnl - PGM_BODY   C program body
dnl
AC_DEFUN(PAC_MPI_RUN_CC_PGM,[
dnl - set local parallel and serial compiler environments
dnl   so input variables can be CC, CFLAGS or LIBS
    pac_MPI_CC="$1"
    pac_MPI_CFLAGS="$2"
    pac_SER_CC="$3"
    pac_SER_CFLAGS="$4"
    pac_SER_LIBS="$5"
    AC_LANG_SAVE
    AC_LANG_C
dnl - save the original environment
    pac_saved_CC="$CC"
    pac_saved_CFLAGS="$CFLAGS"
    pac_saved_LIBS="$LIBS"
dnl - set the parallel compiler environment
    CC="$pac_MPI_CC"
    CFLAGS="$pac_MPI_CFLAGS"
dnl
    rm -rf conftest*
    cat > conftest.c <<EOF
#include "confdefs.h"
$6
EOF
dnl
    pac_compile='${CC-cc} -c $CFLAGS conftest.c 1>&AC_FD_CC'
    if AC_TRY_EVAL(pac_compile) && test -s conftest.${ac_objext} ; then
        CC="$pac_SER_CC"
        CFLAGS="$pac_SER_CFLAGS"
        LIBS="$pac_SER_LIBS"
	pac_link='${CC-cc} -o conftest${ac_exeext} $CFLAGS $LDFLAGS conftest.${ac_objext} $LIBS 1>&AC_FD_CC'
        if AC_TRY_EVAL(pac_link) && test -s conftest${ac_exeext} && (./conftest${ac_exeext} ; exit) 2>/dev/null ; then
	    pac_mpi_working=yes
	else
	    pac_mpi_working=no
	fi
    else
        pac_mpi_working=no
    fi
    rm -rf conftest*
    CC="$pac_saved_CC"
    CFLAGS="$pac_saved_CFLAGS"
    LIBS="$pac_saved_LIBS"
    AC_LANG_RESTORE
    if test "$pac_mpi_working" = "yes" ; then
       ifelse([$7],,:,[$7])
    else
       ifelse([$8],,:,[$8])
    fi
])dnl
dnl
dnl PAC_MPI_RUN_F77_PGM( MPI_F77, MPI_FFLAGS,
dnl                      SER_F77, SER_FFLAGS, SER_LIBS,
dnl                      PGM_BODY,
dnl                      [action if working], [action if not working] )
dnl - MPI_F77    MPI parallel compiler for compilation of the PGM_BODY
dnl              e.g. mpich/mpif77 & AIX/mpxlf
dnl - MPI_FFLAGS FFLAGS to MPI_F77 for compilation of the PGM_BODY
dnl              e.g. "-I/usr/include" for mpif.h
dnl - SER_F77    serial compiler, like AIX/xlf, to generate of serial exeutable
dnl - SER_FFLAGS FFLAGS to SER_F77, used to generate serial executable
dnl - SER_LIBS   LIBS to SER_F77, used to generate serial executable
dnl - PGM_BODY   F77 program body
dnl
AC_DEFUN(PAC_MPI_RUN_F77_PGM,[
dnl - set local parallel and serial compiler environments
dnl   so input variables can be F77, FFLAGS or LIBS
    pac_MPI_F77="$1"
    pac_MPI_FFLAGS="$2"
    pac_SER_F77="$3"
    pac_SER_FFLAGS="$4"
    pac_SER_LIBS="$5"
    AC_LANG_SAVE
    AC_LANG_FORTRAN77
dnl - save the original environment
    pac_saved_F77="$F77"
    pac_saved_FFLAGS="$FFLAGS"
    pac_saved_LIBS="$LIBS"
dnl - set the parallel compiler environment
    F77="$pac_MPI_F77"
    FFLAGS="$pac_MPI_FFLAGS"
dnl
    rm -rf conftest*
    cat > conftest.f <<EOF
	$6
EOF
dnl
    pac_compile='${F77-f77} -c $FFLAGS conftest.f 1>&AC_FD_CC'
    if AC_TRY_EVAL(pac_compile) && test -s conftest.${ac_objext} ; then
        F77="$pac_SER_F77"
        FFLAGS="$pac_SER_FFLAGS"
        LIBS="$pac_SER_LIBS"
	pac_link='${F77-f77} -o conftest${ac_exeext} $FFLAGS $LDFLAGS conftest.${ac_objext} $LIBS 1>&AC_FD_CC'
        if AC_TRY_EVAL(pac_link) && test -s conftest${ac_exeext} && (./conftest${ac_exeext} ; exit) 2>/dev/null ; then
	    pac_mpi_working=yes
	else
	    pac_mpi_working=no
	fi
    else
        pac_mpi_working=no
    fi
    rm -rf conftest*
    F77="$pac_saved_F77"
    FFLAGS="$pac_saved_FFLAGS"
    LIBS="$pac_saved_LIBS"
    AC_LANG_RESTORE
    if test "$pac_mpi_working" = "yes" ; then
       ifelse([$7],,:,[$7])
    else
       ifelse([$8],,:,[$8])
    fi
])dnl
dnl
dnl PAC_MPI_RUN_F77_FUNC_FROM_C( MPI_F77, MPI_FFLAGS,
dnl                              MPI_CC, MPI_CFLAGS,
dnl                              SER_CC, SER_CFLAGS, SER_LIBS,
dnl                              [ FORTRAN routine ], [ C program ],
dnl                              [action if working], [action if not working] )
dnl - MPI_F77    MPI parallel compiler for compilation of the FORTRAN routine
dnl              e.g. mpich/mpif77 & AIX/mpxlf.  It can be serial compiler if 
dnl              no MPI variables in the FORTRAN routine
dnl - MPI_FFLAGS FFLAGS to MPI_F77 for compilation of the FORTRAN routine
dnl              e.g. "-I/usr/include" for mpif.h
dnl - MPI_CC     MPI parallel compiler for compilation of the C program
dnl              e.g. mpich/mpicc & AIX/mpcc.  It can be serial compiler if
dnl              no MPI variables in the C program
dnl - MPI_CFLAGS FFLAGS to MPI_CC for compilation of the C program
dnl              e.g. "-I/usr/include" for mpi.h
dnl - SER_CC     serial compiler, like AIX/xlc, to generate of serial exeutable
dnl - SER_CFLAGS FFLAGS to SER_CC, used to generate serial executable
dnl - SER_LIBS   LIBS to SER_CC, used to generate serial executable
dnl User needs to put in #define F77_NAME_xxxx to get C code to understand
dnl the fortran subroutine name.
dnl 
AC_DEFUN(PAC_MPI_RUN_F77_FUNC_FROM_C,[
dnl - set local parallel compiler environments
dnl   so input variables can be CC, CFLAGS or LIBS
    pac_MPI_F77="$1"
    pac_MPI_FFLAGS="$2"
    pac_MPI_CC="$3"
    pac_MPI_CFLAGS="$4"
    pac_SER_CC="$5"
    pac_SER_CFLAGS="$6"
    pac_SER_LIBS="$7"
dnl
    AC_LANG_SAVE
    AC_LANG_FORTRAN77
dnl - save the original environment
    pac_saved_F77="$F77"
    pac_saved_FFLAGS="$FFLAGS"
    pac_saved_LIBS="$LIBS"
dnl - set the parallel compiler environment
    F77="$pac_MPI_F77"
    FFLAGS="$pac_MPI_FFLAGS"
dnl
    rm -f conftest*
    cat > conftestf.f <<EOF
	$8
EOF
dnl - try compile f77 program
    pac_Fcompile='${F77-f77} -c $FFLAGS conftestf.f 1>&AC_FD_CC'
    if AC_TRY_EVAL(pac_Fcompile) && test -s conftestf.${ac_objext} ; then
        pac_mpi_working=yes
    else
        pac_mpi_working=no
    fi
    F77="$pac_saved_F77"
    FFLAGS="$pac_saved_FFLAGS"
dnl
    AC_LANG_C
dnl - set the parallel compiler environment
    CC="$pac_MPI_CC"
    CFLAGS="$pac_MPI_CFLAGS"
dnl
    cat > conftestc.c <<EOF
#include "confdefs.h"
$9
EOF
    pac_Ccompile='${CC-cc} -c $CFLAGS conftestc.c 1>&AC_FD_CC'
dnl - try compile C program
    if test "$pac_mpi_working" = "yes" && AC_TRY_EVAL(pac_Ccompile) && test -s conftestc.${ac_objext} ; then
        CC="$pac_SER_CC"
	CFLAGS="$pac_SER_CFLAGS"
	LIBS="$pac_SER_LIBS"
        pac_link='${CC-cc} -o conftest${ac_exeext} $CFLAGS $LDFLAGS conftestc.${ac_objext} conftestf.${ac_objext} $LIBS 1>&AC_FD_CC'
	if AC_TRY_EVAL(pac_link) && test -s conftest${ac_exeext} && (./conftest${ac_exeext} ; exit) 2>/dev/null ; then
            pac_mpi_working=yes
        else
            pac_mpi_working=no
        fi
    else
        pac_mpi_working=no
    fi
    rm -f conftest*
    CC="$pac_saved_CC"
    CFLAGS="$pac_saved_CFLAGS"
    LIBS="$pac_saved_LIBS"
    AC_LANG_RESTORE
    if test "$pac_mpi_working" = "yes" ; then
        ifelse([$10],,:,[$10])
    else
        ifelse([$11],,:,[$11])
    fi
])
