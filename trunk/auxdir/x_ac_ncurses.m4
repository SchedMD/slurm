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
   ncurses_dir="/usr/lib"

   for curse_dir in $ncurses_dir; do
      # Search for "libncurses.a" in the directory
      if test -z "$have_ncurses_ar" -a -f "$curse_dir/libncurses.a" ; then
         have_ncurses_ar=yes
         NCURSES="-lncurses"
      fi
      if test -z "$have_ncurses_ar" -a -f "$curse_dir/libcurses.a" ; then
         have_ncurses_ar=yes
         NCURSES="-lcurses"
      fi
   done

   if test -z "$have_ncurses_ar" ; then
      AC_MSG_ERROR([Can not find curses or ncurses library.])
   fi
])
