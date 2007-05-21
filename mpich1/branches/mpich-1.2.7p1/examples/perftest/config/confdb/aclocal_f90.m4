dnl
dnl Macros for Fortran 90
dnl
dnl We'd like to have a PAC_LANG_FORTRAN90 that worked with AC_TRY_xxx, but
dnl that would require too many changes to autoconf macros.
dnl
AC_DEFUN(PAC_LANG_FORTRAN90,
[AC_REQUIRE([PAC_PROG_F90])
define([AC_LANG], [FORTRAN90])dnl
ac_ext=$pac_cv_f90_ext
ac_compile='${F90-f90} -c $F90FLAGS conftest.$ac_ext 1>&AC_FD_CC'
ac_link='${F90-f90} -o conftest${ac_exeext} $F90FLAGS $LDFLAGS conftest.$ac_ext $LIBS 1>&AC_FD_CC'
dnl cross_compiling no longer maintained by autoconf as part of the
dnl AC_LANG changes.  If we set it here, a later AC_LANG may not 
dnl restore it (in the case where one compiler claims to be a cross compiler
dnl and another does not)
dnl cross_compiling=$pac_cv_prog_f90_cross
])
dnl
dnl This is an addition for AC_TRY_COMPILE, but for f90.  If the current 
dnl language is not f90, it does a save/restore
AC_DEFUN(PAC_TRY_F90_COMPILE,
[AC_REQUIRE([PAC_LANG_FORTRAN90])
ifelse(AC_LANG, FORTRAN90,,[AC_LANG_SAVE
PAC_LANG_FORTRAN90
define([NEED_POP],yes)])
cat > conftest.$ac_ext <<EOF
      program main
[$2]
      end
EOF
if AC_TRY_EVAL(ac_compile); then
  ifelse([$3], , :, [rm -rf conftest*
  $3])
else
  echo "configure: failed program was:" >&AC_FD_CC
  cat conftest.$ac_ext >&AC_FD_CC
ifelse([$4], , , [  rm -rf conftest*
  $4
])dnl
fi
rm -f conftest*
# The intel compiler sometimes generates these work.pc and .pcl files
rm -f work.pc work.pcl
ifelse(NEED_POP,yes,[
undefine([NEED_POP])
AC_LANG_RESTORE])
])
dnl/*D
dnl PAC_F90_MODULE_EXT - Determine the filename extension of F90 module
dnl
dnl Synopsis:
dnl PAC_F90_MODULE_EXT(action if found,action if not found)
dnl 
dnl Variables set include:
dnl +  pac_cv_f90_module_ext - extension for created modules
dnl -  pac_cv_f90_module_case - case of name for created module (where the
dnl      module name in the source file is lowercase).  One
dnl      of unknown, lower, upper
dnl D*/
AC_DEFUN(PAC_F90_MODULE_EXT,
[AC_CACHE_CHECK([for Fortran 90 module extension],
pac_cv_f90_module_ext,[
# aclocal_f90.m4 version
pac_cv_f90_module_case="unknown"
rm -f conftest*
# Intel ifc compiler generates files by the name of work.pc and work.pcl (!)
rm -f work.pc work.pcl
cat >conftest.$ac_f90ext <<EOF
        module conftest
        integer n
        parameter (n=1)
        end module conftest
EOF
if AC_TRY_EVAL(ac_f90compile) ; then
   dnl Look for module name
   dnl First, try to find known names.  This avoids confusion caused by
   dnl additional files (like <name>.stb created by some versions of pgf90)
   for name in conftest CONFTEST ; do
       for ext in mod MOD ; do
           if test -s $name.$ext ; then
               if test $name = conftest ; then
                   pac_cv_f90_module_case=lower
               else
                   pac_cv_f90_module_case=upper
               fi
               pac_cv_f90_module_ext=$ext
               pac_MOD=$ext
               break
           fi
       done
       if test -n "$pac_cv_f90_module_ext" ; then break ; fi
   done
   if test -z "$pac_MOD" ; then
		
       pac_MOD=`ls conftest.* 2>&1 | grep -v conftest.$ac_f90ext | grep -v conftest.o`
       pac_MOD=`echo $pac_MOD | sed -e 's/conftest\.//g'`
       pac_cv_f90_module_case="lower"
       if test "X$pac_MOD" = "X" ; then
	   pac_MOD=`ls CONFTEST* 2>&1 | grep -v CONFTEST.f | grep -v CONFTEST.o`
           pac_MOD=`echo $pac_MOD | sed -e 's/CONFTEST\.//g'`
	   if test -n "$pac_MOD" ; then
	       testname="CONFTEST"
	       pac_cv_f90_module_case="upper"
	   fi
       fi
       if test -z "$pac_MOD" ; then 
	   pac_cv_f90_module_ext="unknown"
       else
	   pac_cv_f90_module_ext=$pac_MOD
       fi
    fi
else
    echo "configure: failed program was:" >&AC_FD_CC
    cat conftest.$ac_f90ext >&AC_FD_CC
    pac_cv_f90_module_ext="unknown"
fi
rm -f conftest*
# The intel compiler sometimes generates these work.pc and .pcl files
rm -f work.pc work.pcl
])
if test -s work.pcl ; then
    AC_MSG_WARN([Compiler generates auxillery files (work.pcl)!])
fi
AC_SUBST(F90MODEXT)
if test "$pac_cv_f90_module_ext" = "unknown" ; then
    ifelse($2,,:,[$2])
else
    ifelse($1,,F90MODEXT=$pac_MOD,[$1])
fi
])
dnl
dnl PAC_F90_MODULE_INCFLAG - Determine the compiler flags for specifying 
dnl directories that contain compiled Fortran 90 modules
dnl
AC_DEFUN(PAC_F90_MODULE_INCFLAG,[
AC_CACHE_CHECK([for Fortran 90 module include flag],
pac_cv_f90_module_incflag,[
AC_REQUIRE([PAC_F90_MODULE_EXT])
#
# Note that the name of the file and the name of the module must be
# the same (some compilers use the module, some the file name)
rm -f work.pc work.pcl conftest.$ac_f90ext
cat >conftest.$ac_f90ext <<EOF
        module conftest
        integer n
        parameter (n=1)
        end module conftest
EOF
pac_madedir="no"
if test ! -d conftestdir ; then mkdir conftestdir ; pac_madedir="yes"; fi
if test "$pac_cv_f90_module_case" = "upper" ; then
    pac_module="CONFTEST.$pac_cv_f90_module_ext"
else
    pac_module="conftest.$pac_cv_f90_module_ext"
fi
if AC_TRY_EVAL(ac_f90compile) ; then
    if test -s "$pac_module" ; then
        mv $pac_module conftestdir
	# Remove any temporary files, and hide the work.pc file (if
	# the compiler generates them)
	if test -f work.pc ; then 
	    mv -f work.pc conftest.pc
        fi
	rm -f work.pcl
    else
	AC_MSG_WARN([Unable to build a simple F90 module])
        echo "configure: failed program was:" >&AC_FD_CC
        cat conftest.$ac_f90ext >&AC_FD_CC
    fi
else
    echo "configure: failed program was:" >&AC_FD_CC
    cat conftest.$ac_f90ext >&AC_FD_CC
fi
rm -f conftest.$ac_f90ext
cat >conftest.$ac_f90ext <<EOF
       program main
       use conftest
       end
EOF
pac_cv_f90_module_incflag="unknown"
if ${F90-f90} -c $F90FLAGS -Iconftestdir conftest.$ac_f90ext 1>&AC_FD_CC 2>&1 && \
	test -s conftest.o ; then
    pac_cv_f90_module_incflag="-I"
elif ${F90-f90} -c $F90FLAGS -Mconftestdir conftest.$ac_f90ext 1>&AC_FD_CC 2>&1 && \
	test -s conftest.o ; then
    pac_cv_f90_module_incflag="-M"
elif ${F90-f90} -c $F90FLAGS -pconftestdir conftest.$ac_f90ext 1>&AC_FD_CC 2>&1 && \
	test -s conftest.o ; then
    pac_cv_f90_module_incflag="-p"
elif test -s work.pc ; then 
     mv conftest.pc conftestdir/mpimod.pc
     echo "mpimod.pc" > conftestdir/mpimod.pcl
     echo "`pwd`/conftestdir/mpimod.pc" >> conftestdir/mpimod.pcl
     if ${F90-f90} -c $F90FLAGS -cl,conftestdir/mpimod.pcl conftest.$ac_f90ext 1>&AC_FD_CC 2>&1 && test -s conftest.o ; then
         pac_cv_f90_module_incflag='-cl,'
	# Not quite right; see the comments that follow
         AC_MSG_RESULT([-cl,filename where filename contains a list of files and directories])
	 F90_WORK_FILES_ARG="-cl,mpimod.pcl"
         F90MODINCSPEC="-cl,<dir>/<file>mod.pcl"
	 AC_SUBST(F90_WORK_FILES_ARG)
     else 
         # The version of the Intel compiler that I have refuses to let
	 # you put the "work catalog" list anywhere but the current directory.
         pac_cv_f90_module_incflag="Unavailable!"
     fi
else
    # Early versions of the Intel ifc compiler required a *file*
    # containing the names of files that contained the names of the
    # 
    # -cl,filename.pcl
    #   filename.pcl contains
    #     fullpathname.pc
    # The "fullpathname.pc" is generated, I believe, when a module is 
    # compiled.  
    # Intel compilers use a wierd system: -cl,filename.pcl .  If no file is
    # specified, work.pcl and work.pc are created.  However, if you specify
    # a file, it must contain a the name of a file ending in .pc .  Ugh!
    pac_cv_f90_module_incflag="unknown"
fi
if test "$pac_madedir" = "yes" ; then rm -rf conftestdir ; fi
rm -f conftest*
# The intel compiler sometimes generates these work.pc and .pcl files
rm -f work.pc work.pcl
])
AC_SUBST(F90MODINCFLAG)
F90MODINCFLAG=$pac_cv_f90_module_incflag
])
AC_DEFUN(PAC_F90_MODULE,[
PAC_F90_MODULE_EXT
PAC_F90_MODULE_INCFLAG
])
AC_DEFUN(PAC_F90_EXT,[
AC_CACHE_CHECK([whether Fortran 90 accepts f90 suffix],
pac_cv_f90_ext_f90,[
save_ac_f90ext=$ac_f90ext
ac_f90ext="f90"
PAC_TRY_F90_COMPILE(,,pac_cv_f90_ext_f90="yes",pac_cv_f90_ext_f90="no")
ac_f90ext=$save_ac_f90ext
])
])
dnl
dnl/*D 
dnl PAC_PROG_F90_INT_KIND - Determine kind parameter for an integer with
dnl the specified number of bytes.
dnl
dnl Synopsis:
dnl  PAC_PROG_F90_INT_KIND(variable-to-set,number-of-bytes,[cross-size])
dnl
dnl D*/
AC_DEFUN(PAC_PROG_F90_INT_KIND,[
# Set the default
$1=-1
if test "$pac_cv_prog_f90_cross" = "yes" ; then
    $1="$3"
else
if test -n "$ac_f90compile" ; then
    AC_MSG_CHECKING([for Fortran 90 integer kind for $2-byte integers])
    # Convert bytes to digits
    case $2 in 
	1) sellen=2 ;;
	2) sellen=4 ;;
	4) sellen=8 ;;
	8) sellen=16 ;;
       16) sellen=30 ;;
	*) sellen=8 ;;
    esac
    # Check for cached value
    eval testval=\$"pac_cv_prog_f90_int_kind_$sellen"
    if test -n "$testval" ; then 
        AC_MSG_RESULT([$testval (cached)])
	$1=$testval
    else
        # must compute
        rm -f conftest*
        cat <<EOF > conftest.$ac_f90ext
      program main
      integer i
      i = selected_int_kind($sellen)
      open(8, file="conftest1.out", form="formatted")
      write (8,*) i
      close(8)
      stop
      end
