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
#    This macro must be placed after AC_PROG_CC or equivalent.
##*****************************************************************************

AC_DEFUN([X_AC_READLINE],
[
  AC_MSG_CHECKING([for whether to include readline suport])
  AC_ARG_WITH([readline],
    AC_HELP_STRING([--without-readline], [compile without readline support]),
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
          savedLIBS="$LIBS"
	  READLINE_LIBS="-lreadline -lhistory -lncurses"

	  AC_CHECK_LIB([readline], [readline], [], 
	      AC_MSG_ERROR([Cannot find libreadline!]), [ -lhistory -lncurses ])

	  AC_DEFINE([HAVE_READLINE], [1], 
		        [Define if you are compiling with readline.])
          LIBS="$savedLIBS"
  fi
  AC_SUBST(READLINE_LIBS)
])
