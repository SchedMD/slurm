dnl
dnl To Have Kerberos for the purposes of the server, we need the 
dnl programming interface as well as the /usr/kerberos directories.
dnl 
dnl Just having Kerberos directories doesn't mean you can build applications
dnl with it....
AC_DEFUN(AC_CHECK_KERBEROS,
[AC_MSG_CHECKING(for Kerberos (/usr/kerberos))
    used_cache=yes
    AC_CACHE_VAL(ac_cv_sys_kerberos, [dnl
        ac_cv_sys_kerberos="no"
        used_cache=no
	if test -d /usr/kerberos ; then
	    AC_MSG_RESULT(yes)
	    AC_CHECK_FUNC(ka_UserAuthenticateGeneral,ac_cv_sys_kerberos="yes")
	fi
    ])
    if test "$ac_cv_sys_kerberos" = "yes"; then
	AC_DEFINE(HAVE_KERBEROS)
    fi
    if test "$used_cache" = "yes" ; then
        AC_MSG_RESULT($ac_cv_sys_kerberos)
    fi
])

dnl
dnl Just having AFS directories doesn't mean that you can use 
dnl AFS headers.
AC_DEFUN(AC_CHECK_AFS,
[AC_MSG_CHECKING(for AFS (/usr/afsws))
    AC_CACHE_VAL(ac_cv_sys_afs, [dnl
	if test -d /usr/afsws ; then
	    afs_avail=1
	    AC_CHECK_HEADER(afs/kauth.h,,afs_avail=0)
	    AC_CHECK_HEADER(afs/kautils.h,,afs_avail=0)
	    AC_CHECK_HEADER(afs/auth.h,,afs_avail=0)
	    if test $afs_avail = 1 ; then
  	        ac_cv_sys_afs="yes"
	    else
		ac_cv_sys_afs="no"
	    fi
	else
	    ac_cv_sys_afs="no"
	fi
    ])
    if test "$ac_cv_sys_afs" = "yes"; then
	AC_DEFINE(HAVE_AFS)
    fi
    AC_MSG_RESULT($ac_cv_sys_afs)
])

dnl
dnl It is NEVER a good idea to use the configure cache!
dnl (This is really intended for builds on the SAME platform with different
dnl options)
AC_DEFUN(AC_CHECK_SSL,
[AC_MSG_CHECKING(for SSL in cache)
    found_ssl_in_cache="yes"
    AC_CACHE_VAL(ac_cv_sys_ssl, [dnl
	AC_MSG_RESULT(no. checking manually)
	found_ssl_in_cache="no"
    ])
    if test "$found_ssl_in_cache" = "no"; then
	AC_FIND_USER_INCLUDE(ssl,/usr/local/openssl, ac_cv_sys_ssl="yes", ac_cv_sys_ssl="no")
    else
	AC_MSG_RESULT($ac_cv_sys_ssl)
    fi
    if test "$ac_cv_sys_ssl" = "yes"; then
        AC_FIND_USER_INCLUDE(ssllib,/usr/local/openssl,ac_cvs_sys_ssl="yes",ac_cv_sys_ssl="no")
    fi
    if test "$ac_cv_sys_ssl" = "yes"; then
	AC_DEFINE(HAVE_SSL)
    fi
])

AC_DEFUN(AC_CHECK_IWAY,
[AC_MSG_CHECKING(for IWAY (/usr/local/iway))
    AC_CACHE_VAL(ac_cv_sys_iway, [dnl
	if test -d /usr/local/iway ; then
	    ac_cv_sys_iway="yes"
	else
	    ac_cv_sys_iway="no"
	fi
    ])
    dnl
    dnl Put any other possible tests for the IWAY here
    dnl
    if test "$ac_cv_sys_iway" = "yes"; then
	AC_DEFINE(IWAY)
    fi
    AC_MSG_RESULT($ac_cv_sys_iway)
])

AC_DEFUN(AC_CHECK_UNION_WAIT,
[AC_MSG_CHECKING(for union wait)
    AC_CACHE_VAL(ac_cv_type_union_wait, [dnl
	AC_TRY_COMPILE(

#include <sys/wait.h>
,

union wait status;
,
ac_cv_type_union_wait=yes
AC_DEFINE(HAVE_UNION_WAIT)
,
ac_cv_type_union_wait=no
) dnl --End of AC_TRY_COMPILE()
    ]) dnl--Endo fo AC_CACHE_VAL()
    AC_MSG_RESULT($ac_cv_type_union_wait)
])

dnl
dnl "Stolen" from the MPICH distribution
dnl
AC_DEFUN(AC_FIND_USER_INCLUDE,[
AC_MSG_CHECKING([for include directory for $1])
ac_find_inc_dir=""
for dir in $2 \
	/usr \
	/usr/include \
	/usr/local \
	/usr/local/$1 \
	/usr/contrib \
	/usr/contrib/$1 \
	$HOME/$1 \
	/opt/$1 \
	/opt/local \
	/opt/local/$1 \
	/local/encap/$1 $USER_INCLUDE_DIRS ; do
	if test -r $dir/$1.h ; then
	    ac_find_inc_dir=$dir
	    break
	fi
	if test -r $dir/include/$1.h ; then
	    ac_find_inc_dir=$dir/include
	    break
	fi
dnl	if test -r $dir/lib/lib$1.a ; then
dnl	    ac_find_lib_file=$dir/lib/lib$1.a
dnl	    break
dnl	fi
done
if test -n "$ac_find_inc_dir" ; then
  AC_MSG_RESULT(found $ac_find_inc_dir)
  dnl Must add to the search path
  CFLAGS="$CFLAGS -I$ac_find_inc_dir"
  ifelse([$3],,,[$3])
else
  AC_MSG_RESULT(no)
  ifelse([$4],,,[$4])
fi
])

