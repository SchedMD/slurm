##*****************************************************************************
#  AUTHOR:
#    Chris Dunlap <cdunlap@llnl.gov>
#
#  SYNOPSIS:
#    X_AC_DEBUG
#
#  DESCRIPTION:
#    Add support for the "--enable-debug", "--enable-memory-leak-debug",
#    "--disable-partial-attach", "--enable-front-end", and "--enable-developer"
#    configure script options.
#
#    options.
#    If debugging is enabled, CFLAGS will be prepended with the debug flags.
#    The NDEBUG macro (used by assert) will also be set accordingly.
#
#  WARNINGS:
#    This macro must be placed after AC_PROG_CC or equivalent.
##*****************************************************************************

AC_DEFUN([X_AC_DEBUG], [

  AC_MSG_CHECKING([whether optimizations are enabled])
  AC_ARG_ENABLE(
    [optimizations],
    AS_HELP_STRING(--disable-optimizations, disable optimizations (sets -O0)),
    [ case "$enableval" in
        yes) x_ac_optimizations=yes ;;
         no) x_ac_optimizations=no ;;
          *) AC_MSG_RESULT([doh!])
             AC_MSG_ERROR([bad value "$enableval" for --enable-optimizations]) ;;
      esac
    ],
    [x_ac_optimizations=yes]
  )
  AC_MSG_RESULT([${x_ac_optimizations}])

  AC_MSG_CHECKING([whether or not developer options are enabled])
  AC_ARG_ENABLE(
    [developer],
    AS_HELP_STRING(--enable-developer,enable developer options (asserts, -Werror - also sets --enable-debug as well)),
    [ case "$enableval" in
        yes) x_ac_developer=yes ;;
         no) x_ac_developer=no ;;
          *) AC_MSG_RESULT([doh!])
             AC_MSG_ERROR([bad value "$enableval" for --enable-developer]) ;;
      esac
    ]
  )
  if test "$x_ac_developer" = yes; then
    test "$GCC" = yes && CFLAGS="$CFLAGS -Werror"
    test "$GXX" = yes && CXXFLAGS="$CXXFLAGS -Werror"
    # automatically turn on --enable-debug if being a developer
    x_ac_debug=yes
  else
    AC_DEFINE([NDEBUG], [1],
      [Define to 1 if you are building a production release.]
    )
  fi
  AC_MSG_RESULT([${x_ac_developer=no}])

  AC_MSG_CHECKING([whether debugging is enabled])
  AC_ARG_ENABLE(
    [debug],
    AS_HELP_STRING(--disable-debug,disable debugging symbols and compile with optimizations),
    [ case "$enableval" in
        yes) x_ac_debug=yes ;;
         no) x_ac_debug=no ;;
          *) AC_MSG_RESULT([doh!])
             AC_MSG_ERROR([bad value "$enableval" for --enable-debug]) ;;
      esac
    ],
    [x_ac_debug=yes]
  )
  if test "$x_ac_debug" = yes; then
    # you will most likely get a -O2 in you compile line, but the last option
    # is the only one that is looked at.
    # We used to force this to -O0, but this precludes the use of FSTACK_PROTECT
    # which is injected into RHEL7/SuSE12 RPM builds rather aggressively.
    AX_CHECK_COMPILE_FLAG([-ggdb3], [CFLAGS="$CFLAGS -ggdb3"])

    test "$GCC" = yes && CFLAGS="$CFLAGS -Wall -g -O1 -fno-strict-aliasing"
    test "$GXX" = yes && CXXFLAGS="$CXXFLAGS -Wall -g -O1 -fno-strict-aliasing"
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

  AC_MSG_CHECKING([whether debugger partial attach enabled])
  AC_ARG_ENABLE(
    [partial-attach],
    AS_HELP_STRING(--disable-partial-attach,disable debugger partial task attach support),
    [ case "$enableval" in
        yes) x_ac_partial_attach=yes ;;
         no) x_ac_partial_attach=no ;;
          *) AC_MSG_RESULT([doh!])
             AC_MSG_ERROR([bad value "$enableval" for --enable-partial-leak-attach]) ;;
      esac
    ]
  )
  if test "$x_ac_partial_attach" != "no"; then
    AC_DEFINE(DEBUGGER_PARTIAL_ATTACH, 1, [Define to 1 for debugger partial task attach support.])
  fi
  AC_MSG_RESULT([${x_ac_partial_attach=no}])

  AC_MSG_CHECKING([whether salloc should kill child processes at job termination])
  AC_ARG_ENABLE(
    [salloc-kill-cmd],
    AS_HELP_STRING(--enable-salloc-kill-cmd,salloc should kill child processes at job termination),
    [ case "$enableval" in
        yes) x_ac_salloc_kill_cmd=yes ;;
         no) x_ac_salloc_kill_cmd=no ;;
          *) AC_MSG_RESULT([doh!])
             AC_MSG_ERROR([bad value "$enableval" for --enable-salloc-kill-cmd]) ;;
      esac
    ]
  )
  if test "$x_ac_salloc_kill_cmd" = yes; then
    AC_DEFINE(SALLOC_KILL_CMD, 1, [Define to 1 for salloc to kill child processes at job termination])
    AC_MSG_RESULT([yes])
  else
    AC_MSG_RESULT([no])
  fi

# NOTE: Default value of SALLOC_RUN_FOREGROUND is system dependent
# x_ac_salloc_background is set to "no" for Cray systems in x_ac_cray.m4
  AC_MSG_CHECKING([whether to disable salloc execution in the background])
  AC_ARG_ENABLE(
    [salloc-background],
    AS_HELP_STRING(--disable-salloc-background,disable salloc execution in the background),
    [ case "$enableval" in
        yes) x_ac_salloc_background=yes ;;
         no) x_ac_salloc_background=no ;;
          *) AC_MSG_RESULT([doh!])
             AC_MSG_ERROR([bad value "$enableval" for --disable-salloc-background]) ;;
      esac
    ]
  )
  if test "$x_ac_salloc_background" = no; then
    AC_DEFINE(SALLOC_RUN_FOREGROUND, 1, [Define to 1 to require salloc execution in the foreground.])
    AC_MSG_RESULT([yes])
  else
    AC_MSG_RESULT([no])
  fi

  if test "$x_ac_optimizations" = no; then
    test "$GCC" = yes && CFLAGS="$CFLAGS -O0"
  fi
])
