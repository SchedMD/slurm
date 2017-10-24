##*****************************************************************************
#  AUTHOR:
#    Tim Wickberg <tim@schedmd.com>
#
#  SYNOPSIS:
#    X_AC_DEPRECATED
#
#  DESCRIPTION:
#    Add support for the "--enable-deprecated" configure script option,
#    and error out if any deprecated options are enabled without this
#    option having been set.
##*****************************************************************************

AC_DEFUN([X_AC_DEPRECATED], [
  AC_MSG_CHECKING([whether deprecated options are enabled])
  AC_ARG_ENABLE(
    [deprecated],
    AS_HELP_STRING(--enable-deprecated,enable deprecated),
    [ case "$enableval" in
        yes) x_ac_deprecated=yes ;;
         no) x_ac_deprecated=no ;;
          *) AC_MSG_RESULT([doh!])
             AC_MSG_ERROR([bad value "$enableval" for --enable-deprecated]) ;;
      esac
    ]
  )
  AC_MSG_RESULT([${x_ac_deprecated=no}])

  if test "$x_ac_deprecated" = no; then
     if test "$ac_bluegene_loaded" = yes; then
        AC_MSG_ERROR([BlueGene support is deprecated and will be removed in a future release. SchedMD customers are encouraged to contact support to discuss further. Use --enable-deprecated to build.])
     elif test "$ac_have_alps_cray" = yes; then
        AC_MSG_ERROR([Cray/ALPS support is deprecated and will be removed in a future release. Please consider using Native Cray mode. SchedMD customers are encouraged to contact support to discuss further. Use --enable-deprecated to build.])
     elif test "$ac_have_blcr" = yes; then
        AC_MSG_ERROR([BLCR support is deprecated and will be removed in a future release. SchedMD customers are encouraged to contact support to discuss further. Use --enable-deprecated to build.])
     fi
   fi
])
