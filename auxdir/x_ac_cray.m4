##*****************************************************************************
#  AUTHOR:
#    Morris Jette <jette1@llnl.gov>
#
#  SYNOPSIS:
#    X_AC_CRAY
#
#  DESCRIPTION:
#    Test for Cray XT and XE systems with 2-D/3-D interconnects.
#    Tests for required libraries (Native Cray systems only):
#    * libjob
#    * mySQL (relies on testing for mySQL presence earlier);
#    * libexpat, needed for XML-RPC calls to Cray's BASIL
#      (Batch Application  Scheduler Interface Layer) interface.
#    Tests for required libraries (non-Cray systems with a Cray network):
#    * libalpscomm_sn
#    * libalpscomm_cn
#    Tests for DataWarp files
#*****************************************************************************
#
# Copyright 2013 Cray Inc. All Rights Reserved.
#

AC_DEFUN([X_AC_CRAY],
[
  ac_have_native_cray="yes"
  ac_have_cray_network="no"
  ac_really_no_cray="no"

  AC_ARG_ENABLE(
    [cray-network],
    AS_HELP_STRING(--enable-cray-network,Run Slurm on a non-Cray system with a Cray network),
      [ case "$enableval" in
	yes) ac_have_cray_network="yes" ac_have_native_cray="no" ;;
	 no) ac_have_cray_network="no"  ;;
	  *) AC_MSG_ERROR([bad value "$enableval" for --enable-cray-network]) ;:
      esac ]
  )
  AC_ARG_ENABLE(
    [really-no-cray],
    AS_HELP_STRING(--enable-really-no-cray,Disable cray support for eslogin machines),
      [ case "$enableval" in
   yes) ac_really_no_cray="yes" ac_have_native_cray="no" ;;
    no) ac_really_no_cray="no"  ;;
     *) AC_MSG_ERROR([bad value "$enableval" for --enable-really-no-cray])  ;;
      esac ]
  )

  if test "$ac_really_no_cray" = "yes"; then
    AC_MSG_NOTICE([Ignoring any potential Cray system])
  else
    AC_MSG_CHECKING([whether this is a Cray system])

    _x_ac_cray_job_dir="job/default"
    _x_ac_cray_alpscomm_dir="alpscomm/default"

    _x_ac_cray_dirs="/opt/cray"
    AC_ARG_WITH(
      [cray_dir],
      AS_HELP_STRING(--with-cray_dir=PATH,Specify path to Cray file installation - /opt/cray by default),
      [AS_IF([test "x$with_cray_dir" != xno],[_x_ac_cray_dirs="$with_cray_dir $_x_ac_cray_dirs"])])

    for d in $_x_ac_cray_dirs; do
      test -d "$d" || continue

      if test "$ac_have_native_cray" = "yes"; then
        _test_dir="$d/$_x_ac_cray_job_dir"
        test -d "$_test_dir" || continue
        test -d "$_test_dir/include" || continue
        test -f "$_test_dir/include/job.h" || continue
        test -d "$_test_dir/lib64" || continue
        test -f "$_test_dir/lib64/libjob.so" || continue

        CRAY_JOB_CPPFLAGS="$CRAY_JOB_CPPFLAGS -I$_test_dir/include"
        if test "$ac_with_rpath" = "yes"; then
          CRAY_JOB_LDFLAGS="$CRAY_JOB_LDFLAGS -Wl,-rpath -Wl,$_test_dir/lib64 -L$_test_dir/lib64 -ljob"
        else
          CRAY_JOB_LDFLAGS="$CRAY_JOB_LDFLAGS -L$_test_dir/lib64 -ljob"
        fi
      fi

      _test_dir="$d/$_x_ac_cray_alpscomm_dir"
      test -d "$_test_dir" || continue
      test -d "$_test_dir/include" || continue
      test -f "$_test_dir/include/alpscomm_cn.h" || continue
      test -f "$_test_dir/include/alpscomm_sn.h" || continue
      test -d "$_test_dir/lib64" || continue
      test -f "$_test_dir/lib64/libalpscomm_cn.so" || continue
      test -f "$_test_dir/lib64/libalpscomm_sn.so" || continue

      CRAY_ALPSC_CN_CPPFLAGS="$CRAY_ALPSC_CN_CPPFLAGS -I$_test_dir/include"
      CRAY_ALPSC_SN_CPPFLAGS="$CRAY_ALPSC_SN_CPPFLAGS -I$_test_dir/include"
      if test "$ac_with_rpath" = "yes"; then
        CRAY_ALPSC_CN_LDFLAGS="$CRAY_ALPSC_CN_LDFLAGS -Wl,-rpath -Wl,$_test_dir/lib64 -L$_test_dir/lib64 -lalpscomm_cn"
        CRAY_ALPSC_SN_LDFLAGS="$CRAY_ALPSC_SN_LDFLAGS -Wl,-rpath -Wl,$_test_dir/lib64 -L$_test_dir/lib64 -lalpscomm_sn"
      else
        CRAY_ALPSC_CN_LDFLAGS="$CRAY_ALPSC_CN_LDFLAGS -L$_test_dir/lib64 -lalpscomm_cn"
        CRAY_ALPSC_SN_LDFLAGS="$CRAY_ALPSC_SN_LDFLAGS -L$_test_dir/lib64 -lalpscomm_sn"
      fi

      CRAY_SWITCH_CPPFLAGS="$CRAY_SWITCH_CPPFLAGS $CRAY_JOB_CPPFLAGS $CRAY_ALPSC_CN_CPPFLAGS $CRAY_ALPSC_SN_CPPFLAGS"
      CRAY_SWITCH_LDFLAGS="$CRAY_SWITCH_LDFLAGS $CRAY_JOB_LDFLAGS $CRAY_ALPSC_CN_LDFLAGS $CRAY_ALPSC_SN_LDFLAGS"
      CRAY_SELECT_CPPFLAGS="$CRAY_SELECT_CPPFLAGS $CRAY_ALPSC_SN_CPPFLAGS"
      CRAY_SELECT_LDFLAGS="$CRAY_SELECT_LDFLAGS $CRAY_ALPSC_SN_LDFLAGS"

      if test "$ac_have_native_cray" = "yes"; then
        CRAY_TASK_CPPFLAGS="$CRAY_TASK_CPPFLAGS $CRAY_ALPSC_CN_CPPFLAGS"
        CRAY_TASK_LDFLAGS="$CRAY_TASK_LDFLAGS $CRAY_ALPSC_CN_LDFLAGS"
      fi

      saved_CPPFLAGS="$CPPFLAGS"
      saved_LIBS="$LIBS"
      CPPFLAGS="$CRAY_JOB_CPPFLAGS $CRAY_ALPSC_CN_CPPFLAGS $CRAY_ALPSC_SN_CPPFLAGS $saved_CPPFLAGS"
      LIBS="$CRAY_JOB_LDFLAGS $CRAY_ALPSC_CN_LDFLAGS $CRAY_ALPSC_SN_LDFLAGS $saved_LIBS"

      if test "$ac_have_native_cray" = "yes"; then
        AC_LINK_IFELSE(
	  [AC_LANG_PROGRAM(
	    [[#include <job.h>
	      #include <alpscomm_sn.h>
	      #include <alpscomm_cn.h>
	    ]],
	    [[ job_getjidcnt();
	       alpsc_release_cookies((char **)0, 0, 0);
	       alpsc_flush_lustre((char **)0);
	    ]]
	  )],
	  [have_cray_files="yes"],
	  [AC_MSG_ERROR(There is a problem linking to the Cray API)])

       elif test "$ac_have_cray_network" = "yes"; then
        AC_LINK_IFELSE(
          [AC_LANG_PROGRAM(
            [[#include <alpscomm_sn.h>
             #include <alpscomm_cn.h>
            ]],
            [[
              alpsc_release_cookies((char **)0, 0, 0);
              alpsc_flush_lustre((char **)0);
            ]]
          )],
          [have_cray_files="yes"],
          [AC_MSG_ERROR(There is a problem linking to the Cray API)])
      fi

      LIBS="$saved_LIBS"
      CPPFLAGS="$saved_CPPFLAGS"

      break
    done

    if test -z "$have_cray_files"; then
      AC_MSG_RESULT([Unable to locate Cray APIs (usually in /opt/cray/alpscomm and /opt/cray/job)])
      ac_have_native_cray="no"
    else
      if test "$ac_have_native_cray" = "yes"; then
        AC_MSG_RESULT([Running on a Cray Aries system])
      elif test "$ac_have_cray_network" = "yes"; then
        AC_MSG_RESULT([Running on a system with a Cray Aries network])
      fi
    fi

    if test "$ac_have_native_cray" = "yes"; then
      ac_have_native_cray="yes"
      AC_DEFINE(HAVE_NATIVE_CRAY, 1, [Define to 1 for running on a Cray Aries system])
    elif test "$ac_have_cray_network" = "yes"; then
      ac_have_cray_network="yes"
      AC_DEFINE(HAVE_3D,            1, [Define to 1 if 3-dimensional architecture])
      AC_DEFINE(SYSTEM_DIMENSIONS,  3, [3-dimensional architecture])
      AC_DEFINE(HAVE_CRAY_NETWORK,  1, [Define to 1 for systems with a Cray Aries network])
    fi
  fi

  AM_CONDITIONAL(HAVE_NATIVE_CRAY, test "$ac_have_native_cray" = "yes")
  AM_CONDITIONAL(HAVE_CRAY_NETWORK, test "$ac_have_cray_network" = "yes")

  AC_SUBST(CRAY_JOB_CPPFLAGS)
  AC_SUBST(CRAY_JOB_LDFLAGS)
  AC_SUBST(CRAY_SELECT_CPPFLAGS)
  AC_SUBST(CRAY_SELECT_LDFLAGS)
  AC_SUBST(CRAY_SWITCH_CPPFLAGS)
  AC_SUBST(CRAY_SWITCH_LDFLAGS)
  AC_SUBST(CRAY_TASK_CPPFLAGS)
  AC_SUBST(CRAY_TASK_LDFLAGS)


  _x_ac_datawarp_dirs="/opt/cray/dws/default"
  _x_ac_datawarp_libs="lib64 lib"

  AC_ARG_WITH(
    [datawarp],
    AS_HELP_STRING(--with-datawarp=PATH,Specify path to DataWarp installation),
    [_x_ac_datawarp_dirs="$withval $_x_ac_datawarp_dirs"])

  AC_CACHE_CHECK(
    [for datawarp installation],
    [x_ac_cv_datawarp_dir],
    [
      for d in $_x_ac_datawarp_dirs; do
        test -d "$d" || continue
        test -d "$d/include" || continue
        test -f "$d/include/dws_thin.h" || continue
	for bit in $_x_ac_datawarp_libs; do
          test -d "$d/$bit" || continue
          test -f "$d/$bit/libdws_thin.so" || continue
          AS_VAR_SET(x_ac_cv_datawarp_dir, $d)
          break
        done
        test -n "$x_ac_cv_datawarp_dir" && break
      done
    ])

  if test -z "$x_ac_cv_datawarp_dir"; then
    AC_MSG_WARN([unable to locate DataWarp installation])
  else
    DATAWARP_CPPFLAGS="-I$x_ac_cv_datawarp_dir/include"
    if test "$ac_with_rpath" = "yes"; then
      DATAWARP_LDFLAGS="-Wl,-rpath -Wl,$x_ac_cv_datawarp_dir/$bit -L$x_ac_cv_datawarp_dir/$bit -ldws_thin"
    else
      DATAWARP_LDFLAGS="-L$x_ac_cv_datawarp_dir/$bit -ldws_thin"
    fi
    AC_DEFINE(HAVE_DATAWARP, 1, [Define to 1 if DataWarp library found])
  fi

  AC_SUBST(DATAWARP_CPPFLAGS)
  AC_SUBST(DATAWARP_LDFLAGS)
])
