##*****************************************************************************
## $Id$
##*****************************************************************************
#  AUTHOR:
#    Morris Jette <jette@llnl.gov>
#
#  SYNOPSIS:
#    X_AC_AIX
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
            LIB_LDFLAGS="$LDFLAGS -Wl,-G -Wl,-bnoentry -Wl,-bgcbypass:1000 -Wl,-bexpfull"
            SO_LDFLAGS=" $LDFLAGS -Wl,-G -Wl,-bnoentry -Wl,-bgcbypass:1000 -Wl,-bexpfull"
            if test "$OBJECT_MODE" = "64"; then
                CFLAGS="-maix64 $CFLAGS"
                CMD_LDFLAGS="$LDFLAGS -Wl,-bgcbypass:1000 -Wl,-bexpfull" # keep all common functions
            else
                CFLAGS="-maix32 $CFLAGS"
                CMD_LDFLAGS="$LDFLAGS -Wl,-bgcbypass:1000 -Wl,-bexpfull -Wl,-bmaxdata:0x70000000" # keep all common functions
            fi
            ac_have_aix="yes"
            ac_with_readline="no"
            AC_DEFINE(HAVE_AIX, 1, [Define to 1 for AIX operating system])
            ;;
      *)    ac_have_aix="no"
            ;;
   esac

   AC_SUBST(CMD_LDFLAGS)
   AC_SUBST(LIB_LDFLAGS)
   AC_SUBST(SO_LDFLAGS)
   AM_CONDITIONAL(HAVE_AIX, test "x$ac_have_aix" = "xyes")
   AC_SUBST(HAVE_AIX, "$ac_have_aix")

   if test "x$ac_have_aix" = "xyes"; then
      AC_ARG_WITH(proctrack,
         AS_HELP_STRING(--with-proctrack=PATH,Specify path to proctrack sources),
         [ PROCTRACKDIR="$withval" ]
      )
      if test -f "$PROCTRACKDIR/lib/proctrackext.exp"; then
         PROCTRACKDIR="$PROCTRACKDIR/lib"
         AC_SUBST(PROCTRACKDIR)
         CPPFLAGS="-I$PROCTRACKDIR/include $CPPFLAGS"
         AC_CHECK_HEADERS(proctrack.h)
         ac_have_aix_proctrack="yes"
      elif test -f "$prefix/lib/proctrackext.exp"; then
         PROCTRACKDIR="$prefix/lib"
         AC_SUBST(PROCTRACKDIR)
         CPPFLAGS="$CPPFLAGS -I$prefix/include"
	 AC_CHECK_HEADERS(proctrack.h)
         ac_have_aix_proctrack="yes"
      else
         AC_MSG_WARN([proctrackext.exp is required for AIX proctrack support, specify location with --with-proctrack])
         ac_have_aix_proctrack="no"
      fi
   else
      ac_have_aix_proctrack="no"
      AC_SYS_LARGEFILE
   fi
   AM_CONDITIONAL(HAVE_AIX_PROCTRACK, test "x$ac_have_aix_proctrack" = "xyes")

   AC_MSG_CHECKING([for use of LoadLeveler APIs instead of SLURM daemons])
   AC_ARG_WITH([loadleveler],
     AS_HELP_STRING(--with-loadleveler=PATH,Specify path to LoadLeveler header and library),
     [ if test -f "$withval/lib64/libllapi.so" && test -f "$withval/include/llapi.h"; then
          LDFLAGS="-L$withval/lib64 -lllapi $LDFLAGS"
          LOADLEVELER_INCLUDES="-I$withval/include"
          AC_DEFINE(HAVE_LLAPI_H, 1, [Define to 1 if llapi.h found])
          ac_with_llapi_h="yes"
       else
          AC_MSG_WARN([libllapi.so or llapi.h not found. Emulating LoadLeveler])
       fi
       AC_DEFINE(USE_LOADLEVELER, 1, [Define to 1 if running over LoadLeveler])
       ac_with_load_leveler="yes"
     ]
   )
   AC_SUBST(LOADLEVELER_INCLUDES)
   AM_CONDITIONAL(HAVE_LLAPI_H,    test "x$ac_with_llapi_h" = "xyes")
   AM_CONDITIONAL(USE_LOADLEVELER, test "x$ac_with_load_leveler" = "xyes")

])
