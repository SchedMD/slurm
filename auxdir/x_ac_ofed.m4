##*****************************************************************************
#  AUTHOR:
#    Yiannis Georgiou<yiannis.georgiou@bull.net>
#
#  SYNOPSIS:
#    X_AC_OFED
#
#  DESCRIPTION:
#    Determine if the OFED related libraries exist
##*****************************************************************************

AC_DEFUN([X_AC_OFED], [

  _x_ac_ofed_dirs="/usr /usr/local"
  _x_ac_ofed_libs="lib64 lib"

  AC_ARG_WITH(
    [ofed],
    AS_HELP_STRING(--with-ofed=PATH,Specify path to ofed installation),
    [AS_IF([test "x$with_ofed" != xno && test "x$with_ofed" != xyes],
	   [_x_ac_ofed_dirs="$with_ofed"])])

  if [test "x$with_ofed" = xno]; then
     AC_MSG_WARN([support for ofed disabled])
  else
    AC_CACHE_CHECK(
      [for ofed installation],
      [x_ac_cv_ofed_dir],
      [
        for d in $_x_ac_ofed_dirs; do
          test -d "$d" || continue
          test -d "$d/include/infiniband" || continue
          test -f "$d/include/infiniband/mad.h" || continue
          for bit in $_x_ac_ofed_libs; do
            test -d "$d/$bit" || continue
            _x_ac_ofed_cppflags_save="$CPPFLAGS"
            CPPFLAGS="-I$d/include $CPPFLAGS"
            _x_ac_ofed_libs_save="$LIBS"
            LIBS="-L$d/$bit -libmad -libumad $LIBS"
            AC_LINK_IFELSE(
              [AC_LANG_CALL([], mad_rpc_open_port)],
              AS_VAR_SET(x_ac_cv_ofed_dir, $d), [])
            AC_LINK_IFELSE(
              [AC_LANG_CALL([], pma_query_via)],
              [have_pma_query_via=yes],
              [AC_MSG_RESULT(Using old libmad)])
            CPPFLAGS="$_x_ac_ofed_cppflags_save"
            LIBS="$_x_ac_ofed_libs_save"
            test -n "$x_ac_cv_ofed_dir" && break
          done
          test -n "$x_ac_cv_ofed_dir" && break
        done
    ])

    if test -z "$x_ac_cv_ofed_dir"; then
      if test -z "$with_ofed"; then
        AC_MSG_WARN([unable to locate ofed installation])
      else
        AC_MSG_ERROR([unable to locate ofed installation])
      fi
    else
      OFED_CPPFLAGS="-I$x_ac_cv_ofed_dir/include/infiniband"
      if test "$ac_with_rpath" = "yes"; then
        OFED_LDFLAGS="-Wl,-rpath -Wl,$x_ac_cv_ofed_dir/$bit -L$x_ac_cv_ofed_dir/$bit"
      else
        OFED_LDFLAGS="-L$x_ac_cv_ofed_dir/$bit"
      fi
      OFED_LIBS="-libmad -libumad"
      AC_DEFINE(HAVE_OFED, 1, [Define to 1 if ofed library found])
      if test ! -z "$have_pma_query_via" ; then
        AC_DEFINE(HAVE_OFED_PMA_QUERY_VIA, 1, [Define to 1 if using code with pma_query_via])
      fi
    fi
    AC_SUBST(OFED_LIBS)
    AC_SUBST(OFED_CPPFLAGS)
    AC_SUBST(OFED_LDFLAGS)
  fi

  AM_CONDITIONAL(BUILD_OFED, test -n "$x_ac_cv_ofed_dir")
])

