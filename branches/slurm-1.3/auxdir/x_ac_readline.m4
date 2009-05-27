##*****************************************************************************
## $Id$
##*****************************************************************************
#  AUTHOR:
#    Jim Garlick <garlick@llnl.gov>
#
#  SYNOPSIS:
#    AC_READLINE
#
#  DESCRIPTION:
#    Adds support for --without-readline. Exports READLINE_LIBS if found
#    
#
#  WARNINGS:
#    This macro must be placed after AC_PROG_CC and X_AC_CURSES.
##*****************************************************************************

AC_DEFUN([X_AC_READLINE],
[
  AC_MSG_CHECKING([for whether to include readline suport])
  AC_ARG_WITH([readline],
    AS_HELP_STRING(--without-readline,compile without readline support),
      [ case "$withval" in
        yes) ac_with_readline=yes ;;
        no)  ac_with_readline=no ;;
        *)   AC_MSG_RESULT([doh!])
             AC_MSG_ERROR([bad value "$withval" for --without-readline]) ;;
      esac
    ]
  )

  AC_MSG_RESULT([${ac_with_readline=yes}])
  if test "$ac_with_readline" = "yes"; then
    saved_LIBS="$LIBS"
    READLINE_LIBS="-lreadline -lhistory $NCURSES"
    LIBS="$saved_LIBS $READLINE_LIBS"
    AC_LINK_IFELSE([AC_LANG_PROGRAM([[	#include <stdio.h>
	#include <readline/readline.h>
	#include <readline/history.h>]], [[
	char *line = readline("in:");]])],[AC_DEFINE([HAVE_READLINE], [1], 
                 [Define if you are compiling with readline.])],[READLINE_LIBS=""])
    LIBS="$saved_LIBS"
  fi
  AC_SUBST(READLINE_LIBS)
])
