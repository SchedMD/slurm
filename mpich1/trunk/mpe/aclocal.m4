dnl
dnl record top-level directory (this one)
dnl A problem.  Some systems use an NFS automounter.  This can generate
dnl paths of the form /tmp_mnt/... . On SOME systems, that path is
dnl not recognized, and you need to strip off the /tmp_mnt. On others, 
dnl it IS recognized, so you need to leave it in.  Grumble.
dnl The real problem is that OTHER nodes on the same NFS system may not
dnl be able to find a directory based on a /tmp_mnt/... name.
dnl
dnl It is WRONG to use $PWD, since that is maintained only by the C shell,
dnl and if we use it, we may find the 'wrong' directory.  To test this, we
dnl try writing a file to the directory and then looking for it in the 
dnl current directory.  Life would be so much easier if the NFS automounter
dnl worked correctly.
dnl
dnl PAC_GETWD(varname [, filename ] )
dnl 
dnl Set varname to current directory.  Use filename (relative to current
dnl directory) if provided to double check.
dnl
dnl Need a way to use "automounter fix" for this.
dnl
define(PAC_GETWD,[
AC_MSG_CHECKING(for current directory name)
$1=$PWD
if test "${$1}" != "" -a -d "${$1}" ; then 
    if test -r ${$1}/.foo$$ ; then
        rm -f ${$1}/.foo$$
	rm -f .foo$$
    fi
    if test -r ${$1}/.foo$$ -o -r .foo$$ ; then
	$1=
    else
	echo "test" > ${$1}/.foo$$
	if test ! -r .foo$$ ; then
            rm -f ${$1}/.foo$$
	    $1=
        else
 	    rm -f ${$1}/.foo$$
	fi
    fi
fi
if test "${$1}" = "" ; then
    $1=`pwd | sed -e 's%/tmp_mnt/%/%g'`
fi
dnl
dnl First, test the PWD is sensible
ifelse($2,,,
if test ! -r ${$1}/$2 ; then
    dnl PWD must be messed up
    $1=`pwd`
    if test ! -r ${$1}/$2 ; then
	print_error "Cannot determine the root directory!" 
        exit 1
    fi
    $1=`pwd | sed -e 's%/tmp_mnt/%/%g'`
    if test ! -d ${$1} ; then 
        print_error "Warning: your default path uses the automounter; this may"
        print_error "cause some problems if you use other NFS-connected systems."
        $1=`pwd`
    fi
fi)
if test -z "${$1}" ; then
    $1=`pwd | sed -e 's%/tmp_mnt/%/%g'`
    if test ! -d ${$1} ; then 
        print_error "Warning: your default path uses the automounter; this may"
        print_error "cause some problems if you use other NFS-connected systems."
        $1=`pwd`
    fi
fi
AC_MSG_RESULT(${$1})
])dnl
dnl
dnl This version compiles an entire function; used to check for
dnl things like varargs done correctly
dnl
dnl PAC_COMPILE_CHECK_FUNC(msg,function,if_true,if_false)
dnl
define(PAC_COMPILE_CHECK_FUNC,
[AC_PROVIDE([$0])dnl
ifelse([$1], , , [AC_MSG_CHECKING(for $1)]
)dnl
if test ! -f confdefs.h ; then
    AC_MSG_RESULT("!! SHELL ERROR !!")
    echo "The file confdefs.h created by configure has been removed"
    echo "This may be a problem with your shell; some versions of LINUX"
    echo "have this problem.  See the Installation guide for more"
    echo "information."
    exit 1
fi
cat > conftest.c <<EOF
#include "confdefs.h"
[$2]
EOF
dnl Don't try to run the program, which would prevent cross-configuring.
if { (eval echo configure:__oline__: \"$ac_compile\") 1>&5; (eval $ac_compile) 2>&5; }; then
  ifelse([$1], , , [AC_MSG_RESULT(yes)])
  ifelse([$3], , :, [rm -rf conftest*
  $3
])
ifelse([$4], , , [else
  rm -rf conftest*
  $4
])dnl
   ifelse([$1], , , ifelse([$4], ,else) [AC_MSG_RESULT(no)])
fi
rm -f conftest*]
)dnl
dnl
dnl PAC_OUTPUT_EXEC(files[,mode]) - takes files (as shell script or others),
dnl and applies configure to the them.  Basically, this is what AC_OUTPUT
dnl should do, but without adding a comment line at the top.
dnl Must be used ONLY after AC_OUTPUT (it needs config.status, which 
dnl AC_OUTPUT creates).
dnl Optionally, set the mode (+x, a+x, etc)
dnl
define(PAC_OUTPUT_EXEC,[
CONFIG_FILES="$1"
export CONFIG_FILES
./config.status
CONFIG_FILES=""
for pac_file in $1 ; do 
    rm -f .pactmp
    sed -e '1d' $pac_file > .pactmp
    rm -f $pac_file
    mv .pactmp $pac_file
    ifelse($2,,,chmod $2 $pac_file)
done
])dnl
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
dnlD*/
AC_DEFUN(PAC_PROG_CC,[
AC_PROVIDE([AC_PROG_CC])
AC_CHECK_PROGS(CC, cc xlC xlc pgcc icc gcc )
test -z "$CC" && AC_MSG_ERROR([no acceptable cc found in \$PATH])
PAC_PROG_CC_WORKS
AC_PROG_CC_GNU
if test $ac_cv_prog_gcc = yes; then
  GCC=yes
else
  GCC=
fi
])
dnl
#
# This is a replacement that checks that FAILURES are signaled as well
# (later configure macros look for the .o file, not just success from the
# compiler, but they should not HAVE to
#
dnl --- insert 2.52 compatibility here ---
dnl 2.52+ does not have AC_PROG_CC_GNU
ifdef([AC_PROG_CC_GNU],,[AC_DEFUN([AC_PROG_CC_GNU],)])
dnl 2.52 does not have AC_PROG_CC_WORKS
ifdef([AC_PROG_CC_WORKS],,[AC_DEFUN([AC_PROG_CC_WORKS],)])
dnl
dnl
AC_DEFUN(PAC_PROG_CC_WORKS,
[AC_PROG_CC_WORKS
AC_MSG_CHECKING([whether the C compiler sets its return status correctly])
AC_LANG_SAVE
AC_LANG_C
AC_TRY_COMPILE(,[int a = bzzzt;],notbroken=no,notbroken=yes)
AC_MSG_RESULT($notbroken)
if test "$notbroken" = "no" ; then
    AC_MSG_ERROR([installation or configuration problem: C compiler does not
correctly set error code when a fatal error occurs])
fi
])
dnl
dnl ***TAKEN FROM sowing/confdb/aclocal_cc.m4 IF YOU FIX THIS, FIX THAT
dnl VERSION AS WELL
dnl Check whether we need -fno-common to correctly compile the source code.
dnl This is necessary if global variables are defined without values in
dnl gcc.  Here is the test
dnl conftest1.c:
dnl extern int a; int a;
dnl conftest2.c:
dnl extern int a; int main(int argc; char *argv[] ){ return a; }
dnl Make a library out of conftest1.c and try to link with it.
dnl If that fails, recompile it with -fno-common and see if that works.
dnl If so, add -fno-common to CFLAGS
dnl An alternative is to use, on some systems, ranlib -c to force 
dnl the system to find common symbols.
dnl
dnl NOT TESTED
AC_DEFUN(PAC_PROG_C_BROKEN_COMMON,[
AC_MSG_CHECKING([whether global variables handled properly])
AC_REQUIRE([AC_PROG_RANLIB])
ac_cv_prog_cc_globals_work=no
echo 'extern int a; int a;' > conftest1.c
echo 'extern int a; int main( ){ return a; }' > conftest2.c
if ${CC-cc} $CFLAGS -c conftest1.c >conftest.out 2>&1 ; then
    if ${AR-ar} cr libconftest.a conftest1.o >/dev/null 2>&1 ; then
        if ${RANLIB-:} libconftest.a >/dev/null 2>&1 ; then
            if ${CC-cc} $CFLAGS -o conftest conftest2.c $LDFLAGS libconftest.a >> conftest.out 2>&1 ; then
		# Success!  C works
		ac_cv_prog_cc_globals_work=yes
	    else
	        # Failure!  Do we need -fno-common?
	        ${CC-cc} $CFLAGS -fno-common -c conftest1.c >> conftest.out 2>&1
		rm -f libconftest.a
		${AR-ar} cr libconftest.a conftest1.o >>conftest.out 2>&1
	        ${RANLIB-:} libconftest.a >>conftest.out 2>&1
	        if ${CC-cc} $CFLAGS -o conftest conftest2.c $LDFLAGS libconftest.a >> conftest.out 2>&1 ; then
		    ac_cv_prog_cc_globals_work="needs -fno-common"
		    CFLAGS="$CFLAGS -fno-common"
		fi
	    fi
        fi
    fi
fi
rm -f conftest* libconftest*
AC_MSG_RESULT($ac_cv_prog_cc_globals_work)
])
dnl
dnl
dnl Include Make related definitions
builtin(include,aclocal_make.m4)
dnl
dnl
dnl Include F77 related definitions
builtin(include,aclocal_f77.m4)
dnl
dnl
dnl Include MPI related definitions
builtin(include,aclocal_mpi.m4)
