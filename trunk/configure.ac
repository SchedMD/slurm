# This file is to be processed with autoconf to generate a configure script

AC_INIT([slurm], [0.1])
# AC_PREREQ(2.50)
AC_CONFIG_SRCDIR([src/common/slurm.h])
AC_CONFIG_AUX_DIR(auxdir)
AC_CANONICAL_HOST
AC_CANONICAL_SYSTEM

# determine project/version 
# PROJECT="slurm"
#AC_DEFINE_UNQUOTED(PROJECT, "$PROJECT", [Define the project's name.])
#AC_SUBST(PROJECT)
# VERSION="0.1"
#AC_DEFINE_UNQUOTED(VERSION, "$VERSION", [Define the project's version.])
#AC_SUBST(VERSION)

dnl Automake stuff.
AM_CONFIG_HEADER(config.h)
AM_INIT_AUTOMAKE(AC_PACKAGE_NAME, AC_PACKAGE_VERSION)

# check for debug compilation of slurm modules:
AC_MSG_CHECKING(whether debug compilation of modules is enabled)
AC_ARG_ENABLE(debug, 
  AC_HELP_STRING([--enable-debug],[Enable debugging of individual modules]),
  [ case "${enableval}" in
     yes) debug=true ;;
     no)  debug=false ;;
     *) AC_MSG_ERROR([bad value ${enableval} for --enable-debug]) ;;
    esac
  ]
)
AC_MSG_RESULT(${debug=no})
if test "x$debug" = "xtrue"; then 
  CFLAGS="${CFLAGS--g -Wall}"
  AC_DEFINE(DEBUG_SYSTEM, 1, [Define for extra debug messages.])
else
  CFLAGS="${CFLAGS--g -O2}"
fi
AM_CONDITIONAL(DEBUG_MODULES, test "x$debug" = "xtrue")

dnl Checks for programs.
AC_PROG_CC
AC_PROG_MAKE_SET
AC_PROG_RANLIB

dnl Checks for libraries

dnl Checks for header files.
AC_CHECK_HEADERS(popt.h mcheck.h unistd.h)
AC_CHECK_HEADERS(strings.h values.h)

dnl Checks for types.
AC_PROG_GCC_TRADITIONAL([])

dnl Checks for structures.

dnl Checks for compiler characteristics.

dnl checks for library functions.
AC_CHECK_FUNCS(strerror mtrace)
AC_SEARCH_LIBS([pthread_create], [pthread c_r cr], [], 
               [AC_MSG_ERROR([Error: Cannot find the pthread library])])

AC_CHECK_LIB(popt, poptGetNextOpt, [got_popt=yes], [got_popt=no])
AM_CONDITIONAL(USE_INCLUDED_POPT, test $got_popt = no)
if test $got_popt = no; then
    AC_MSG_RESULT([using included popt package...])
    POPT_LIBS="\$(top_srcdir)/src/popt/libpopt.a"
    POPT_INCLUDES="-I\$(top_srcdir)/src/popt"
else
    POPT_LIBS="-lpopt"
fi
AC_SUBST(POPT_LIBS)
AC_SUBST(POPT_INCLUDES)

dnl checks for system services.


dnl All slurm Makefiles:
dnl need to add squeue and scancel when they're there

AC_CONFIG_FILES([Makefile 
                 src/Makefile 
		 src/common/Makefile 
		 src/popt/Makefile 
		 src/srun/Makefile 
		 src/slurmd/Makefile 
		 src/slurmctld/Makefile 
		 src/scontrol/Makefile
		]
)


AC_OUTPUT

