dnl
dnl We need routines to check that make works.  Possible problems with
dnl make include
dnl
dnl It is really gnumake, and contrary to the documentation on gnumake,
dnl it insists on screaming everytime a directory is changed.  The fix
dnl is to add the argument --no-print-directory to the make
dnl
dnl It is really BSD 4.4 make, and can't handle 'include'.  For some
dnl systems, this can be fatal; there is no fix (other than removing this
dnl alleged make).
dnl
dnl It is the OSF V3 make, and can't handle a comment in a block of target
dnl code.  There is no acceptable fix.
dnl
dnl
dnl
dnl
dnl Find a make program if none is defined.
AC_DEFUN(PAC_PROG_MAKE_PROGRAM,[true
if test "X$MAKE" = "X" ; then
   AC_CHECK_PROGS(MAKE,make gnumake nmake pmake smake)
fi
])dnl
dnl/*D
dnl PAC_PROG_MAKE_ECHOS_DIR - Check whether make echos all directory changes
dnl
dnl Synopsis:
dnl PAC_PROG_MAKE_ECHOS_DIR
dnl
dnl Output Effect:
dnl  If make echos directory changes, append '--no-print-directory' to the 
dnl  symbol 'MAKE'.  If 'MAKE' is not set, chooses 'make' for 'MAKE'.
dnl
dnl  You can override this test (if, for example, you want make to be
dnl  more noisy) by setting the environment variable MAKE_MAY_PRINT_DIR to 
dnl  yes
dnl
dnl See also:
dnl PAC_PROG_MAKE
dnl D*/
dnl
AC_DEFUN(PAC_PROG_MAKE_ECHOS_DIR,[
if test "$MAKE_MAY_PRINT_DIR" != "yes" ; then
    AC_CACHE_CHECK([whether make echos directory changes],
pac_cv_prog_make_echos_dir,
[
AC_REQUIRE([PAC_PROG_MAKE_PROGRAM])
rm -f conftest
cat > conftest <<.
SHELL=/bin/sh
ALL:
	@(dir="`pwd`" ; cd .. ; \$(MAKE) -f "\$\$dir/conftest" SUB)
SUB:
	@echo "success"
.
str="`$MAKE -f conftest 2>&1`"
if test "$str" != "success" ; then
    str="`$MAKE --no-print-directory -f conftest 2>&1`"
    if test "$str" = "success" ; then
	pac_cv_prog_make_echos_dir="yes using --no-print-directory"
    else
	pac_cv_prog_make_echos_dir="no"
	echo "Unexpected output from make with program" >>config.log
	cat conftest >>config.log
	echo "str" >> config.log
    fi
else
    pac_cv_prog_make_echos_dir="no"
fi
rm -f conftest
str=""
])
    if test "$pac_cv_prog_make_echos_dir" = "yes using --no-print-directory" ; then
        MAKE="$MAKE --no-print-directory"
    fi
fi
])dnl
dnl
dnl/*D
dnl PAC_PROG_MAKE_INCLUDE - Check whether make supports include
dnl
dnl Synopsis:
dnl PAC_PROG_MAKE_INCLUDE([action if true],[action if false])
dnl
dnl Output Effect:
dnl   None
dnl
dnl Notes:
dnl  This checks for makes that do not support 'include filename'.  Some
dnl  versions of BSD 4.4 make required '#include' instead; some versions of
dnl  'pmake' have the same syntax.
dnl
dnl See Also:
dnl  PAC_PROG_MAKE
dnl
dnl D*/
dnl
AC_DEFUN(PAC_PROG_MAKE_INCLUDE,[
AC_CACHE_CHECK([whether make supports include],pac_cv_prog_make_include,[
AC_REQUIRE([PAC_PROG_MAKE_PROGRAM])
rm -f conftest
cat > conftest <<.
ALL:
	@echo "success"
.
cat > conftest1 <<.
include conftest
.
pac_str=`$MAKE -f conftest1 2>&1`
rm -f conftest conftest1
if test "$pac_str" != "success" ; then
    pac_cv_prog_make_include="no"
else
    pac_cv_prog_make_include="yes"
fi
])
if test "$pac_cv_prog_make_include" = "no" ; then
    ifelse([$2],,:,[$2])
else
    ifelse([$1],,:,[$1])
fi
])dnl
dnl
dnl/*D
dnl PAC_PROG_MAKE_ALLOWS_COMMENTS - Check whether comments are allowed in 
dnl   shell commands in a makefile
dnl
dnl Synopsis:
dnl PAC_PROG_MAKE_ALLOWS_COMMENTS([false text])
dnl
dnl Output Effect:
dnl Issues a warning message if comments are not allowed in a makefile.
dnl Executes the argument if one is given.
dnl
dnl Notes:
dnl Some versions of OSF V3 make do not all comments in action commands.
dnl
dnl See Also:
dnl  PAC_PROG_MAKE
dnl D*/
dnl
AC_DEFUN(PAC_PROG_MAKE_ALLOWS_COMMENTS,[
AC_CACHE_CHECK([whether make allows comments in actions],
pac_cv_prog_make_allows_comments,[
AC_REQUIRE([PAC_PROG_MAKE_PROGRAM])
rm -f conftest
cat > conftest <<.
SHELL=/bin/sh
ALL:
	@# This is a valid comment!
	@echo "success"
.
pac_str=`$MAKE -f conftest 2>&1`
rm -f conftest 
if test "$pac_str" != "success" ; then
    pac_cv_prog_make_allows_comments="no"
else
    pac_cv_prog_make_allows_comments="yes"
fi
])
if test "$pac_cv_prog_make_allows_comments" = "no" ; then
    AC_MSG_WARN([Your make does not allow comments in target code.
Using this make may cause problems when building programs.
You should consider using gnumake instead.])
    ifelse([$1],,[$1])
fi
])dnl
dnl
dnl/*D
dnl PAC_PROG_MAKE_VPATH - Check whether make supports source-code paths.
dnl
dnl Synopsis:
dnl PAC_PROG_MAKE_VPATH
dnl
dnl Output Effect:
dnl Sets the variable 'VPATH' to either
dnl.vb
dnl VPATH = .:${srcdir}
dnl.ve
dnl or
dnl.vb
dnl .PATH: . ${srcdir}
dnl.ve
dnl 
dnl Notes:
dnl The test checks that the path works with implicit targets (some makes
dnl support only explicit targets with 'VPATH' or 'PATH').
dnl
dnl NEED TO DO: Check that $< works on explicit targets.
dnl
dnl See Also:
dnl PAC_PROG_MAKE
dnl
dnl D*/
dnl
AC_DEFUN(PAC_PROG_MAKE_VPATH,[
AC_SUBST(VPATH)AM_IGNORE(VPATH)
AC_CACHE_CHECK([for virtual path format],
pac_cv_prog_make_vpath,[
AC_REQUIRE([PAC_PROG_MAKE_PROGRAM])
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
    pac_cv_prog_make_vpath="VPATH"
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
        pac_cv_prog_make_vpath=".PATH"
    else
	pac_cv_prog_make_vpath="neither VPATH nor .PATH works"
    fi
fi
rm -rf conftest*
])
if test "$pac_cv_prog_make_vpath" = "VPATH" ; then
    VPATH='VPATH=.:${srcdir}'
elif test "$pac_cv_prog_make_vpath" = ".PATH" ; then
    VPATH='.PATH: . ${srcdir}'
fi
])dnl
dnl
dnl/*D
dnl PAC_PROG_MAKE_SET_CFLAGS - Check whether make sets CFLAGS
dnl
dnl Synopsis:
dnl PAC_PROG_MAKE_SET_CFLAGS([action if true],[action if false])
dnl
dnl Output Effects:
dnl Executes the first argument if 'CFLAGS' is set by 'make'; executes
dnl the second argument if 'CFLAGS' is not set by 'make'.
dnl
dnl Notes:
dnl If 'CFLAGS' is set by make, you may wish to override that choice in your
dnl makefile.
dnl
dnl See Also:
dnl PAC_PROG_MAKE
dnl D*/
AC_DEFUN(PAC_PROG_MAKE_SET_CFLAGS,[
AC_CACHE_CHECK([whether make sets CFLAGS],
pac_cv_prog_make_set_cflags,[
AC_REQUIRE([PAC_PROG_MAKE_PROGRAM])
rm -f conftest
cat > conftest <<EOF
SHELL=/bin/sh
ALL:
	@echo X[\$]{CFLAGS}X
EOF
pac_str=`$MAKE -f conftest 2>&1`
rm -f conftest 
if test "$pac_str" = "XX" ; then
    pac_cv_prog_make_set_cflags="no"
else
    pac_cv_prog_make_set_cflags="yes"
fi
])
if test "$pac_cv_prog_make_set_cflags" = "no" ; then
    ifelse([$2],,:,[$2])
else
    ifelse([$1],,:,[$1])
fi
])dnl
dnl/*D
dnl PAC_PROG_MAKE_CLOCK_SKEW - Check whether there is a problem with 
dnl clock skew in suing make.
dnl
dnl Effect:
dnl Sets the cache variable 'pac_cv_prog_make_found_clock_skew' to yes or no
dnl D*/
AC_DEFUN(PAC_PROG_MAKE_CLOCK_SKEW,[
AC_CACHE_CHECK([whether clock skew breaks make],
pac_cv_prog_make_found_clock_skew,[
AC_REQUIRE([PAC_PROG_MAKE_PROGRAM])
rm -f conftest*
cat > conftest <<EOF
ALL:
	@-echo "success"
EOF
$MAKE -f conftest > conftest.out 2>&1
if grep -i skew conftest >/dev/null 2>&1 ; then
    pac_cv_prog_make_found_clock_skew=yes
else
    pac_cv_prog_make_found_clock_skew=no
fi
rm -f conftest*
])
dnl We should really do something if we detect clock skew.  The question is,
dnl what?
if test "$pac_cv_prog_make_found_clock_skew" = "yes" ; then
    AC_MSG_WARN([Clock skew found by make.  The configure and build may fail.
Consider building in a local instead of NFS filesystem.])
fi
])
dnl
dnl/*D
dnl PAC_PROG_MAKE_HAS_PATTERN_RULES - Determine if the make program supports
dnl pattern rules
dnl
dnl Synopsis:
dnl PAC_PROG_MAKE_HAS_PATTERN_RULES([action if true],[action if false])
dnl
dnl Output Effect:
dnl Executes the first argument if patterns of the form
dnl.vb
dnl   prefix%suffix: prefix%suffix
dnl.ve
dnl are supported by make (gnumake and Solaris make are known to support
dnl this form of target).  If patterns are not supported, executes the
dnl second argument.
dnl
dnl See Also:
dnl PAC_PROG_MAKE
dnl 
dnl D*/
AC_DEFUN(PAC_PROG_MAKE_HAS_PATTERN_RULES,[
AC_CACHE_CHECK([whether make has pattern rules],
pac_cv_prog_make_has_patterns,[
AC_REQUIRE([PAC_PROG_MAKE_PROGRAM])
rm -f conftest*
cat > conftestmm <<EOF
# Test for pattern rules
.SUFFIXES:
.SUFFIXES: .dep .c
conftest%.dep: %.c
	@cat \[$]< >\[$]@
EOF
date > conftest.c
if ${MAKE} -f conftestmm conftestconftest.dep 1>&AC_FD_CC 2>&1 </dev/null ; then
    pac_cv_prog_make_has_patterns="yes"
else
    pac_cv_prog_make_has_patterns="no"
fi
rm -f conftest*
])
if test "$pac_cv_prog_make_has_patterns" = "no" ; then
    ifelse([$2],,:,[$2])
else
    ifelse([$1],,:,[$1])
fi
])dnl
dnl
dnl/*D
dnl PAC_PROG_MAKE - Checks for the varieties of MAKE, including support for 
dnl VPATH
dnl
dnl Synopsis:
dnl PAC_PROG_MAKE
dnl
dnl Output Effect:
dnl Sets 'MAKE' to the make program to use if 'MAKE' is not already set.
dnl Sets the variable 'SET_CFLAGS' to 'CFLAGS =' if make sets 'CFLAGS'.
dnl
dnl Notes:
dnl This macro uses 'PAC_PROG_MAKE_ECHOS_DIR', 'PAC_PROG_MAKE_INCLUDE',
dnl 'PAC_PROG_MAKE_ALLOWS_COMMENTS', 'PAC_PROG_MAKE_VPATH', and
dnl 'PAC_PROG_MAKE_SET_CFLAGS'.  See those commands for details about their
dnl actions.
dnl 
dnl It may call 'AC_PROG_MAKE_SET', which sets 'SET_MAKE' to 'MAKE = @MAKE@'
dnl if the make program does not set the value of make, otherwise 'SET_MAKE'
dnl is set to empty; if the make program echos the directory name, then 
dnl 'SET_MAKE' is set to 'MAKE = $MAKE'.
dnl
dnl A recent change has been to remove the test on make echoing 
dnl directories.  This was done to make the build process behave more
dnl like other builds that do not work around this behavior in gnumake.
dnl D*/
dnl
AC_DEFUN(PAC_PROG_MAKE,[
PAC_PROG_MAKE_PROGRAM
PAC_PROG_MAKE_CLOCK_SKEW
dnl PAC_PROG_MAKE_ECHOS_DIR
PAC_PROG_MAKE_INCLUDE
PAC_PROG_MAKE_ALLOWS_COMMENTS
PAC_PROG_MAKE_VPATH
dnl 
dnl We're not using patterns any more, and Compaq/DEC OSF-1 sometimes hangs
dnl at this test
dnl PAC_PROG_MAKE_HAS_PATTERN_RULES
AC_SUBST(SET_CFLAGS)AM_IGNORE(SET_CFLAGS)
PAC_PROG_MAKE_SET_CFLAGS([SET_CFLAGS='CFLAGS='])
if test "$pac_cv_prog_make_echos_dir" = "no" ; then
    AC_PROG_MAKE_SET
else
    SET_MAKE="MAKE=${MAKE-make}"
fi
])
