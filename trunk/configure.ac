# This file is to be processed with autoconf to generate a configure script

# XXX generate PROJECT and VERSION automatically at some point
AC_INIT([slurm],[0.1])
# AC_PREREQ(2.52)
AC_CONFIG_SRCDIR([src/api/slurm.h])
AC_CONFIG_AUX_DIR(auxdir)
AC_CANONICAL_TARGET([])

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
  CFLAGS="${CFLAGS--g -O2 -Wall -Wpointer-arith}"
  AC_DEFINE(DEBUG_SYSTEM, 1, [Define for extra debug messages.])
else
  CFLAGS="${CFLAGS--g -O2 -Wall -Wpointer-arith}"
fi
AM_CONDITIONAL(DEBUG_MODULES, test "x$debug" = "xtrue")
#

# check for whether to include Elan support:
AC_MSG_CHECKING(whether to include Elan support)
AC_ARG_WITH(elan,
  AC_HELP_STRING([--with-elan],[compile with Elan support]),
  [ case "${withval}" in
      yes) elan=yes ;;
      no)  elan=no ;;
      *) AC_MSG_ERROR([bad value ${enableval} for --with-elan]) ;;
    esac
  ]
)
AC_MSG_RESULT(${elan=no})
AM_CONDITIONAL(WITH_ELAN, test "x$with_elan" = "xyes")

  
dnl Checks for programs.
AC_PROG_CC
AC_PROG_MAKE_SET
AC_PROG_LIBTOOL

dnl Checks for libraries
if test $with_elan; then
  AC_CHECK_LIB(elan3, elan3_create,
    [], 
    [AC_MSG_ERROR([unable to find the Elan3 library needed for Elan support.])], 
  )
  AC_CHECK_LIB(rms, rms_prgcreate, 
    [],
    [AC_MSG_ERROR([unable to find the RMS library needed for Elan support])],
  )
fi

dnl Checks for header files.
AC_CHECK_HEADERS(popt.h mcheck.h values.h socket.h sys/socket.h)
AC_HEADER_SYS_WAIT
AC_HEADER_TIME
AC_HEADER_STDC

dnl Checks for structures.

# check to see if glibc's program_invocation_short_name is available:
AC_MSG_CHECKING([for program_invocation_short_name])
AC_TRY_LINK([extern char *program_invocation_short_name;], 
  [char *p; p = program_invocation_short_name; printf("%s\n", p);],
  [got_program_invocation_short_name=yes],
  []
)
AC_MSG_RESULT(${got_program_invocation_short_name=no})
if test "x$got_program_invocation_short_name" = "xyes"; then
  AC_DEFINE(HAVE_PROGRAM_INVOCATION_SHORT_NAME, 1, 
            [Define if libc sets program_invocation_short_name]
	   )
fi


dnl Checks for types.

dnl Checks for compiler characteristics.
AC_PROG_GCC_TRADITIONAL([])

dnl checks for library functions.
AC_FUNC_MALLOC
AC_FUNC_STRERROR_R
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
		 src/api/Makefile 
		 src/common/Makefile 
		 src/popt/Makefile 
		 src/srun/Makefile 
		 src/slurmd/Makefile 
		 src/slurmctld/Makefile 
		 src/scontrol/Makefile
		 src/test/Makefile
		 testsuite/Makefile
		 testsuite/slurm_compiled/Makefile
		]
)

AC_OUTPUT

