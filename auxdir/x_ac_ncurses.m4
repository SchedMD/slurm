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
      ac_have_some_curses="yes"
   elif test "$ac_have_curses" = "yes"; then
      NCURSES="-lcurses"
      ac_have_some_curses="yes"
   fi

   if test "$ac_have_some_curses" = "yes"; then	
        save_LIBS="$LIBS"
        LIBS="$NCURSES $save_LIBS"
        AC_TRY_LINK([
          #include <ncurses.h>
        ],[
          int main()
          {
            (void) initscr();
            (void) endwin();
          }
        ], , [ac_have_some_curses="no"])
        LIBS="$save_LIBS"
        if test "$ac_have_some_curses" == "yes"; then
            AC_MSG_RESULT([NCURSES test program built properly.])    
        else
            AC_MSG_WARN([*** NCURSES test program execution failed.])
        fi	
   else
      AC_MSG_WARN([Can not build smap without curses or ncurses library])
      ac_have_some_curses="no"
   fi
])
