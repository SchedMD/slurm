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
#    Check for AIX operating system and sets parameters accordingly
##*****************************************************************************


AC_DEFUN([X_AC_AIX],
[
   case "$host" in
      *-*-aix*) LDFLAGS="$LDFLAGS -Wl,-brtl -Wl,-blpdata"  # permit run time linking and large pages
            CMD_LDFLAGS="$LDFLAGS -Wl,-bgcbypass:1000" # keep all common functions
            LIB_LDFLAGS="$LDFLAGS -Wl,-G -Wl,-bnoentry -Wl,-bexpfull"
            SO_LDFLAGS=" $LDFLAGS -Wl,-G -Wl,-bnoentry -Wl,-bexpfull"
            ac_have_aix="yes"
            ac_with_readline="no"
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
])