dnl
dnl "Stolen" from the MPICH distribution
dnl
AC_DEFUN(AC_FIND_USER_LIB,[
AC_MSG_CHECKING([for library $1])
ac_find_lib_file=""
for dir in $2 \
	/usr \
	/usr/lib \
	/usr/local \
	/usr/local/$1 \
	/usr/contrib \
	/usr/contrib/$1 \
	$HOME/$1 \
	/opt/$1 \
	/opt/local \
	/opt/local/$1 \
	/local/encap/$1 ; do
  for ext in a so; do
	if test -r $dir/$1.$ext ; then
	    ac_find_lib_file=$dir/$1.$ext
	    ac_find_lib_dir=$dir
	    break
	fi
	if test -r $dir/lib$1.$ext ; then
	    ac_find_lib_file=$dir/lib$1.$ext
	    ac_find_lib_dir=$dir
	    break
	fi
	if test -r $dir/lib/$1.$ext ; then
	    ac_find_lib_file=$dir/lib/$1.$ext
	    ac_find_lib_dir=$dir/lib
	    break
	fi
	if test -r $dir/lib/lib$1.$ext ; then
	    ac_find_lib_file=$dir/lib/lib$1.$ext
	    ac_find_lib_dir=$dir/lib
	    break
	fi
  done
done
if test -n "$ac_find_lib_file" ; then
  AC_MSG_RESULT(found $ac_find_lib_file)
  ifelse([$3],,,[$3])
else
  AC_MSG_RESULT(no)
  ifelse([$4],,,[$4])
fi
])

AC_DEFUN(AC_CHECK_POSIX_SIGNAL,
[found_ps_funcs="yes"
    AC_MSG_CHECKING(for posix signal in cache)
    AC_CACHE_VAL(ac_cv_check_posix_signal, [dnl
        AC_MSG_RESULT(no. checking manually)
	found_ps_funcs="no"
    ])

    if test "$found_ps_funcs" = "no"; then
	found_ps_funcs="yes"
        AC_CHECK_FUNCS(sigaction sigemptyset sigaddset sigprocmask waitpid,
	# do nothing if it finds the functions
	,
	    found_ps_funcs="no"
	    break) dnl --End of AC_CHECK_FUNCS()
    else
	AC_MSG_RESULT($ac_cv_check_posix_signal);
    fi

    if test "$found_ps_funcs" = "yes" ; then
        ac_cv_check_posix_signal="yes"
        AC_DEFINE(HAVE_POSIX_SIGNAL)
    else
	ac_cv_check_posix_signal="no"
	AC_DEFINE(HAVE_BSD_SIGNAL)
    fi
]) dnl --End of AC_CHECK_POSIX_SIGNALS()

AC_DEFUN(AC_SYS_SIGNAL_WAITING,
[ AC_MSG_CHECKING(for SIGWAITING)
  AC_CACHE_VAL(ac_cv_sys_signal_waiting, [dnl
  AC_EGREP_CPP(zowie,
[#include <signal.h>
#ifdef SIGWAITING
 zowie
#endif
], ac_cv_sys_signal_waiting="yes",
ac_cv_sys_signal_waiting="no")
])
  AC_MSG_RESULT($ac_cv_sys_signal_waiting)
  if test "$ac_cv_sys_signal_waiting" = "yes"; then
    AC_DEFINE(HAVE_SYS_SIGWAITING)
  fi
])
dnl
dnl Eventually, this should include acmakeinfo.m4
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
dnl from sowing/confdb/aclocal_cc.m4.  Change that copy if you fix a bug int
dnl the below macro.
AC_DEFUN(PAC_C_TRY_COMPILE_CLEAN,[
$3=2
dnl Get the compiler output to test against
if test -z "$pac_TRY_COMPLILE_CLEAN" ; then
    rm -f conftest*
    echo 'int try(void);int try(void){return 0;}' > conftest.c
    if ${CC-cc} $CFLAGS -c conftest.c >conftest.bas 2>&1 ; then
	if test -s conftest.bas ; then 
	    pac_TRY_COMPILE_CLEAN_OUT=`cat conftest.bas`
        fi
        pac_TRY_COMPILE_CLEAN=1
    else
	AC_MSG_WARN([Could not compile simple test program!])
	if test -s conftest.bas ; then 	cat conftest.bas >> config.log ; fi
    fi
fi
dnl
dnl Create the program that we need to test with
rm -f conftest*
cat >conftest.c <<EOF
#include "confdefs.h"
[$1]
[$2]
EOF
dnl
dnl Compile it and test
if ${CC-cc} $CFLAGS -c conftest.c >conftest.bas 2>&1 ; then
    dnl Success.  Is the output the same?
    if test "$pac_TRY_COMPILE_CLEAN_OUT" = "`cat conftest.bas`" ; then
	$3=0
    else
        cat conftest.c >>config.log
	if test -s conftest.bas ; then 	cat conftest.bas >> config.log ; fi
        $3=1
    fi
else
    dnl Failure.  Set flag to 2
    cat conftest.c >>config.log
    if test -s conftest.bas ; then cat conftest.bas >> config.log ; fi
    $3=2
fi
rm -f conftest*
])
