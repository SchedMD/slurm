##*****************************************************************************
#  $Id$
##*****************************************************************************
#  AUTHOR:
#    Chris Dunlap <cdunlap@llnl.gov>
#
#  SYNOPSIS:
#    X_AC_DEBUG
#
#  DESCRIPTION:
#    Add support for the "--enable-debug", "--enable-memory-leak-debug",
#    and "--enable-front-end" configure script options.
#    If debugging is enabled, CFLAGS will be prepended with the debug flags.
#    The NDEBUG macro (used by assert) will also be set accordingly.
#
#  WARNINGS:
#    This macro must be placed after AC_PROG_CC or equivalent.
##*****************************************************************************

AC_DEFUN([X_AC_DEBUG], [
  AC_MSG_CHECKING([whether debugging is enabled])
  AC_ARG_ENABLE(
    [debug],
    AS_HELP_STRING(--enable-debug,enable debugging code for development),
    [ case "$enableval" in
        yes) x_ac_debug=yes ;;
         no) x_ac_debug=no ;;
          *) AC_MSG_RESULT([doh!])
             AC_MSG_ERROR([bad value "$enableval" for --enable-debug]) ;;
      esac
    ]
  )
  if test "$x_ac_debug" = yes; then
    test "$GCC" = yes && CFLAGS="$CFLAGS -Wall -fno-strict-aliasing"
  else
    AC_DEFINE([NDEBUG], [1],
      [Define to 1 if you are building a production release.]
    )
  fi
  AC_MSG_RESULT([${x_ac_debug=no}])

  AC_MSG_CHECKING([whether memory leak debugging is enabled])
  AC_ARG_ENABLE(
    [memory-leak-debug],
    AS_HELP_STRING(--enable-memory-leak-debug,enable memory leak debugging code for development),
    [ case "$enableval" in
        yes) x_ac_memory_debug=yes ;;
         no) x_ac_memory_debug=no ;;
          *) AC_MSG_RESULT([doh!])
             AC_MSG_ERROR([bad value "$enableval" for --enable-memory-leak-debug]) ;;
      esac
    ]
  )
  if test "$x_ac_memory_debug" = yes; then
    AC_DEFINE(MEMORY_LEAK_DEBUG, 1, [Define to 1 for memory leak debugging.])
  fi
  AC_MSG_RESULT([${x_ac_memory_debug=no}])

  AC_MSG_CHECKING([whether to enable slurmd operation on a front-end])
  AC_ARG_ENABLE(
    [front-end],
     AS_HELP_STRING(--enable-front-end, enable slurmd operation on a front-end),
     [ case "$enableval" in
        yes) x_ac_front_end=yes ;;
         no) x_ac_front_end=no ;;
          *) AC_MSG_RESULT([doh!])
             AC_MSG_ERROR([bad value "$enableval" for --enable-front-end]) ;;
      esac
    ]
  )
  if test "$x_ac_front_end" = yes; then
    AC_DEFINE(HAVE_FRONT_END, 1, [Define to 1 if running slurmd on front-end only])
  fi
  AC_MSG_RESULT([${x_ac_front_end=no}])

  ]
)
