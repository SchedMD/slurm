##*****************************************************************************
## $Id$
##*****************************************************************************
#  AUTHOR:
#    Morris Jette  <jette1@llnl.gov>
#
#  SYNOPSIS:
#    X_AC_NCURSES
#
#  DESCRIPTION:
#    Test for NCURSES or CURSES. If found define NCURSES
##*****************************************************************************


AC_DEFUN([X_AC_NCURSES],
[
   AC_CHECK_LIB([ncurses],
	[initscr],
	[ac_have_ncurses=yes])
   AC_CHECK_LIB([curses],
	[initscr],
	[ac_have_curses=yes])

   AC_SUBST(NCURSES)
   if test "$ac_have_ncurses" = "yes"; then
      NCURSES="-lncurses"
   elif test "$ac_have_curses" = "yes"; then
      NCURSES="-lcurses"
   else
      AC_MSG_ERROR([Can not find curses or ncurses library.])
   fi
])
