# This file is to be processed with autoconf to generate a configure script

AC_INIT(src/common/slurm.h)
AC_CONFIG_AUX_DIR(auxdir)
AC_CANONICAL_SYSTEM

# determine project/version 
dnl XXX fill this in from ./META or something ...
PROJECT="slurm"
AC_DEFINE_UNQUOTED(PROJECT, "$PROJECT", [Define the project's name.])
AC_SUBST(PROJECT)
VERSION="0.0"
AC_DEFINE_UNQUOTED(VERSION, "$VERSION", [Define the project's version.])
AC_SUBST(VERSION)

dnl Automake stuff.
AM_CONFIG_HEADER(config.h)
AM_INIT_AUTOMAKE(PROJECT, VERSION)

dnl Checks for programs.
AC_PROG_CC
AC_PROG_MAKE_SET
AC_PROG_RANLIB

dnl Checks for libraries

dnl Checks for header files.
AC_CHECK_HEADERS(popt.h mcheck.h unistd.h)
AC_CHECK_HEADERS(strings.h values.h)

dnl Checks for types.
AC_GCC_TRADITIONAL

dnl Checks for structures.

dnl Checks for compiler characteristics.

dnl checks for library functions.
AC_CHECK_FUNCS(strerror mtrace)
AC_SEARCH_LIBS([pthread_create], [pthread c_r cr], [], 
               [AC_MSG_ERROR([Error: Cannot find the pthread library])])

AC_CHECK_LIB(popt, poptGetNextOpt, [got_popt=yes], [got_popt=no])
AM_CONDITIONAL(USE_INCLUDED_POPT, test x$got_popt = xno)
if test "$got_popt" = "no"; then
    AC_MSG_RESULT([using included popt package...])
    POPT_LIBS="src/popt/libpopt.a"
    POPT_INCLUDES="-Isrc/popt"
else
    POPT_LIBS="-lpopt"
fi
AC_SUBST(POPT_LIBS)
AC_SUBST(POPT_INCLUDES)

dnl checks for system services.


AC_OUTPUT([Makefile src/Makefile src/common/Makefile src/popt/Makefile src/srun/Makefile src/slurmd/Makefile src/slurmctld/Makefile src/scancel/Makefile src/squeue/Makefile src/scontrol/Makefile])
