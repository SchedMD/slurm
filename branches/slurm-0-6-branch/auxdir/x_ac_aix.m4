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
#    also define HAVE_AIX and HAVE_LARGEFILE if appropriate.
#  NOTE: AC_SYS_LARGEFILE may fail on AIX due to inconstencies within
#    installed gcc header files.
##*****************************************************************************


AC_DEFUN([X_AC_AIX],
[
   case "$host" in
      *-*-aix*) LDFLAGS="$LDFLAGS -Wl,-brtl"  # permit run time linking
 	    CMD_LDFLAGS="$LDFLAGS -Wl,-bgcbypass:1000 -Wl,-bexpfull -Wl,-bmaxdata:0x70000000" # keep all common functions
            LIB_LDFLAGS="$LDFLAGS -Wl,-G -Wl,-bnoentry -Wl,-bexpfull"
            SO_LDFLAGS=" $LDFLAGS -Wl,-G -Wl,-bnoentry -Wl,-bexpfull"
            CFLAGS="-maix32 $CFLAGS"
            ac_have_aix="yes"
            ac_with_readline="no"
            AC_DEFINE(HAVE_AIX, 1, [Define to 1 for AIX operating system])
            AC_DEFINE(USE_ALIAS, 0, 
                      [Define slurm_ prefix function aliases for plusins]) ;;
      *)    ac_have_aix="no"
            AC_DEFINE(USE_ALIAS, 1, 
                      [Define slurm_ prefix function aliases for plugins]) ;;
   esac

   AC_SUBST(CMD_LDFLAGS)
   AC_SUBST(LIB_LDFLAGS)
   AC_SUBST(SO_LDFLAGS)
   AM_CONDITIONAL(HAVE_AIX, test "x$ac_have_aix" = "xyes")
   AC_SUBST(HAVE_AIX, "$ac_have_aix")

   if test "x$ac_have_aix" = "xyes"; then
      AC_ARG_WITH(proctrack,
         AC_HELP_STRING([--with-proctrack=PATH],
                        [Specify path to proctrack sources]),
         [ PROCTRACKDIR="$withval" ]
      )
      if test ! -d "$PROCTRACKDIR" -o ! -f "$PROCTRACKDIR/proctrackext.exp"; then
         AC_MSG_WARN([proctrackext.exp is required for AIX proctrack support, specify location with --with-proctrack])
         ac_have_aix_proctrack="no"
      else
         AC_SUBST(PROCTRACKDIR)
         ac_have_aix_proctrack="yes"
      fi
   else
      ac_have_aix_proctrack="no"
      AC_SYS_LARGEFILE
   fi
   AM_CONDITIONAL(HAVE_AIX_PROCTRACK, test "x$ac_have_aix_proctrack" = "xyes")
])
