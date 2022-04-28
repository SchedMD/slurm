##*****************************************************************************
#  AUTHOR:
#    Artem Polyakov <artpol84@gmail.com>
#    Ralph Castain <ralph.h.castain@intel.com>
#    Isaias Compres <isaias.compres@tum.de>
#
#  SYNOPSIS:
#    X_AC_PMIX
#
#  DESCRIPTION:
#    Determine if the PMIx libraries exists. Derived from "x_ac_hwloc.m4".
##*****************************************************************************

AC_DEFUN([X_AC_PMIX],
[
  _x_ac_pmix_dirs="/usr /usr/local /usr/lib/x86_64-linux-gnu/pmix /usr/lib/x86_64-linux-gnu/pmix2"
  _x_ac_pmix_libs="lib64 lib"

  _x_ac_pmix_found="0"
  _x_ac_pmix_v2_found="0"
  _x_ac_pmix_v3_found="0"
  _x_ac_pmix_v4_found="0"
  _x_ac_pmix_v5_found="0"

  AC_ARG_WITH(
    [pmix],
    AS_HELP_STRING(--with-pmix=PATH,Specify path to pmix installation(s).  Multiple version directories can be ':' delimited.),
    [AS_IF([test "x$with_pmix" != xno && test "x$with_pmix" != xyes],
           [_x_ac_pmix_dirs="`echo $with_pmix | sed "s/:/ /g"`"])])

  if [test "x$with_pmix" = xno]; then
    AC_MSG_WARN([support for pmix disabled])
  else
    AC_CACHE_CHECK(
      [for pmix installation],
      [x_ac_cv_pmix_dir],
      [
        if [test "x$with_pmix" = xyes]; then
          $PKG_CONFIG pmix
          if [test "$?" -eq 0]; then
            # this is really just to determine if the pkg-config output is useful
            CPPFLAGS_tmp="$($PKG_CONFIG --cflags-only-I pmix)"
            pmix_prefix="$($PKG_CONFIG --variable=prefix pmix)"
            if [test "x$CPPFLAGS_tmp" != x && test "x$pmix_prefix" != x] ; then
              _x_ac_pmix_dirs="$pmix_prefix"
            fi
          fi
        fi
        for d in $_x_ac_pmix_dirs; do
          if [ ! test -d "$d/include" ] || [ ! test -f "$d/include/pmix_server.h" ] ||
		[ ! test -f "$d/include/pmix/pmix_common.h" && ! test -f $d/include/pmix_common.h ]; then
		if [ test -n "$with_pmix" && test "$with_pmix" != yes ]; then
			AC_MSG_ERROR([No PMIX installation found in $d])
		fi
		continue
	  fi
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
              #include <pmix_version.h>
              #if (PMIX_VERSION_MAJOR != 5L)
                #error "not version 5"
              #endif
            ], [ ] )],
            [ _x_ac_pmix_version="5" ],
            [ AC_PREPROC_IFELSE([AC_LANG_PROGRAM([
              #include <pmix_version.h>
              #if (PMIX_VERSION_MAJOR != 4L)
                #error "not version 4"
              #endif
            ], [ ] )],
            [ _x_ac_pmix_version="4" ],
	    [ AC_PREPROC_IFELSE([AC_LANG_PROGRAM([
              #include <pmix_version.h>
              #if (PMIX_VERSION_MAJOR != 3L)
                #error "not version 3"
              #endif
            ], [ ] )],
            [ _x_ac_pmix_version="3" ],
	    [ AC_PREPROC_IFELSE([AC_LANG_PROGRAM([
              #include<pmix_server.h>
              #if (PMIX_VERSION_MAJOR != 2L)
                #error "not version 2"
              #endif
            ], [ ] )],
            [ _x_ac_pmix_version="2" ] )
            ])
            ])
            ])

            CPPFLAGS="$_x_ac_pmix_cppflags_save"
            LIBS="$_x_ac_pmix_libs_save"

            m4_define([err_pmix],[was already found in one of the previous paths])

            if [test "$_x_ac_pmix_version" = "2"]; then
              if [test "$_x_ac_pmix_v2_found" = "1" ]; then
                m4_define([err_pmix_v2],[error processing $x_ac_cv_pmix_libdir: PMIx v2.x])
                AC_MSG_ERROR(err_pmix_v2 err_pmix)
              fi
              _x_ac_pmix_found="1"
              _x_ac_pmix_v2_found="1"
              PMIX_V2_CPPFLAGS="-I$x_ac_cv_pmix_dir/include"
              if test "$ac_with_rpath" = "yes"; then
                PMIX_V2_LDFLAGS="-Wl,-rpath -Wl,$x_ac_cv_pmix_libdir -L$x_ac_cv_pmix_libdir"
              else
                PMIX_V2_CPPFLAGS=$PMIX_V2_CPPFLAGS" -DPMIXP_V2_LIBPATH=\\\"$x_ac_cv_pmix_libdir\\\""
              fi
              # We don't want to search the other lib after we found it in
              # one place or we might report a false duplicate if lib64 is a
              # symlink of lib.
              break
            fi

            if [test "$_x_ac_pmix_version" = "3"]; then
              if [test "$_x_ac_pmix_v3_found" = "1" ]; then
                m4_define([err_pmix_v3],[error processing $x_ac_cv_pmix_libdir: PMIx v3.x])
                AC_MSG_ERROR(err_pmix_v3 err_pmix)
              fi
              _x_ac_pmix_found="1"
              _x_ac_pmix_v3_found="1"
              PMIX_V3_CPPFLAGS="-I$x_ac_cv_pmix_dir/include"
              if test "$ac_with_rpath" = "yes"; then
                PMIX_V3_LDFLAGS="-Wl,-rpath -Wl,$x_ac_cv_pmix_libdir -L$x_ac_cv_pmix_libdir"
              else
                PMIX_V3_CPPFLAGS=$PMIX_V3_CPPFLAGS" -DPMIXP_V3_LIBPATH=\\\"$x_ac_cv_pmix_libdir\\\""
              fi
              # We don't want to search the other lib after we found it in
              # one place or we might report a false duplicate if lib64 is a
              # symlink of lib.
              break
            fi

            if [test "$_x_ac_pmix_version" = "4"]; then
              if [test "$_x_ac_pmix_v4_found" = "1" ]; then
                m4_define([err_pmix_v4],[error processing $x_ac_cv_pmix_libdir: PMIx v4.x])
                AC_MSG_ERROR(err_pmix_v4 err_pmix)
              fi
              _x_ac_pmix_found="1"
              _x_ac_pmix_v4_found="1"
              PMIX_V4_CPPFLAGS="-I$x_ac_cv_pmix_dir/include"
              if test "$ac_with_rpath" = "yes"; then
                PMIX_V4_LDFLAGS="-Wl,-rpath -Wl,$x_ac_cv_pmix_libdir -L$x_ac_cv_pmix_libdir"
              else
                PMIX_V4_CPPFLAGS=$PMIX_V4_CPPFLAGS" -DPMIXP_V4_LIBPATH=\\\"$x_ac_cv_pmix_libdir\\\""
              fi
              # We don't want to search the other lib after we found it in
              # one place or we might report a false duplicate if lib64 is a
              # symlink of lib.
              break
            fi

            if [test "$_x_ac_pmix_version" = "5"]; then
              if [test "$_x_ac_pmix_v5_found" = "1" ]; then
                m4_define([err_pmix_v5],[error processing $x_ac_cv_pmix_libdir: PMIx v5.x])
                AC_MSG_ERROR(err_pmix_v5 err_pmix)
              fi
              _x_ac_pmix_found="1"
              _x_ac_pmix_v5_found="1"
              PMIX_V5_CPPFLAGS="-I$x_ac_cv_pmix_dir/include"
              if test "$ac_with_rpath" = "yes"; then
                PMIX_V5_LDFLAGS="-Wl,-rpath -Wl,$x_ac_cv_pmix_libdir -L$x_ac_cv_pmix_libdir"
              else
                PMIX_V5_CPPFLAGS=$PMIX_V5_CPPFLAGS" -DPMIXP_V5_LIBPATH=\\\"$x_ac_cv_pmix_libdir\\\""
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

    AC_SUBST(PMIX_V2_CPPFLAGS)
    AC_SUBST(PMIX_V2_LDFLAGS)
    AC_SUBST(PMIX_V3_CPPFLAGS)
    AC_SUBST(PMIX_V3_LDFLAGS)
    AC_SUBST(PMIX_V4_CPPFLAGS)
    AC_SUBST(PMIX_V4_LDFLAGS)
    AC_SUBST(PMIX_V5_CPPFLAGS)
    AC_SUBST(PMIX_V5_LDFLAGS)

    if test $_x_ac_pmix_found = 0; then
      if test -z "$with_pmix"; then
        AC_MSG_WARN([unable to locate pmix installation])
      else
        AC_MSG_ERROR([unable to locate pmix installation])
      fi
    fi
  fi

  AM_CONDITIONAL(HAVE_PMIX, [test $_x_ac_pmix_found = "1"] )
  AM_CONDITIONAL(HAVE_PMIX_V2, [test $_x_ac_pmix_v2_found = "1"])
  AM_CONDITIONAL(HAVE_PMIX_V3, [test $_x_ac_pmix_v3_found = "1"])
  AM_CONDITIONAL(HAVE_PMIX_V4, [test $_x_ac_pmix_v4_found = "1"])
  AM_CONDITIONAL(HAVE_PMIX_V5, [test $_x_ac_pmix_v5_found = "1"])
])
