##*****************************************************************************
## $Id$
##*****************************************************************************
#  AUTHOR:
#    Danny Auble <da@llnl.gov>
#
#  SYNOPSIS:
#    X_AC_NCURSES
#
#  DESCRIPTION:
#    Test for NCURSES or CURSES. If found define NCURSES
##*****************************************************************************


AC_DEFUN([X_AC_NCURSES],
[
      AC_SUBST(NCURSES)
      ncurses_dir="/usr/lib /usr/lib/curses"

      # Search for "libncurses.a" in the directory
      if test -z "$have_ncurses_ar" -a -f "$ncurses_dir/libncurses.a" ; then
         have_ncurses_ar=yes
         NCURSES="ncurses"
      fi
      if test -z "$have_ncurses_ar" -a -f "$ncurses_dir/libcurses.a" ; then
         NCURSES="curses"
      fi
])
