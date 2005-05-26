##*****************************************************************************
## $Id$
##*****************************************************************************
#  AUTHOR:
#    Morris Jette <jette@llnl.gov>
#
#  SYNOPSIS:
#    AC_AIX
#
#  DESCRIPTION:
#    Check for AIX operating system and sets parameters accordingly, 
#    also define HAVE_AIX if appropriate
##*****************************************************************************


AC_DEFUN([X_AC_AIX],
[
   case "$host" in
      *-*-aix*) LDFLAGS="$LDFLAGS -Wl,-brtl"  # permit run time linking
 	    CMD_LDFLAGS="$LDFLAGS -Wl,-bgcbypass:1000 -Wl,-bexpfull" # keep all common functions
            LIB_LDFLAGS="$LDFLAGS -Wl,-G -Wl,-bnoentry -Wl,-bexpfull"
            SO_LDFLAGS=" $LDFLAGS -Wl,-G -Wl,-bnoentry -Wl,-bexpfull"
            CFLAGS="-maix32 $CFLAGS"
            ac_have_aix="yes"
            ac_with_readline="no"
            AC_DEFINE(HAVE_AIX, 1, [Define to 1 for AIX operating system])
            AC_DEFINE(USE_ALIAS, 0, 
                      [Define slurm_ prefix function aliases for plusins]) ;;
      *)    AC_DEFINE(USE_ALIAS, 1, 
                      [Define slurm_ prefix function aliases for plugins]) ;;
   esac

   AC_SUBST(CMD_LDFLAGS)
   AC_SUBST(LIB_LDFLAGS)
   AC_SUBST(SO_LDFLAGS)
   AM_CONDITIONAL(HAVE_AIX, test "x$ac_have_aix" = "xyes")
   AC_SUBST(HAVE_AIX)

   if test "x$ac_have_aix" = "xyes"; then
      AC_ARG_WITH(proctrack,
         AC_HELP_STRING([--with-proctrack=PATH],
                        [Specify path to proctrack sources]),
         [ PROCTRACKDIR="$withval" ]
      )
      if test ! -d "$PROCTRACKDIR" -o ! -f "$PROCTRACKDIR/proctrackext.exp"; then
         AC_MSG_ERROR([$PROCTRACKDIR/proctrackext.exp is required and does not exist])
      fi
      AC_SUBST(PROCTRACKDIR)
   fi
])
