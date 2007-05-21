dnl
dnl Fixes to bugs in AC_xxx macros
dnl 
dnl (AC_TRY_COMPILE is missing a newline after the end in the Fortran
dnl branch; that has been fixed in-place)
dnl
dnl (AC_PROG_CC makes many dubious assumptions.  One is that -O is safe
dnl with -g, even with gcc.  This isn't true; gcc will eliminate dead code
dnl when -O is used, even if you added code explicitly for debugging 
dnl purposes.  -O shouldn't do dead code elimination when -g is selected, 
dnl unless a specific option is selected.  Unfortunately, there is no
dnl documented option to turn off dead code elimination.
dnl
dnl
dnl (AC_CHECK_HEADER and AC_CHECK_HEADERS both make the erroneous assumption
dnl that the C-preprocessor and the C (or C++) compilers are the same program
dnl and have the same search paths.  In addition, CHECK_HEADER looks for 
dnl error messages to decide that the file is not available; unfortunately,
dnl it also interprets messages such as "evaluation copy" and warning messages
dnl from broken CPP programs (such as IBM's xlc -E, which often warns about 
dnl "lm not a valid option").  Instead, we try a compilation step with the 
dnl C compiler.
dnl
dnl AC_CONFIG_AUX_DIRS only checks for install-sh, but assumes other
dnl values are present.  Also doesn't provide a way to override the
dnl sources of the various configure scripts.  This replacement
dnl version of AC_CONFIG_AUX_DIRS overcomes this.
dnl Internal subroutine.
dnl Search for the configuration auxiliary files in directory list $1.
dnl We look only for install-sh, so users of AC_PROG_INSTALL
dnl do not automatically need to distribute the other auxiliary files.
dnl AC_CONFIG_AUX_DIRS(DIR ...)
dnl Also note that since AC_CONFIG_AUX_DIR_DEFAULT calls this, there
dnl isn't a easy way to fix it other than replacing it completely.
dnl This fix applies to 2.13
dnl/*D
dnl AC_CONFIG_AUX_DIRS - Find the directory containing auxillery scripts
dnl for configure
dnl
dnl Synopsis:
dnl AC_CONFIG_AUX_DIRS( [ directories to search ] )
dnl
dnl Output Effect:
dnl Sets 'ac_config_guess' to location of 'config.guess', 'ac_config_sub'
dnl to location of 'config.sub', 'ac_install_sh' to the location of
dnl 'install-sh' or 'install.sh', and 'ac_configure' to the location of a
dnl Cygnus-style 'configure'.  Only 'install-sh' is guaranteed to exist,
dnl since the other scripts are needed only by some special macros.
dnl
dnl The environment variable 'CONFIG_AUX_DIR', if set, overrides the
dnl directories listed.  This is an extension to the 'autoconf' version of
dnl this macro. 
dnl D*/
undefine([AC_CONFIG_AUX_DIRS])
AC_DEFUN(AC_CONFIG_AUX_DIRS,
[if test -f $CONFIG_AUX_DIR/install-sh ; then ac_aux_dir=$CONFIG_AUX_DIR 
else
ac_aux_dir=
# We force the test to use the absolute path to ensure that the install
# program can be used if we cd to a different directory before using
# install.
for ac_dir in $1; do
  if test -f $ac_dir/install-sh; then
    ac_aux_dir=$ac_dir
    abs_ac_aux_dir=`(cd $ac_aux_dir && pwd)`
    ac_install_sh="$abs_ac_aux_dir/install-sh -c"
    break
  elif test -f $ac_dir/install.sh; then
    ac_aux_dir=$ac_dir
    abs_ac_aux_dir=`(cd $ac_aux_dir && pwd)`
    ac_install_sh="$abs_ac_aux_dir/install.sh -c"
    break
  fi
done
fi
if test -z "$ac_aux_dir"; then
  AC_MSG_ERROR([can not find install-sh or install.sh in $1])
fi
ac_config_guess=$ac_aux_dir/config.guess
ac_config_sub=$ac_aux_dir/config.sub
ac_configure=$ac_aux_dir/configure # This should be Cygnus configure.
AC_PROVIDE([AC_CONFIG_AUX_DIR_DEFAULT])dnl
])

dnl
dnl Added complication:  We want to check for a header EVEN IF WE CAN'T 
dnl COMPILE IT.  E.g., if the header requires another header, we want 
dnl this to still work.  To make this slightly more robust, if the
dnl CPP is the compiler, just use the CPP test.  Otherwise, fall
dnl back into the compilation test.  Since autoconf uses /lib/cpp
dnl only if it can't figure out how to use the compiler, we
dnl test for that specific cast, then use CPP if we can. 
dnl However, on failure, we still test to see if, even though CPP 
dnl failed, the compiler accepts it (this avoids some of the "evaluation
dnl copy" problems.
dnl 
undefine([AC_CHECK_HEADER])
AC_DEFUN(AC_CHECK_HEADER,
[dnl Do the transliteration at runtime so arg 1 can be a shell variable.
ac_safe=`echo "$1" | sed 'y%./+-%__p_%'`
AC_MSG_CHECKING([for $1])
AC_CACHE_VAL(ac_cv_header_$ac_safe,
[
pac_found_header=no
if test "$CPP" != "/lib/cpp" ; then
    AC_TRY_CPP([#include <$1>], pac_found_header=yes)
fi
# If cpp failed, see if the compiler accepts the header.
if test "$pac_found_header" != "yes" ; then
    cat >conftest.c<<EOF
[#]line __oline__ "configure"
#include "confdefs.h"
#include <$1>
int conftest() {return 0;}
EOF
    ac_compile_for_cpp='${CC-cc} -c $CFLAGS $CPPFLAGS conftest.c 1>&AC_FD_CC'
    if AC_TRY_EVAL(ac_compile_for_cpp); then
	pac_found_header=yes
    else
        echo "configure: failed program was:" >&AC_FD_CC
        cat conftest.c >&AC_FD_CC
    fi
    rm -f conftest*
fi
# Finally, set the ac variable.
if test "$pac_found_header" = "yes" ; then
    eval "ac_cv_header_$ac_safe=yes"
else
    eval "ac_cv_header_$ac_safe=no"
fi
])dnl
if eval "test \"`echo '$ac_cv_header_'$ac_safe`\" = yes"; then
  AC_MSG_RESULT(yes)
  ifelse([$2], , :, [$2])
else
  AC_MSG_RESULT(no)
ifelse([$3], , , [$3
])dnl
fi
])

dnl
dnl This internal macro fails to work properly with OTHER internal macros.
dnl Basically, if the prologue is [], then no message should be generated.
dnl This macro is in autoconf 2.52
m4_define([AC_LANG_PROGRAM(Fortran 77)],
[m4_if([$1],[[[]]],,[m4_ifval([$1],
       [m4_warn([syntax], [$0: ignoring PROLOGUE: $1])])])dnl
      program main
$2
      end])


dnl/*D
dnl PAC_PROG_CHECK_INSTALL_WORKS - Check whether the install program in INSTALL
dnl works.
dnl
dnl Synopsis:
dnl PAC_PROG_CHECK_INSTALL_WORKS
dnl
dnl Output Effect:
dnl   Sets the variable 'INSTALL' to the value of 'ac_sh_install' if 
dnl   a file cannot be installed into a local directory with the 'INSTALL'
dnl   program
dnl
dnl Notes:
dnl   The 'AC_PROG_INSTALL' scripts tries to avoid broken versions of 
dnl   install by avoiding directories such as '/usr/sbin' where some 
dnl   systems are known to have bad versions of 'install'.  Unfortunately, 
dnl   this is exactly the sort of test-on-name instead of test-on-capability
dnl   that 'autoconf' is meant to eliminate.  The test in this script
dnl   is very simple but has been adequate for working around problems 
dnl   on Solaris, where the '/usr/sbin/install' program (known by 
dnl   autoconf to be bad because it is in /usr/sbin) is also reached by a 
dnl   soft link through /bin, which autoconf believes is good.
dnl
dnl   No variables are cached to ensure that we do not make a mistake in 
dnl   our choice of install program.
dnl
dnl   The Solaris configure requires the directory name to immediately
dnl   follow the '-c' argument, rather than the more common 
dnl.vb
dnl      args sourcefiles destination-dir
dnl.ve
dnl D*/
AC_DEFUN([PAC_PROG_CHECK_INSTALL_WORKS],[
if test -z "$INSTALL" ; then
    AC_MSG_RESULT([No install program available])
else
    # Check that this install really works
    rm -f conftest
    echo "Test file" > conftest
    if test ! -d .conftest ; then mkdir .conftest ; fi
    AC_MSG_CHECKING([whether install works])
    if $INSTALL conftest .conftest >/dev/null 2>&1 ; then
        installOk=yes
    else
        installOk=no
    fi
    rm -rf .conftest conftest
    AC_MSG_RESULT($installOk)
    if test "$installOk" = no ; then
        if test -n "$ac_install_sh" ; then
            INSTALL=$ac_install_sh
        else
	    AC_MSG_ERROR([Unable to find working install])
        fi
    fi
fi
])
