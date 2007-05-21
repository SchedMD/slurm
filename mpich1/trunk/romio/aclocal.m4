dnl
dnl This files contains additional macros for using autoconf to 
dnl build configure scripts.
dnl
dnl Almost all of this file is taken from the aclocal.m4 of MPICH
dnl
dnl Get the format of Fortran names.  Uses F77, FFLAGS, and sets WDEF.
dnl If the test fails, sets NOF77 to 1, HAVE_FORTRAN to 0.
dnl
dnl
AC_DEFUN([PAC_GET_FORTNAMES],[
   rm -f confftest.f confftest.o
   cat > confftest.f <<EOF
       subroutine mpir_init_fop( a )
       integer a
       a = 1
       return
       end
EOF
   $F77 $FFLAGS -c confftest.f > /dev/null 2>&1
   if test ! -s confftest.o ; then
	AC_MSG_WARN([Unable to test Fortran compiler.  Compiling a test 
program failed to produce an object file])
	NOF77=1
   elif test -z "$FORTRANNAMES" ; then
     # MAC OS X (and probably FreeBSD need strings - (not strings -a)
     # Cray doesn't accept -a ...
     allstrings="-a"
     if test $arch_CRAY ; then 
	allstrings="" 
     elif strings - confftest.o < /dev/null >/dev/null 2>&1 ; then
         allstrings="-"
     elif strings -a confftest.o < /dev/null >/dev/null 2>&1 ; then
         allstrings="-a"
     fi
    
     nameform1=`strings $allstrings confftest.o | grep mpir_init_fop_  | head -1`
     nameform2=`strings $allstrings confftest.o | grep MPIR_INIT_FOP   | head -1`
     nameform3=`strings $allstrings confftest.o | grep mpir_init_fop   | head -1`
     nameform4=`strings $allstrings confftest.o | grep mpir_init_fop__ | head -1`
    rm -f confftest.f confftest.o
    if test -n "$nameform4" ; then
	echo "Fortran externals are lower case and have two trailing underscores"
	FORTRANNAMES="FORTRANDOUBLEUNDERSCORE"
    elif test -n "$nameform1" ; then
        # We don't set this in CFLAGS; it is a default case
        echo "Fortran externals have a trailing underscore and are lowercase"
	FORTRANNAMES="FORTRANUNDERSCORE"
    elif test -n "$nameform2" ; then
	echo "Fortran externals are uppercase"     
	FORTRANNAMES="FORTRANCAPS" 
    elif test -n "$nameform3" ; then
	echo "Fortran externals are lower case"
	FORTRANNAMES="FORTRANNOUNDERSCORE"
    else
	AC_MSG_WARN([Unable to determine the form of Fortran external names.
Make sure that the compiler $F77 can be run on this system.
Turning off Fortran (-nof77 being assumed)])
	NOF77=1
    fi
    fi
    if test -n "$FORTRANNAMES" ; then
        WDEF="-D$FORTRANNAMES"
    fi
    rm -f confftest.f confftest.o
    ])dnl
