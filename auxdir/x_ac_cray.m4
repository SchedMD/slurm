##*****************************************************************************
#  AUTHOR:
#    Morris Jette <jette1@llnl.gov>
#
#  SYNOPSIS:
#    X_AC_CRAY
#
#  DESCRIPTION:
#    Test for Cray XT and XE systems with 2-D/3-D interconnects.
#    Tests for required libraries (native Cray systems only):
#    * mySQL (relies on testing for mySQL presence earlier);
#    * libexpat, needed for XML-RPC calls to Cray's BASIL
#      (Batch Application  Scheduler Interface Layer) interface.
#*****************************************************************************

AC_DEFUN([X_AC_CRAY], [
  x_ac_cray="no"
  ac_have_native_cray="no"

  AC_MSG_CHECKING([whether support for Cray XT/XE is enabled])
  AC_ARG_ENABLE(
    [cray],
    AS_HELP_STRING(--enable-cray,enable Cray XT/XE system support),
    [ case "$enableval" in
        yes) x_ac_cray="yes" ;;
         no) x_ac_cray="no"  ;;
          *) AC_MSG_ERROR([bad value "$enableval" for --enable-cray]) ;;
      esac
    ]
  )
  AC_MSG_RESULT([$x_ac_cray])

  if test "$x_ac_cray" = "yes"; then
    AC_DEFINE(HAVE_CRAY, 1, [Define to 1 for basic support of Cray XT/XE systems])

    AC_MSG_CHECKING([whether this is a native Cray XT or XE system])
    # Check for a Cray-specific file:
    #  * older XT systems use an /etc/xtrelease file
    #  * newer XT/XE systems use an /etc/opt/cray/release/xtrelease file
    #  * both have an /etc/xthostname
    if test -f /etc/xtrelease  || test -d /etc/opt/cray/release ; then
      ac_have_native_cray="yes"
    fi
    AC_MSG_RESULT([$ac_have_native_cray])
  fi

  if test "$ac_have_native_cray" = "yes"; then
      if test -z "$MYSQL_CFLAGS" || test -z "$MYSQL_LIBS"; then
        AC_MSG_ERROR([BASIL requires the cray-MySQL-devel-enterprise rpm])
      fi
      ALPS_LIBS="$MYSQL_LIBS"

      AC_CHECK_HEADER(expat.h, [],
                    AC_MSG_ERROR([BASIL requires expat headers/rpm]))
      AC_CHECK_LIB(expat, XML_ParserCreate, [ALPS_LIBS="$ALPS_LIBS -lexpat"],
                    AC_MSG_ERROR([BASIL requires libexpat.so/rpm]))
      AC_SUBST(ALPS_LIBS)

      AC_DEFINE(HAVE_NATIVE_CRAY,  1, [Define to 1 for native Cray XT/XE system])
      AC_DEFINE(HAVE_3D,           1, [Define to 1 if 3-dimensional architecture])
      AC_DEFINE(SYSTEM_DIMENSIONS, 3, [3-dimensional architecture])
      AC_DEFINE(HAVE_FRONT_END,    1, [Define to 1 if running slurmd on front-end only])
  fi
  AM_CONDITIONAL(HAVE_NATIVE_CRAY, test "$ac_have_native_cray" = "yes")
])
