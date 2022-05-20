##*****************************************************************************
#  AUTHOR:
#    Danny Auble  <da@schedmd.com>
#
#  SYNOPSIS:
#    X_AC_LZ4
#
#  DESCRIPTION:
#    Test if we have liblz4 installed. If found define appropriate ENVs.
#
##*****************************************************************************

AC_DEFUN([X_AC_LZ4],
#
# Handle user hints
#
[AC_MSG_CHECKING(if lz4 is wanted)
lz4_places="/usr/local /usr /opt/local /sw"
AC_ARG_WITH([lz4],
[  --with-lz4=PATH         Specify path to liblz4 installation],
[AS_IF([test "x$with_lz4" != xno && test "x$with_lz4" != xyes],
       [lz4_places="$with_lz4"; AC_MSG_RESULT([yes])],
       [AC_MSG_RESULT([no])])])

#
# Locate lz4, if installed
#
if [test "x$with_lz4" = xno]; then
  AC_MSG_WARN([support for lz4 disabled])
else
  # check the user supplied or any other more or less 'standard' place:
  #   Most UNIX systems      : /usr/local and /usr
  #   MacPorts / Fink on OSX : /opt/local respectively /sw
  HAVE_LZ4=0
  for LZ4_HOME in ${lz4_places} ; do
    test -f "${LZ4_HOME}/include/lz4.h" || continue

    LZ4_OLD_LDFLAGS=$LDFLAGS
    LZ4_OLD_CPPFLAGS=$CPPFLAGS
    if test -n "${LZ4_HOME}"; then
      LZ4_CPPFLAGS="-I${LZ4_HOME}/include"
      LZ4_LDFLAGS="-L${LZ4_HOME}/lib"
      LZ4_LIBS="-llz4"

      LDFLAGS="$LDFLAGS ${LZ4_LDFLAGS}"
      CPPFLAGS="$CPPFLAGS ${LZ4_CPPFLAGS}"
    fi
    AC_LANG_SAVE
    AC_LANG([C])
    AC_CHECK_LIB([lz4], [LZ4_compress_destSize], [ac_cv_lz4=yes], [ac_cv_lz4=no])
    AC_CHECK_HEADER([lz4.h], [ac_cv_lz4_h=yes], [ac_cv_lz4_h=no])
    AC_LANG_RESTORE

    # Restore variables
    LDFLAGS="$LZ4_OLD_LDFLAGS"
    CPPFLAGS="$LZ4_OLD_CPPFLAGS"

    if [ test "$ac_cv_lz4" = "yes" && test "$ac_cv_lz4_h" = "yes" ]; then
        #
        # If both library and header were found, action-if-found
        #
        AC_SUBST(LZ4_CPPFLAGS)
        AC_SUBST(LZ4_LDFLAGS)
        AC_SUBST(LZ4_LIBS)
        AC_DEFINE([HAVE_LZ4], [1],
                  [Define to 1 if you have 'lz4' library (-llz4)])
        AC_MSG_RESULT([LZ4 test program built properly.])
        HAVE_LZ4=1
        break
    else
        AC_MSG_RESULT([LZ4 test program build failed.])
    fi
  done

  if [test "$HAVE_LZ4" != 1]; then
    if [test -z "$with_lz4"]; then
      AC_MSG_WARN([unable to locate working lz4 installation])
    else
      AC_MSG_ERROR([unable to locate working lz4 installation])
    fi
  fi


fi

])