dnl
define(PAC_GET_SPECIAL_SYSTEM_INFO,[
#
if test -n "$arch_IRIX"; then
   AC_MSG_CHECKING(for IRIX OS version)
   dnl This block of code replaces a generic "IRIX" arch value with
   dnl  IRIX_<version>_<chip>
   dnl  For example
   dnl  IRIX_5_4400 (IRIX 5.x, using MIPS 4400)
   osversion=`uname -r | sed 's/\..*//'`
   dnl Note that we need to allow brackets here, so we briefly turn off 
   dnl the macro quotes
   changequote(,)dnl
   dnl Get the second field (looking for 6.1)
   osvminor=`uname -r | sed 's/[0-9]\.\([0-9]*\)\..*/\1/'`
   changequote([,])dnl
   AC_MSG_RESULT($osversion)
   dnl Get SGI processor count by quick hack
   AC_MSG_CHECKING(for IRIX cpucount)
   changequote(,)dnl
   cpucount=`hinv | grep '[0-9]* [0-9]* MHZ IP[0-9]* Proc' | cut -f 1 -d' '`
   if test "$cpucount" = "" ; then
     cpucount=`hinv | grep 'Processor [0-9]*:' | wc -l | sed -e 's/ //g'`
   fi
   changequote([,])dnl
   if test "$cpucount" = "" ; then
     AC_MSG_RESULT([Could not determine cpucount.  Please send])
     hinv
     AC_MSG_ERROR([to romio-maint@mcs.anl.gov])
   fi
   AC_MSG_RESULT($cpucount)
   dnl
   AC_MSG_CHECKING(for IRIX cpumodel)
   dnl The tail -1 is necessary for multiple processor SGI boxes
   dnl We might use this to detect SGI multiprocessors and recommend
   dnl -comm=shared
   cputype=`hinv -t cpu | tail -1 | cut -f 3 -d' '`
   if test -z "$cputype" ; then
        AC_MSG_RESULT([Could not get cputype from hinv -t cpu command. Please send])
        hinv -t cpu 2>&1
        hinv -t cpu | cut -f 3 -d' ' 2>&1
	AC_MSG_ERROR([to romio-maint@mcs.anl.gov])
   fi
   AC_MSG_RESULT($cputype)
   dnl echo "checking for osversion and cputype"
   dnl cputype may contain R4400, R2000A/R3000, or something else.  
   dnl We may eventually need to look at it.
   if test -z "$osversion" ; then
        AC_MSG_RESULT([Could not determine OS version.  Please send])
        uname -a
        AC_MSG_ERROR([to romio-maint@mcs.anl.gov])
   elif test $osversion = 4 ; then
        true
   elif test $osversion = 5 ; then
        true
   elif test $osversion = 6 ; then
        true
   else 
       AC_MSG_RESULT([Could not recognize the version of IRIX (got $osversion).
ROMIO knows about versions 4, 5 and 6; the version being returned from 
uname -r is $osversion.  Please send])
       uname -a 2>&1
       hinv 2>&1
       AC_MSG_ERROR([to romio-maint@mcs.anl.gov])
   fi
   AC_MSG_CHECKING(for cputype)
   OLD_ARCH=IRIX
   IRIXARCH="$ARCH_$osversion"
   dnl Now, handle the chip set
   changequote(,)dnl
   cputype=`echo $cputype | sed -e 's%.*/%%' -e 's/R//' | tr -d "[A-Z]"`
   changequote([,])dnl
   case $cputype in 
        3000) ;;
        4000) ;;
        4400) ;;
        4600) ;;
        5000) ;;
        8000) ;;
        10000);;
	12000);;
        *)
	AC_MSG_WARN([Unexpected IRIX/MIPS chipset $cputype.  Please send the output])
        uname -a 2>&1
        hinv 2>&1 
        AC_MSG_WARN([to romio-maint@mcs.anl.gov
ROMIO will continue and assume that the cputype is
compatible with a MIPS 4400 processor.])
        cputype=4400
        ;;
   esac
   AC_MSG_RESULT($cputype)
   IRIXARCH="$IRIXARCH_$cputype"
   echo "IRIX-specific architecture is $IRIXARCH"
