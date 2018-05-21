##*****************************************************************************
#  AUTHOR:
#    Artem Polyakov <artpol84@gmail.com>
#
#  SYNOPSIS:
#    X_AC_PMIX
#
#  DESCRIPTION:
#    Determine if the PMIx libraries exists. Derived from "x_ac_hwloc.m4".
##*****************************************************************************

AC_DEFUN([X_AC_PMIX],
[
  _x_ac_pmix_dirs="/usr /usr/local"
  _x_ac_pmix_libs="lib64 lib"

  _x_ac_pmix_v1_found="0"
  _x_ac_pmix_v2_found="0"

  AC_ARG_WITH(
    [pmix],
    AS_HELP_STRING(--with-pmix=PATH,Specify path to pmix installation(s).  Multiple version directories can be ':' delimited.),
    [AS_IF([test "x$with_pmix" != xno],[with_pmix=`echo $with_pmix | sed "s/:/ /"`
      _x_ac_pmix_dirs="$with_pmix"])])

  if [test "x$with_pmix" = xno]; then
    AC_MSG_WARN([support for pmix disabled])
  else
    AC_CACHE_CHECK(
      [for pmix installation],
      [x_ac_cv_pmix_dir],
      [
        for d in $_x_ac_pmix_dirs; do
          test -d "$d" || continue
          test -d "$d/include" || continue
          test -f "$d/include/pmix/pmix_common.h" || test -f $d/include/pmix_common.h || continue
          test -f "$d/include/pmix_server.h" || continue
          for d1 in $_x_ac_pmix_libs; do
            test -d "$d/$d1" || continue
            _x_ac_pmix_cppflags_save="$CPPFLAGS"
            CPPFLAGS="-I$d/include $CPPFLAGS"
            _x_ac_pmix_libs_save="$LIBS"
            LIBS="-L$d/$d1 -lpmix $LIBS"
            AC_LINK_IFELSE(
              [AC_LANG_CALL([], PMIx_Get_version)],
              AS_VAR_SET(x_ac_cv_pmix_dir, $d)
              AS_VAR_SET(x_ac_cv_pmix_libdir, $d/$d1))

            if [test -z "$x_ac_cv_pmix_dir"] ||
               [test -z "$x_ac_cv_pmix_libdir"]; then
              AC_MSG_WARN([unable to locate pmix installation])
              continue
            fi

            _x_ac_pmix_version="0"
            AC_PREPROC_IFELSE([AC_LANG_PROGRAM([
              #include<pmix_server.h>
              #if (PMIX_VERSION_MAJOR != 2L)
                #error "not version 2"
              #endif
            ], [ ] )],
            [ _x_ac_pmix_version="2" ], [ _x_ac_pmix_version="1" ] )

            CPPFLAGS="$_x_ac_pmix_cppflags_save"
            LIBS="$_x_ac_pmix_libs_save"

            m4_define([err_pmix],[was already found in one of the previous paths])

            if [test "$_x_ac_pmix_version" = "1"]; then
              if [test "$_x_ac_pmix_v1_found" = "1" ]; then
                m4_define([err_pmix_v1],[error processing $x_ac_cv_pmix_libdir: PMIx v1.x])
                AC_MSG_ERROR(err_pmix_v1 err_pmix)
              fi

              _x_ac_pmix_v1_found="1"
              PMIX_V1_CPPFLAGS="-I$x_ac_cv_pmix_dir/include"
              if test "$ac_with_rpath" = "yes"; then
                PMIX_V1_LDFLAGS="-Wl,-rpath -Wl,$x_ac_cv_pmix_libdir -L$x_ac_cv_pmix_libdir"
              else
                PMIX_V1_CPPFLAGS+=" -DPMIXP_V1_LIBPATH=\\\"$x_ac_cv_pmix_libdir\\\""
              fi
	      # We don't want to search the other lib after we found it in
	      # one place or we might report a false duplicate if lib64 is a
	      # symlink of lib.
	      break
            fi

            if [test "$_x_ac_pmix_version" = "2"]; then
              if [test "$_x_ac_pmix_v2_found" = "1" ]; then
                m4_define([err_pmix_v2],[error processing $x_ac_cv_pmix_libdir: PMIx v2.x])
                AC_MSG_ERROR(err_pmix_v2 err_pmix)
              fi
              _x_ac_pmix_v2_found="1"
              PMIX_V2_CPPFLAGS="-I$x_ac_cv_pmix_dir/include"
              if test "$ac_with_rpath" = "yes"; then
                PMIX_V2_LDFLAGS="-Wl,-rpath -Wl,$x_ac_cv_pmix_libdir -L$x_ac_cv_pmix_libdir"
              else
		PMIX_V2_CPPFLAGS+=" -DPMIXP_V2_LIBPATH=\\\"$x_ac_cv_pmix_libdir\\\""
              fi
	      # We don't want to search the other lib after we found it in
	      # one place or we might report a false duplicate if lib64 is a
	      # symlink of lib.
	      break
            fi
          done
        done
      ])

    AC_DEFINE(HAVE_PMIX, 1, [Define to 1 if pmix library found])

    AC_SUBST(PMIX_V1_CPPFLAGS)
    AC_SUBST(PMIX_V1_LDFLAGS)
    AC_SUBST(PMIX_V2_CPPFLAGS)
    AC_SUBST(PMIX_V2_LDFLAGS)

    if test $_x_ac_pmix_v1_found = 0 && test $_x_ac_pmix_v2_found = 0; then
      AC_MSG_WARN([unable to locate pmix installation])
    fi
  fi

  AM_CONDITIONAL(HAVE_PMIX, [test $_x_ac_pmix_v1_found = "1"] ||
                [test $_x_ac_pmix_v2_found = "1"])
  AM_CONDITIONAL(HAVE_PMIX_V1, [test $_x_ac_pmix_v1_found = "1"])
  AM_CONDITIONAL(HAVE_PMIX_V2, [test $_x_ac_pmix_v2_found = "1"])
])
