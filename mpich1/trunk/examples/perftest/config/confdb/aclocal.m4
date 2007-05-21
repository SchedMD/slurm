dnl
dnl This version of aclocal.m4 simply includes all of the individual
dnl components
builtin(include,aclocal_am.m4)
builtin(include,aclocal_bugfix.m4)
builtin(include,aclocal_cache.m4)
builtin(include,aclocal_cc.m4)
builtin(include,aclocal_cross.m4)
builtin(include,aclocal_cxx.m4)
builtin(include,aclocal_f77.m4)
dnl If we're using 2.52, include new support for F90
dnl If we're using 2.13, use the old version
dnl If we're using 2.57, we're in trouble.  
dnl !!!! 2.57 doesn't define AC_ACVERSION !!!!
dnl 2.57 seems to define AC_PREPROC_IFELSE, so we use that 
dnl 
ifelse(AC_ACVERSION,[2.52],[
builtin(include,aclangf90.m4)
],[
ifdef([AC_PREPROC_IFELSE],[
builtin(include,fortran90.m4)],
[builtin(include,aclocal_f90.m4)])
])
dnl builtin(include,aclocal_csharp.m4)
builtin(include,aclocal_make.m4)
builtin(include,aclocal_mpi.m4)
builtin(include,aclocal_web.m4)
builtin(include,libtool.m4)
builtin(include,aclocal_shl.m4)
dnl builtin(include,aclocal_tcl.m4)
builtin(include,aclocal_java.m4)

dnl
dnl Newer versions of autoconf support AC_EXEEXT and AC_OBJEXT.
dnl If these are not available, just set them as empty
ifdef([AC_EXEEXT],,[
AC_DEFUN(AC_EXEEXT,[
EXEEXT=
AC_SUBST(EXEEXT)
])])
ifdef([AC_OBJEXT],,[
AC_DEFUN(AC_OBJEXT,[
OBJEXT=
AC_SUBST(OBJEXT)
])])
dnl
dnl PAC_MKDIRS(path)
dnl Create any missing directories in the path
dnl
AC_DEFUN([PAC_MKDIRS],[
#
# Build any intermediate directories
for dir in $1 ; do
    saveIFS="$IFS"
    IFS="/"
    tmp_curdir=""
    for tmp_subdir in $dir ; do
	tmp_curdir="${tmp_curdir}$tmp_subdir"
	if test ! -d "$tmp_curdir" ; then mkdir "$tmp_curdir" ; fi
        tmp_curdir="${tmp_curdir}/"
    done
    IFS="$saveIFS"
done
])
dnl
dnl PAC_CONFIG_SUBDIRS_IMMEDIATE(DIR ...)
dnl Perform the configuration *now*
dnl 
dnl There is a bug in AC_OUTPUT_SUBDIRS that is tickled by this
dnl code.  There is no step to create any of the intermediate
dnl directories in the case that this is a vpath build.  
dnl
AC_DEFUN(PAC_CONFIG_SUBDIRS_IMMEDIATE,
[AC_REQUIRE([AC_CONFIG_AUX_DIR_DEFAULT])dnl
SAVE_subdirs="$subdirs"
subdirs="$1"
#
# Build any intermediate directories
for dir in $1 ; do
    saveIFS="$IFS"
    IFS="/"
    curdir=""
    for subdir in $dir ; do
	curdir="${curdir}$subdir"
	if test ! -d "$curdir" ; then mkdir "$curdir" ; fi
        curdir="${curdir}/"
    done
    IFS="$saveIFS"
done
PAC_CACHE_CLEAN
dnl autoconf 2.52 uses _ before *some* internal commands (!)
dnl output_subdirs *ALSO* resets INSTALL.  It *also* requires that 
dnl ac_given_INSTALL be set to INSTALL
SAVE_INSTALL="$INSTALL"
ac_given_INSTALL="$INSTALL"
ifdef([AC_OUTPUT_SUBDIRS],[AC_OUTPUT_SUBDIRS($1)],[_AC_OUTPUT_SUBDIRS($1)])
subdirs="$SAVE_subdirs"
INSTALL="$SAVE_INSTALL"
])

dnl
dnl Find something to use for mkdir -p.  Eventually, this will 
dnl have a script for backup
AC_DEFUN(PAC_PROG_MKDIR_P,[
AC_CACHE_CHECK([whether mkdir -p works],
pac_cv_mkdir_p,[
pac_cv_mkdir_p=no
rm -rf .tmp
if mkdir -p .tmp/.foo 1>/dev/null 2>&1 ; then 
    if test -d .tmp/.foo ; then 
        pac_cv_mkdir_p=yes
    fi
fi
rm -rf .tmp
])
if test "$pac_cv_mkdir_p" = "yes" ; then
   MKDIR_P="mkdir -p"
   export MKDIR_P
else
   AC_MSG_WARN([mkdir -p does not work; the install step may fail])
fi
AC_SUBST(MKDIR_P)
])
dnl
dnl
dnl Test for a clean VPATH directory.  Provide this command with the names
dnl of all of the generated files that might cause problems 
dnl (Makefiles won't cause problems because there's no VPATH usage for them)
AC_DEFUN([PAC_VPATH_CHECK],[
if test "`cd $srcdir && pwd`" != "`pwd`" ; then
    pac_dirtyfiles=""
    pac_header=""
    ifdef([AC_LIST_HEADER],[pac_header=AC_LIST_HEADER])
    for file in config.status $pac_header $1 ; do
        if test -f $srcdir/$file ; then 
	    pac_dirtyfiles="$pac_dirtyfiles $file"
	fi
    done

    if test -n "$pac_dirtyfiles" ; then
        if test -f $srcdir/Makefile ; then
            AC_MSG_ERROR([You cannot do a VPATH build if the source directory has been
    configured.  Run "make distclean" in $srcdir first and make sure that the
    files $pac_dirtyfiles have been removed.])
        else
            AC_MSG_ERROR([You cannot do a VPATH build if the source directory has been
    configured.  Remove the files $pac_dirtyfiles in $srcdir.])
        fi
    fi
fi
])