fi
])dnl
dnl
dnl
define(PAC_TEST_MPI,[
  AC_MSG_CHECKING(if a simple MPI program compiles and links)
  rm -f mpitest.c
  cat > mpitest.c <<EOF
#include "mpi.h"
     main(int argc, char **argv)
     {
         MPI_Init(&argc,&argv);
         MPI_Finalize(); 
     }
EOF
  rm -f conftest
  $CC $USER_CFLAGS -I$MPI_INCLUDE_DIR -o conftest mpitest.c $MPI_LIB > /dev/null 2>&1
  if test ! -x conftest ; then
      rm -f conftest mpitest.c
      AC_MSG_ERROR([Unable to compile a simple MPI program.
Use environment variables to provide the location of MPI libraries and
include directories])
  else
      rm -f conftest mpitest.c
  fi
AC_MSG_RESULT(yes)
])dnl
dnl
dnl
dnl
define(PAC_NEEDS_FINT,[
  AC_MSG_CHECKING(if MPI_Fint is defined in the MPI implementation)
  cat > mpitest1.c <<EOF
#include "mpi.h"
     main()
     {
         MPI_Fint i;
         i = 0;
     }
EOF
  rm -f mpitest1.o
  $CC $USER_CFLAGS -I$MPI_INCLUDE_DIR -c mpitest1.c > /dev/null 2>&1
  if test ! -s mpitest1.o ; then
      NEEDS_MPI_FINT="#define NEEDS_MPI_FINT"
      CFLAGS="$CFLAGS -DNEEDS_MPI_FINT"
      AC_MSG_RESULT(no)
      rm -f mpitest1.o mpitest1.c
  else
      NEEDS_MPI_FINT=""
      AC_MSG_RESULT(yes)
      rm -f mpitest1.o mpitest1.c
  fi
])dnl
dnl
define(PAC_MPI_LONG_LONG_INT,[
  AC_MSG_CHECKING(if MPI_LONG_LONG_INT is defined in mpi.h)
  rm -f mpitest.c
  cat > mpitest.c <<EOF
#include "mpi.h"
     main(int argc, char **argv)
     {
         long long i;   
         MPI_Init(&argc,&argv);
         MPI_Send(&i, 1, MPI_LONG_LONG_INT, 0, 0, MPI_COMM_WORLD);
         MPI_Finalize(); 
     }
EOF
  rm -f conftest
  $CC $USER_CFLAGS -I$MPI_INCLUDE_DIR -o conftest mpitest.c $MPI_LIB > /dev/null 2>&1
  if test -x conftest ; then
      AC_MSG_RESULT(yes)
      AC_DEFINE(HAVE_MPI_LONG_LONG_INT,,[Define if mpi has long long it])
  else
      AC_MSG_RESULT(no)
  fi
  rm -f conftest mpitest.c
])dnl
dnl
dnl PAC_LONG_LONG_64: check if there is a 64-bit long long
dnl
define(PAC_LONG_LONG_64,[
if test -n "$longlongsize" ; then
    if test "$longlongsize" = 8 ; then
       echo "defining MPI_Offset as long long in C and integer*8 in Fortran" 
       AC_DEFINE(HAVE_LONG_LONG_64,,[Define if long long is 64 bits])
       DEFINE_MPI_OFFSET="typedef long long MPI_Offset;"
       FORTRAN_MPI_OFFSET="integer*8"
       LL="\%lld"
    elif test "$longlongsize" = "int" ; then  # a hack to set MPI_Offset as int
       echo "defining MPI_Offset as int in C and integer in Fortran"
       DEFINE_MPI_OFFSET="typedef int MPI_Offset;"
       FORTRAN_MPI_OFFSET="integer"
       AC_DEFINE(MPI_OFFSET_IS_INT,,[Define if MPI_Offset is int])
       LL="\%d"
       MPI_OFFSET_KIND1="!"
       MPI_OFFSET_KIND2="!"
    else 
       echo "defining MPI_Offset as long in C and integer in Fortran" 
       DEFINE_MPI_OFFSET="typedef long MPI_Offset;"
       FORTRAN_MPI_OFFSET="integer"
       LL="\%ld"
       MPI_OFFSET_KIND1="!"
       MPI_OFFSET_KIND2="!"
    fi
else
   PAC_GET_TYPE_SIZE(long long, longlongsize)
   if test -n "$longlongsize" ; then
      if test "$longlongsize" = 8 ; then
         PAC_TEST_LONG_LONG()
      else
         echo "defining MPI_Offset as long in C and integer in Fortran" 
         DEFINE_MPI_OFFSET="typedef long MPI_Offset;"
         FORTRAN_MPI_OFFSET="integer"
         LL="\%ld"
         MPI_OFFSET_KIND1="!"
         MPI_OFFSET_KIND2="!"
      fi
   else 
dnl   check if longlong is not supported or only its size cannot be determined
dnl   because the program cannot be run.
      rm -f ltest.c
      cat > ltest.c <<EOF
        main()
        {
           long long i=8;
           return 0;
        }
EOF
      rm -f conftest
      $CC $USER_CFLAGS -o conftest ltest.c > /dev/null 2>&1
      if test -x conftest ; then
         echo "assuming size of long long is 8bytes; use '-longlongsize' to indicate otherwise"
         rm -f conftest ltest.c
         echo "defining MPI_Offset as long long in C and integer*8 in Fortran" 
         AC_DEFINE(HAVE_LONG_LONG_64,,[Define if long long is 64 bits])
         DEFINE_MPI_OFFSET="typedef long long MPI_Offset;"
         FORTRAN_MPI_OFFSET="integer*8"
         LL="\%lld"
      else 
         echo "assuming long long is not available; use '-longlongsize' to indicate otherwise"
         echo "defining MPI_Offset as long in C and integer in Fortran" 
         DEFINE_MPI_OFFSET="typedef long MPI_Offset;"
         FORTRAN_MPI_OFFSET="integer"
         LL="\%ld"
         MPI_OFFSET_KIND1="!"
         MPI_OFFSET_KIND2="!"
      fi
   fi
fi
])dnl
dnl
dnl
define(PAC_MPI_INFO,[
  AC_MSG_CHECKING(if MPI_Info functions are defined in the MPI implementation)
  rm -f mpitest.c
  cat > mpitest.c <<EOF
#include "mpi.h"
     main(int argc, char **argv)
     {
         MPI_Info info;
         MPI_Init(&argc,&argv);
         MPI_Info_create(&info);
         MPI_Finalize(); 
     }
EOF
  rm -f conftest
  $CC $USER_CFLAGS -I$MPI_INCLUDE_DIR -o conftest mpitest.c $MPI_LIB > /dev/null 2>&1
  if test -x conftest ; then
      AC_MSG_RESULT(yes)
      AC_DEFINE(HAVE_MPI_INFO,1,[Define if MPI_Info available])
      HAVE_MPI_INFO="#define HAVE_MPI_INFO"
      MPI_FINFO1="!"
      MPI_FINFO2="!"
      MPI_FINFO3="!"
      MPI_FINFO4="!"
  else
      AC_MSG_RESULT(no)
      BUILD_MPI_INFO=1
      MPI_FINFO1="      INTEGER MPI_MAX_INFO_KEY, MPI_MAX_INFO_VAL"
      MPI_FINFO2="      PARAMETER (MPI_MAX_INFO_KEY=255, MPI_MAX_INFO_VAL=1024)"
      MPI_FINFO3="      INTEGER MPI_INFO_NULL"
      MPI_FINFO4="      PARAMETER (MPI_INFO_NULL=0)"
  fi
  rm -f conftest mpitest.c
])dnl
dnl
dnl
define(PAC_MPI_DARRAY_SUBARRAY,[
  AC_MSG_CHECKING(if darray and subarray constructors are defined in the MPI implementation)
  rm -f mpitest.c
  cat > mpitest.c <<EOF
#include "mpi.h"
     main(int argc, char **argv)
     {
         int i=MPI_DISTRIBUTE_CYCLIC;
         MPI_Datatype t;
         MPI_Init(&argc,&argv);
         MPI_Type_create_darray(i, i, i, &i, &i, &i, &i, i, MPI_INT, &t);
         MPI_Type_create_subarray(i, &i, &i, &i, i, MPI_INT, &t);
         MPI_Finalize(); 
     }
EOF
  rm -f conftest
  $CC $USER_CFLAGS -I$MPI_INCLUDE_DIR -o conftest mpitest.c $MPI_LIB > /dev/null 2>&1
  if test -x conftest ; then
      AC_MSG_RESULT(yes)
      AC_DEFINE(HAVE_MPI_DARRAY_SUBARRAY,,[Define if MPI Darray available])
      HAVE_MPI_DARRAY_SUBARRAY="#define HAVE_MPI_DARRAY_SUBARRAY"
      MPI_FARRAY1="!"
      MPI_FARRAY2="!"
      MPI_FARRAY3="!"
      MPI_FARRAY4="!"
      MPI_FARRAY5="!"
      MPI_FARRAY6="!"
      MPI_FARRAY7="!"
  else
      AC_MSG_RESULT(no)
      BUILD_MPI_ARRAY=1
      MPI_FARRAY1="      INTEGER MPI_ORDER_C, MPI_ORDER_FORTRAN"
      MPI_FARRAY2="      PARAMETER (MPI_ORDER_C=56, MPI_ORDER_FORTRAN=57)"
      MPI_FARRAY3="      INTEGER MPI_DISTRIBUTE_BLOCK, MPI_DISTRIBUTE_CYCLIC"
      MPI_FARRAY4="      INTEGER MPI_DISTRIBUTE_NONE, MPI_DISTRIBUTE_DFLT_DARG"
      MPI_FARRAY5="      PARAMETER (MPI_DISTRIBUTE_BLOCK=121, MPI_DISTRIBUTE_CYCLIC=122)"
      MPI_FARRAY6="      PARAMETER (MPI_DISTRIBUTE_NONE=123)"
      MPI_FARRAY7="      PARAMETER (MPI_DISTRIBUTE_DFLT_DARG=-49767)"
  fi
  rm -f conftest mpitest.c
])dnl
dnl
dnl
define(PAC_CHECK_MPI_SGI_INFO_NULL,[
  AC_MSG_CHECKING([if MPI_INFO_NULL is defined in mpi.h])
  rm -f mpitest.c
  cat > mpitest.c <<EOF
#include "mpi.h"
     main(int argc, char **argv)
     {
	int i;
	i = MPI_INFO_NULL;
     }
EOF
  rm -f conftest
  $CC $USER_CFLAGS -I$MPI_INCLUDE_DIR -o conftest mpitest.c $MPI_LIB > /dev/null 2>&1
  if test -x conftest ; then
      AC_MSG_RESULT(yes)
      cp adio/sgi/mpi3.1/*.h include
  else
      AC_MSG_RESULT(no)
  fi
  rm -f conftest mpitest.c
])dnl
dnl
dnl
dnl
define(PAC_CHECK_MPIOF_H,[
  AC_MSG_CHECKING(if mpiof.h is included in mpif.h)
  rm -f mpitest.f
  cat > mpitest.f <<EOF
      program main
      implicit none
      include 'mpif.h'
      integer i
      i = MPI_MODE_RDWR
      stop
      end
EOF
  rm -f conftest
  $F77 $FFLAGS -I$MPI_INCLUDE_DIR -o conftest mpitest.f $MPI_LIB > /dev/null 2>&1
  if test -x conftest ; then
      AC_MSG_RESULT(yes)
      MPIOF_H_INCLUDED=1
  else
      AC_MSG_RESULT(no)
  fi
  rm -f conftest mpitest.f
])dnl
dnl
dnl
dnl check if pread64 is defined in IRIX. needed on IRIX 6.5
dnl
define(PAC_HAVE_PREAD64,[
  AC_MSG_CHECKING(if pread64 is defined)
  rm -f conftest.c
  cat > conftest.c <<EOF
#include <unistd.h>
     main()
     {
         int fd=0, buf=0, i=0;
         off64_t off=0;
         pread64(fd, &buf, i, off);
     }
EOF
  rm -f conftest
  $CC $USER_CFLAGS -o conftest conftest.c > /dev/null 2>&1
  if test -x conftest ; then
      AC_MSG_RESULT(yes)
      AC_DEFINE(HAVE_PREAD64,,[Define if pread64 available])
  else
      AC_MSG_RESULT(no)
  fi
rm -f conftest conftest.c
])dnl
dnl
dnl
define(PAC_TEST_MPI_SGI_type_is_contig,[
  AC_MSG_CHECKING(if MPI_SGI_type_is_contig is defined)
  rm -f mpitest.c
  cat > mpitest.c <<EOF
#include "mpi.h"
     main(int argc, char **argv)
     {
         MPI_Datatype type;
         int i;

         MPI_Init(&argc,&argv);
         i = MPI_SGI_type_is_contig(type);
         MPI_Finalize(); 
     }
EOF
  rm -f conftest
  $CC $USER_CFLAGS -I$MPI_INCLUDE_DIR -o conftest mpitest.c $MPI_LIB > /dev/null 2>&1
  if test -x conftest ; then
     AC_MSG_RESULT(yes)
  else
     AC_MSG_RESULT(no)
     AC_DEFINE(NO_MPI_SGI_type_is_contig,,[Define if no MPI type is contig])
  fi
  rm -f conftest mpitest.c
])dnl
dnl
dnl
dnl
define(PAC_TEST_MPI_COMBINERS,[
  AC_MSG_CHECKING(if MPI-2 combiners are defined in mpi.h)
  rm -f mpitest.c
  cat > mpitest.c <<EOF
#include "mpi.h"
     main(int argc, char **argv)
     {
         int i;

         MPI_Init(&argc,&argv);
         i = MPI_COMBINER_STRUCT;
         MPI_Finalize(); 
     }
EOF
  rm -f conftest
  $CC $USER_CFLAGS -I$MPI_INCLUDE_DIR -o conftest mpitest.c $MPI_LIB > /dev/null 2>&1
  if test -x conftest ; then
     AC_MSG_RESULT(yes)
     AC_DEFINE(HAVE_MPI_COMBINERS,,[Define if MPI combiners available])
  else
     AC_MSG_RESULT(no)
  fi
  rm -f conftest mpitest.c
])dnl
dnl
dnl
dnl PAC_MPI_OFFSET_KIND()
dnl
dnl tries to determine the Fortran 90 kind parameter for 8-byte integers
dnl
define(PAC_MPI_OFFSET_KIND,
[rm -f conftest*
# Determine the extension for Fortran 90 files (not all compilers accept
# .f and not all accept .f90)
if test -z "$ac_f90ext" ; then
    if test -z "$F90" ; then
       AC_CHECK_PROGS(F90,f90 xlf90 pgf90 ifort epcf90 f95 fort xlf95 lf95 pathf90 g95 fc ifc efc)
    fi
    AC_MSG_CHECKING([for extension for Fortran 90 programs])
    ac_f90ext="f90"
    ac_f90compile='${F90-f90} -c $F90FLAGS conftest.$ac_f90ext 1>&AC_FD_CC'
    cat > conftest.$ac_f90ext <<EOF
      program conftest
      end
EOF
    if AC_TRY_EVAL(ac_f90compile) ; then
        AC_MSG_RESULT([f90])
    else
        rm -f conftest*
        ac_f90ext="f"
        cat > conftest.$ac_f90ext <<EOF
      program conftest
      end
EOF
        if AC_TRY_EVAL(ac_f90compile) ; then
            AC_MSG_RESULT([f])
        else
            AC_MSG_RESULT([unknown!])
        fi
    fi
fi
AC_MSG_CHECKING([for Fortran 90 KIND parameter for 8-byte integers])
cat <<EOF > conftest.$ac_f90ext
      program main
      integer i
      i = selected_int_kind(16)
      open(8, file="conftest.out", form="formatted")
      write (8,*) i
      close(8)
      stop
      end
EOF
if test -z "$F90" ; then
   F90=f90
fi
KINDVAL=""
if $F90 -o conftest conftest.$ac_f90ext >/dev/null 2>&1 ; then
    ./conftest >/dev/null 2>&1
    if test -s conftest.out ; then 
        KINDVAL=`cat conftest.out`
    fi
fi
rm -f conftest*
if test -n "$KINDVAL" -a "$KINDVAL" != "-1" ; then
   AC_MSG_RESULT($KINDVAL)
   MPI_OFFSET_KIND1="      INTEGER MPI_OFFSET_KIND"
   MPI_OFFSET_KIND2="      PARAMETER (MPI_OFFSET_KIND=$KINDVAL)"
else
    AC_MSG_RESULT(unavailable)
fi
])dnl
dnl
dnl
define(PAC_TEST_MPI_HAVE_OFFSET_KIND,[
  AC_MSG_CHECKING(if MPI_OFFSET_KIND is defined in mpif.h)
  rm -f mpitest.f
  cat > mpitest.f <<EOF
      program main
      implicit none
      include 'mpif.h'
      integer i
      i = MPI_OFFSET_KIND
      stop
      end
EOF
  rm -f conftest
  $F77 $FFLAGS -I$MPI_INCLUDE_DIR -o conftest mpitest.f $MPI_LIB > /dev/null 2>&1
  if test -x conftest ; then
     AC_MSG_RESULT(yes)
     MPI_OFFSET_KIND1="!"
     MPI_OFFSET_KIND2="!"
  else
     AC_MSG_RESULT(no)
  fi
  rm -f conftest mpitest.f
])dnl
dnl
dnl
dnl PAC_GET_XFS_MEMALIGN
dnl 
dnl
define(PAC_GET_XFS_MEMALIGN,
[AC_MSG_CHECKING([for memory alignment needed for direct I/O])
/bin/rm -f memalignval
/bin/rm -f /tmp/romio_tmp.bin
AC_TEST_PROGRAM([#include <stdio.h>
#include <unistd.h>
#include <fcntl.h>
#include <stdio.h>
main() { 
  struct dioattr st;
  int fd = open("/tmp/romio_tmp.bin", O_RDWR | O_CREAT, 0644);
  FILE *f=fopen("memalignval","w");
  if (fd == -1) exit(1);
  if (!f) exit(1);
  fcntl(fd, F_DIOINFO, &st);
  fprintf( f, "%u\n", st.d_mem);
  exit(0);
}],Pac_CV_NAME=`cat memalignval`,Pac_CV_NAME="")
/bin/rm -f memalignval
/bin/rm -f /tmp/romio_tmp.bin
if test -n "$Pac_CV_NAME" -a "$Pac_CV_NAME" != 0 ; then
    AC_MSG_RESULT($Pac_CV_NAME)
    CFLAGS="$CFLAGS -DXFS_MEMALIGN=$Pac_CV_NAME"
else
    AC_MSG_RESULT(unavailable, assuming 128)
    CFLAGS="$CFLAGS -DXFS_MEMALIGN=128"
fi
])dnl
dnl

dnl
dnl Look for a style of VPATH.  Known forms are
dnl VPATH = .:dir
dnl .PATH: . dir
dnl
dnl Defines VPATH or .PATH with . $(srcdir)
dnl Requires that vpath work with implicit targets
dnl NEED TO DO: Check that $< works on explicit targets.
dnl
define(PAC_MAKE_VPATH,[
AC_SUBST(VPATH)
AC_MSG_CHECKING(for virtual path format)
rm -rf conftest*
mkdir conftestdir
cat >conftestdir/a.c <<EOF
A sample file
EOF
cat > conftest <<EOF
all: a.o
VPATH=.:conftestdir
.c.o:
	@echo \$<
EOF
ac_out=`$MAKE -f conftest 2>&1 | grep 'conftestdir/a.c'`
if test -n "$ac_out" ; then 
    AC_MSG_RESULT(VPATH)
    VPATH='VPATH=.:$(srcdir)'
else
    rm -f conftest
    cat > conftest <<EOF
all: a.o
.PATH: . conftestdir
.c.o:
	@echo \$<
EOF
    ac_out=`$MAKE -f conftest 2>&1 | grep 'conftestdir/a.c'`
    if test -n "$ac_out" ; then 
        AC_MSG_RESULT(.PATH)
        VPATH='.PATH: . $(srcdir)'
    else
	AC_MSG_RESULT(neither VPATH nor .PATH works)
    fi
fi
rm -rf conftest*
])dnl
dnl
define(PAC_HAVE_MOUNT_NFS,[
  AC_MSG_CHECKING([if MOUNT_NFS is defined in the include files])
  rm -f conftest.c
  cat > conftest.c <<EOF
#include <sys/param.h>
#include <sys/mount.h>
     main()
     {
         int i=MOUNT_NFS;
     }
EOF
  rm -f conftest
  $CC $USER_CFLAGS -o conftest conftest.c > /dev/null 2>&1
  if test -x conftest ; then
     AC_MSG_RESULT(yes)
     ROMIO_HAVE_MOUNT_NFS=1
     AC_DEFINE(HAVE_MOUNT_NFS,,[Define if MOUNT_NFS defined])
  else
     ROMIO_HAVE_MOUNT_NFS=0
     AC_MSG_RESULT(no)
  fi
  rm -f conftest conftest.c
])dnl
dnl
dnl
dnl PAC_MPI_OFFSET_KIND_4BYTE()
dnl
dnl tries to determine the Fortran 90 kind parameter for 4-byte integers
dnl
define(PAC_MPI_OFFSET_KIND_4BYTE,
[AC_MSG_CHECKING([for Fortran 90 KIND parameter for 4-byte integers])
rm -f kind.f kind.o kind
cat <<EOF > kind.f
      program main
      integer i
      i = selected_int_kind(8)
      open(8, file="k.out", form="formatted")
      write (8,*) i
      close(8)
      stop
      end
EOF
if test -z "$F90" ; then
   F90=f90
fi
KINDVAL=""
if $F90 -o kind kind.f >/dev/null 2>&1 ; then
    ./kind >/dev/null 2>&1
    if test -s k.out ; then 
        KINDVAL=`cat k.out`
    fi
fi
rm -f kind k.out kind.f kind.o
if test -n "$KINDVAL" -a "$KINDVAL" != "-1" ; then
   AC_MSG_RESULT($KINDVAL)
   MPI_OFFSET_KIND1="      INTEGER MPI_OFFSET_KIND"
   MPI_OFFSET_KIND2="      PARAMETER (MPI_OFFSET_KIND=$KINDVAL)"
else
    AC_MSG_RESULT(unavailable)
fi
])dnl
dnl
dnl
define(PAC_FUNC_STRERROR,[
  AC_MSG_CHECKING([for strerror()])
  rm -f conftest.c
  cat > conftest.c <<EOF
#include <string.h>
     main()
     {
        char *s = strerror(5);
     }
EOF
  rm -f conftest
  $CC $USER_CFLAGS -o conftest conftest.c >> config.log 2>&1
  if test -x conftest ; then
     AC_MSG_RESULT(yes)
     AC_DEFINE(HAVE_STRERROR,,[Define if strerror available])
  else
     AC_MSG_RESULT(no)
     AC_MSG_CHECKING([for sys_errlist])
     rm -f conftest.c
changequote(,)
     cat > conftest.c <<EOF
#include <stdio.h>
        main()
        {
           extern char *sys_errlist[];
	   printf("%s\n", sys_errlist[34]);
        }
EOF
changequote([,])
     rm -f conftest
     $CC $USER_CFLAGS -o conftest conftest.c > config.log 2>&1
     if test -x conftest ; then
        AC_MSG_RESULT(yes)
        AC_DEFINE(HAVE_SYSERRLIST,,[Define if syserrlist available])
     else
        AC_MSG_RESULT(no)
     fi
  fi
  rm -f conftest conftest.c
])dnl
dnl
define(PAC_TEST_MPIR_STATUS_SET_BYTES,[
  AC_MSG_CHECKING(if MPIR_Status_set_bytes is defined)
  rm -f mpitest.c
  cat > mpitest.c <<EOF
#include "mpi.h"
     main(int argc, char **argv)
     {
     	 MPI_Status status;
         MPI_Datatype type;
	 int err;

         MPI_Init(&argc,&argv);
         MPIR_Status_set_bytes(status,type,err);
         MPI_Finalize(); 
     }
EOF
  rm -f conftest
  $CC $USER_CFLAGS -I$MPI_INCLUDE_DIR -o conftest mpitest.c $MPI_LIB > /dev/null 2>&1
  if test -x conftest ; then
     AC_MSG_RESULT(yes)
     AC_DEFINE(HAVE_STATUS_SET_BYTES,,[Define if status set bytes available])
  else
     AC_MSG_RESULT(no)
  fi
  rm -f conftest mpitest.c
])dnl
define(PAC_TEST_MPI_GREQUEST,[
  AC_MSG_CHECKING(support for generalized requests)
  rm -f mpitest.c
  cat > mpitest.c <<EOF
#include "mpi.h"
#include "stdio.h"
    main(int argc, char **argv)
    {
       MPI_Request request;
       MPI_Init(&argc, &argv);
       MPI_Grequest_start(NULL, NULL, NULL, NULL, &request);
       MPI_Finalize();
     }
EOF
  rm -f conftest
  $CC $USER_CFLAGS -I$MPI_INCLUDE_DIR -o conftest mpitest.c $MPI_LIB > /dev/null 2>&1
  if test -x conftest ; then
     AC_MSG_RESULT(yes)
     AC_DEFINE(HAVE_MPI_GREQUEST,,[Define if generalized requests avaliable])
  else
     AC_MSG_RESULT(no)
  fi
  rm -f conftest mpitest.c
])dnl
dnl
dnl/*D
dnl PAC_FUNC_NEEDS_DECL - Set NEEDS_<funcname>_DECL if a declaration is needed
dnl
dnl Synopsis:
dnl PAC_FUNC_NEEDS_DECL(headerfiles,funcname)
dnl
dnl Output Effect:
dnl Sets 'NEEDS_<funcname>_DECL' if 'funcname' is not declared by the 
dnl headerfiles.
dnl
dnl Approach:
dnl Try to compile a program with the function, but passed with an incorrect
dnl calling sequence.  If the compilation fails, then the declaration
dnl is provided within the header files.  If the compilation succeeds,
dnl the declaration is required.
dnl
dnl We use a 'double' as the first argument to try and catch varargs
dnl routines that may use an int or pointer as the first argument.
dnl 
dnl D*/
AC_DEFUN(PAC_FUNC_NEEDS_DECL,[
AC_CACHE_CHECK([whether $2 needs a declaration],
pac_cv_func_decl_$2,[
AC_TRY_COMPILE([$1],[int a=$2(1.0,27,1.0,"foo");],
pac_cv_func_decl_$2=yes,pac_cv_func_decl_$2=no)])
if test "$pac_cv_func_decl_$2" = "yes" ; then
changequote(<<,>>)dnl
define(<<PAC_FUNC_NAME>>, translit(NEEDS_$2_DECL, [a-z *], [A-Z__]))dnl
changequote([, ])dnl
    AC_DEFINE_UNQUOTED(PAC_FUNC_NAME,1,[Define if $2 needs a declaration])
undefine([PAC_FUNC_NAME])
fi
])dnl
dnl
dnl/*D
dnl PAC_C_RESTRICT - Check if C supports restrict
dnl
dnl Synopsis:
dnl PAC_C_RESTRICT
dnl
dnl Output Effect:
dnl Defines 'restrict' if some version of restrict is supported; otherwise
dnl defines 'restrict' as empty.  This allows you to include 'restrict' in 
dnl declarations in the same way that 'AC_C_CONST' allows you to use 'const'
dnl in declarations even when the C compiler does not support 'const'
dnl
dnl Note that some compilers accept restrict only with additional options.
dnl DEC/Compaq/HP Alpha Unix (Tru64 etc.) -accept restrict_keyword
dnl
dnl D*/
AC_DEFUN(PAC_C_RESTRICT,[
AC_CACHE_CHECK([for restrict],
pac_cv_c_restrict,[
AC_TRY_COMPILE(,[int * restrict a;],pac_cv_c_restrict="restrict",
pac_cv_c_restrict="no")
if test "$pac_cv_c_restrict" = "no" ; then
   AC_TRY_COMPILE(,[int * _Restrict a;],pac_cv_c_restrict="_Restrict",
   pac_cv_c_restrict="no")
fi
if test "$pac_cv_c_restrict" = "no" ; then
   AC_TRY_COMPILE(,[int * __restrict a;],pac_cv_c_restrict="__restrict",
   pac_cv_c_restrict="no")
fi
])
if test "$pac_cv_c_restrict" = "no" ; then
  restrict_val=""
elif test "$pac_cv_c_restrict" != "restrict" ; then
  restrict_val=$pac_cv_c_restrict
fi
if test "$restrict_val" != "restrict" ; then 
  AC_DEFINE_UNQUOTED(restrict,$restrict_val,[if C does not support restrict])
fi
])dnl
dnl
dnl
dnl
dnl This is a replacement for AC_PROG_CC that does not prefer gcc and
dnl that does not mess with CFLAGS.  See acspecific.m4 for the original defn.
dnl
dnl/*D
dnl PAC_PROG_CC - Find a working C compiler
dnl
dnl Synopsis:
dnl PAC_PROG_CC
dnl
dnl Output Effect:
dnl   Sets the variable CC if it is not already set
dnl
dnl Notes:
dnl   Unlike AC_PROG_CC, this does not prefer gcc and does not set CFLAGS.
dnl   It does check that the compiler can compile a simple C program.
dnl   It also sets the variable GCC to yes if the compiler is gcc.  It does
dnl   not yet check for some special options needed in particular for 
dnl   parallel computers, such as -Tcray-t3e, or special options to get
dnl   full ANSI/ISO C, such as -Aa for HP.
dnl
dnl D*/
dnl 2.52 doesn't have AC_PROG_CC_GNU
ifdef([AC_PROG_CC_GNU],,[AC_DEFUN([AC_PROG_CC_GNU],)])
AC_DEFUN(PAC_PROG_CC,[
AC_PROVIDE([AC_PROG_CC])
AC_CHECK_PROGS(CC, cc xlC xlc pgcc icc gcc )
test -z "$CC" && AC_MSG_ERROR([no acceptable cc found in \$PATH])
PAC_PROG_CC_WORKS
AC_PROG_CC_GNU
if test "$ac_cv_prog_gcc" = yes; then
  GCC=yes
else
  GCC=
fi
])
dnl
dnl
dnl PAC_C_GNU_ATTRIBUTE - See if the GCC __attribute__ specifier is allow.
dnl Use the following
dnl #ifndef HAVE_GCC_ATTRIBUTE
dnl #define __attribute__(a)
dnl #endif
dnl If *not*, define __attribute__(a) as null
dnl
dnl We start by requiring Gcc.  Some other compilers accept __attribute__
dnl but generate warning messages, or have different interpretations 
dnl (which seems to make __attribute__ just as bad as #pragma) 
dnl For example, the Intel icc compiler accepts __attribute__ and
dnl __attribute__((pure)) but generates warnings for __attribute__((format...))
dnl
AC_DEFUN([PAC_C_GNU_ATTRIBUTE],[
AC_REQUIRE([AC_PROG_CC_GNU])
if test "$ac_cv_prog_gcc" = "yes" ; then
    AC_CACHE_CHECK([whether __attribute__ allowed],
pac_cv_gnu_attr_pure,[
AC_TRY_COMPILE([int foo(int) __attribute__ ((pure));],[int a;],
pac_cv_gnu_attr_pure=yes,pac_cv_gnu_attr_pure=no)])
AC_CACHE_CHECK([whether __attribute__((format)) allowed],
pac_cv_gnu_attr_format,[
AC_TRY_COMPILE([int foo(char *,...) __attribute__ ((format(printf,1,2)));],[int a;],
pac_cv_gnu_attr_format=yes,pac_cv_gnu_attr_format=no)])
    if test "$pac_cv_gnu_attr_pure" = "yes" -a "$pac_cv_gnu_attr_format" = "yes" ; then
        AC_DEFINE(HAVE_GCC_ATTRIBUTE,1,[Define if GNU __attribute__ is supported])
    fi
fi
])
dnl