EOF
        KINDVAL="unavailable"
        eval "pac_cv_prog_f90_int_kind_$sellen"=-1
        if AC_TRY_EVAL(ac_f90link) && test -s conftest ; then
            ./conftest 1>&AC_FD_CC 2>&1
            if test -s conftest1.out ; then
	        # Because of write, there may be a leading blank.
                KINDVAL=`cat conftest1.out | sed 's/ //g'`
 	        eval "pac_cv_prog_f90_int_kind_$sellen"=$KINDVAL
	        $1=$KINDVAL
            fi
        fi
        rm -f conftest*
	# The intel compiler sometimes generates these work.pc and .pcl files
	rm -f work.pc work.pcl
	AC_MSG_RESULT($KINDVAL)
    fi # not cached
fi # Has Fortran 90
fi # is not cross compiling
])dnl
dnl
dnl
dnl Note: This checks for f95 before f90, since F95 is the more recent
dnl revision of Fortran 90.  efc is the Intel Fortran 77/90/95 compiler
dnl The compilers are:
dnl xlf90 - IBM 
dnl pgf90 - Portland group
dnl f90/f95 - Miscellaneous compilers, including NAG, Solaris, IRIX
# It is believed that under HP-UX `fort77' is the name of the native
# compiler.  On some Cray systems, fort77 is a native compiler.
# frt is the Fujitsu F77 compiler.
# pgf77 and pgf90 are the Portland Group F77 and F90 compilers.
# xlf/xlf90/xlf95 are IBM (AIX) F77/F90/F95 compilers.
# lf95 is the Lahey-Fujitsu compiler.
# fl32 is the Microsoft Fortran "PowerStation" compiler.
# af77 is the Apogee F77 compiler for Intergraph hardware running CLIX.
# epcf90 is the "Edinburgh Portable Compiler" F90.
# fort is the Compaq Fortran 90 (now 95) compiler for Tru64 and Linux/Alpha.
# pathf90 is the Pathscale Fortran 90 compiler
# ifort is another name for the Inten f90 compiler
# efc - An older Intel compiler (?)
# ifc - An older Intel compiler
AC_DEFUN(PAC_PROG_F90,[
if test -z "$F90" ; then
    AC_CHECK_PROGS(F90,f90 xlf90 pgf90 ifort epcf90 f95 fort xlf95 lf95 pathf90 g95 gfortran ifc efc)
    test -z "$F90" && AC_MSG_WARN([no acceptable Fortran 90 compiler found in \$PATH])
fi
if test -n "$F90" ; then
     PAC_PROG_F90_WORKS
fi
dnl Cache these so we don't need to change in and out of f90 mode
ac_f90ext=$pac_cv_f90_ext
ac_f90compile='${F90-f90} -c $F90FLAGS conftest.$ac_f90ext 1>&AC_FD_CC'
ac_f90link='${F90-f90} -o conftest${ac_exeext} $F90FLAGS $LDFLAGS conftest.$ac_f90ext $LIBS 1>&AC_FD_CC'
# Check for problems with Intel efc compiler, if the compiler works
if test "$pac_cv_prog_f90_works" = yes ; then
    cat > conftest.$ac_f90ext <<EOF
        program main
        end
EOF
    pac_msg=`$F90 -o conftest $F90FLAGS $LDFLAGS conftest.$ac_f90ext $LIBS 2>&1 | grep 'bfd assertion fail'`
    if test -n "$pac_msg" ; then
        pac_msg=`$F90 -o conftest $F90FLAGS $LDFLAGS conftest.$ac_f90ext -i_dynamic $LIBS 2>&1 | grep 'bfd assertion fail'`
        if test -z "$pac_msg" ; then LDFLAGS="-i_dynamic" ; fi
        # There should really be f90linker flags rather than generic ldflags.
    fi
fi
])
dnl Internal routine for testing F90
dnl PAC_PROG_F90_WORKS()
AC_DEFUN(PAC_PROG_F90_WORKS,
[AC_MSG_CHECKING([for extension for Fortran 90 programs])
pac_cv_f90_ext="f90"
cat > conftest.$pac_cv_f90_ext <<EOF
      program conftest
      end
EOF
ac_compile='${F90-f90} -c $F90FLAGS conftest.$pac_cv_f90_ext 1>&AC_FD_CC'
if AC_TRY_EVAL(ac_compile) ; then
    AC_MSG_RESULT([f90])
else
    rm -f conftest*
    pac_cv_f90_ext="f"
    cat > conftest.$pac_cv_f90_ext <<EOF
      program conftest
      end
EOF
    if AC_TRY_EVAL(ac_compile) ; then
	AC_MSG_RESULT([f])
    else
        AC_MSG_RESULT([unknown!])
    fi
fi
AC_MSG_CHECKING([whether the Fortran 90 compiler ($F90 $F90FLAGS $LDFLAGS) works])
AC_LANG_SAVE
# We cannot use _LANG_FORTRAN90 here because we will usually be executing this
# test in the context of _PROG_F90, which is a require on _LANG_FORTRAN90.
# Instead, we insert the necessary code from _LANG_FORTRAN90 here
dnl PAC_LANG_FORTRAN90
dnl define(ifdef([_AC_LANG],[_AC_LANG],[AC_LANG]), [FORTRAN90])dnl
define([AC_LANG], [FORTRAN90])dnl
ac_ext=$pac_cv_f90_ext
ac_compile='${F90-f90} -c $F90FLAGS conftest.$ac_ext 1>&AC_FD_CC'
ac_link='${F90-f90} -o conftest${ac_exeext} $F90FLAGS $LDFLAGS conftest.$ac_ext $LIBS 1>&AC_FD_CC'
dnl cross_compiling no longer maintained by autoconf as part of the
dnl AC_LANG changes.  If we set it here, a later AC_LANG may not 
dnl restore it (in the case where one compiler claims to be a cross compiler
dnl and another does not)
dnl cross_compiling=$pac_cv_prog_f90_cross
# Include a Fortran 90 construction to distinguish between Fortran 77 
# and Fortran 90 compilers.
cat >conftest.$ac_ext <<EOF
      program conftest
      integer, dimension(10) :: n
      end
EOF
if AC_TRY_EVAL(ac_link) && test -s conftest${ac_exeect} ; then
    pac_cv_prog_f90_works="yes"
    if (./conftest; exit) 2>/dev/null ; then
        pac_cv_prog_f90_cross="no"
    else
        pac_cv_prog_f90_cross="yes"
    fi
else
  echo "configure: failed program was:" >&AC_FD_CC
  cat conftest.$ac_ext >&AC_FD_CC
  pac_cv_prog_f90_works="no"
fi
rm -f conftest*
# The intel compiler sometimes generates these work.pc and .pcl files
rm -f work.pc work.pcl
AC_LANG_RESTORE
AC_MSG_RESULT($pac_cv_prog_f90_works)
if test $pac_cv_prog_f90_works = no; then
  AC_MSG_WARN([installation or configuration problem: Fortran 90 compiler cannot create executables.])
fi
AC_MSG_CHECKING([whether the Fortran 90 compiler ($F90 $F90FLAGS $LDFLAGS) is a cross-compiler])
AC_MSG_RESULT($pac_cv_prog_f90_cross)
dnl cross_compiling no longer maintained by autoconf as part of the
dnl AC_LANG changes.  If we set it here, a later AC_LANG may not 
dnl restore it (in the case where one compiler claims to be a cross compiler
dnl and another does not)
dnl cross_compiling=$pac_cv_prog_f90_cross
])
dnl
dnl This version uses a Fortran program to link programs.
dnl This is necessary because some compilers provide shared libraries
dnl that are not within the default linker paths (e.g., our installation
dnl of the Portland Group compilers).
dnl We assume a naming convention consistent with the Fortran 77 one.
dnl
AC_DEFUN(PAC_PROG_F90_CHECK_SIZEOF,[
changequote(<<,>>)dnl
dnl The name to #define.
dnl If the arg value contains a variable, we need to update that
define(<<PAC_TYPE_NAME>>, translit(sizeof_f90_$1, [a-z *], [A-Z__]))dnl
dnl The cache variable name.
define(<<PAC_CV_NAME>>, translit(pac_cv_f90_sizeof_$1, [ *], [__]))dnl
changequote([,])dnl
AC_CACHE_CHECK([for size of Fortran type $1],PAC_CV_NAME,[
AC_REQUIRE([PAC_PROG_F77_NAME_MANGLE])
if test "$cross_compiling" = yes ; then
    ifelse([$2],,[AC_MSG_WARN([No value provided for size of $1 when cross-compiling])]
,eval PAC_CV_NAME=$2)
else
    rm -f conftest*
    cat <<EOF > conftestc.c
#include <stdio.h>
#include "confdefs.h"
#ifdef F77_NAME_UPPER
#define cisize_ CISIZE
#define isize_ ISIZE
#elif defined(F77_NAME_LOWER) || defined(F77_NAME_MIXED)
#define cisize_ cisize
#define isize_ isize
#endif
int cisize_(char *,char*);
int cisize_(char *i1p, char *i2p)
{ 
    int isize_val=0;
    FILE *f = fopen("conftestval", "w");
    if (!f) return 1;
    isize_val = (int)(i2p - i1p);
    fprintf(f,"%d\n", isize_val );
    fclose(f);
    return 0;
}
EOF
    pac_tmp_compile='$CC -c $CFLAGS $CPPFLAGS conftestc.c >&5'
    if AC_TRY_EVAL(pac_tmp_compile) && test -s conftestc.o ; then
        AC_LANG_SAVE
        AC_LANG_FORTRAN77
        saveLIBS=$LIBS
        LIBS="conftestc.o $LIBS"
        dnl TRY_RUN does not work correctly for autoconf 2.13 (the
        dnl macro includes C-preprocessor directives that are not 
        dnl valid in Fortran.  Instead, we do this by hand
        cat >conftest.f <<EOF
         program main
         $1 a(2)
         integer irc
         irc = cisize(a(1),a(2))
         end
EOF
        rm -f conftest$ac_exeext
        rm -f conftestval
        if AC_TRY_EVAL(ac_link) && test -s conftest$ac_exeext ; then
	    if ./conftest$ac_exeext ; then
	        # success
                :
            else
	        # failure 
                :
	    fi
        else
	    # failure
            AC_MSG_WARN([Unable to build program to determine size of $1])
        fi
        AC_LANG_RESTORE
        if test -s conftestval ; then
            eval PAC_CV_NAME=`cat conftestval`
        else
	    eval PAC_CV_NAME=0
        fi
        rm -f conftest*
	# The intel compiler sometimes generates these work.pc and .pcl files
	rm -f work.pc work.pcl
        LIBS=$saveLIBS
    else
        AC_MSG_WARN([Unable to compile the C routine for finding the size of a $1])
    fi
fi # cross-compiling
])
AC_DEFINE_UNQUOTED(PAC_TYPE_NAME,$PAC_CV_NAME,[Define size of PAC_TYPE_NAME])
undefine([PAC_TYPE_NAME])
undefine([PAC_CV_NAME])
])
dnl
dnl PAC_F90_AND_F77_COMPATIBLE([action-if-true],[action-if-false])
dnl
AC_DEFUN([PAC_F90_AND_F77_COMPATIBLE],[
AC_REQUIRE([PAC_PROG_F90_WORKS])
AC_CACHE_CHECK([whether Fortran 90 works with Fortran 77],
pac_cv_f90_and_f77,[
pac_cv_f90_and_f77="unknown"
rm -f conftest*
if test -z "$ac_ext_f90" ; then ac_ext_f90=$pac_cv_f90_ext ; fi
# Define the two language-specific steps
link_f90='${F90-f90} -o conftest${ac_exeext} $F90FLAGS $LDFLAGS conftest1.$ac_ext_f90 conftest2.o $LIBS 1>&AC_FD_CC'
compile_f77='${F77-f77} -c $FFLAGS conftest2.f 1>&AC_FD_CC'
# Create test programs
cat > conftest1.$ac_ext_f90 <<EOF
       program main
       integer a
       a = 1
       call t1_2(a)
       end
EOF
cat > conftest2.f <<EOF
       subroutine t1_2(b)
       integer b
       b = b + 1
       end
EOF
# compile the f77 program and link with the f90 program
# The reverse may not work because the Fortran 90 environment may
# expect to be in control (and to provide library files unknown to any other
# environment, even Fortran 77!)
if AC_TRY_EVAL(compile_f77) ; then
    if AC_TRY_EVAL(link_f90) && test -x conftest ; then
        pac_cv_f90_and_f77="yes"
    else 
        pac_cv_f90_and_f77="no"
    fi
    # Some versions of the Intel compiler produce these two files
    rm -f work.pc work.pcl
else
    pac_cv_f90_and_f77="yes"
fi])
# Perform the requested action based on whether the test succeeded
if test "$pac_cv_f90_and_f77" = yes ; then
    ifelse($1,,:,[$1])
else
    ifelse($2,,:,[$2])
fi
])
