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
#    Tests for required libraries (ALPS Cray systems only):
#    * mySQL (relies on testing for mySQL presence earlier);
#    * libexpat, needed for XML-RPC calls to Cray's BASIL
#      (Batch Application  Scheduler Interface Layer) interface.
#    Tests for required libraries (non-Cray systems with a Cray network):
#    * libalpscomm_sn
#    * libalpscomm_cn
#*****************************************************************************
#
# Copyright 2013 Cray Inc. All Rights Reserved.
#

AC_DEFUN([X_AC_CRAY],
[
  ac_have_native_cray="no"
  ac_have_alps_cray="no"
  ac_have_real_cray="no"
  ac_have_alps_emulation="no"
  ac_have_alps_cray_emulation="no"
  ac_have_cray_network="no"

  AC_ARG_WITH(
    [alps-emulation],
    AS_HELP_STRING(--with-alps-emulation,Run SLURM against an emulated ALPS system - requires option cray.conf @<:@default=no@:>@),
    [test "$withval" = no || ac_have_alps_emulation=yes],
    [ac_have_alps_emulation=no])

  AC_ARG_ENABLE(
    [cray-emulation],
    AS_HELP_STRING(--enable-alps-cray-emulation,Run SLURM in an emulated Cray mode),
      [ case "$enableval" in
	yes) ac_have_alps_cray_emulation="yes" ;;
	 no) ac_have_alps_cray_emulation="no"  ;;
	  *) AC_MSG_ERROR([bad value "$enableval" for --enable-alps-cray-emulation])  ;;
      esac ]
  )

  AC_ARG_ENABLE(
    [native-cray],
    AS_HELP_STRING(--enable-native-cray,Run SLURM natively on a Cray without ALPS),
      [ case "$enableval" in
	yes) ac_have_native_cray="yes" ;;
	 no) ac_have_native_cray="no"  ;;
	  *) AC_MSG_ERROR([bad value "$enableval" for --enable-native-cray])  ;;
      esac ]
  )

  AC_ARG_ENABLE(
    [cray-network],
    AS_HELP_STRING(--enable-cray-network,Run SLURM on a non-Cray system with a Cray network),
      [ case "$enableval" in
	yes) ac_have_cray_network="yes" ;;
	 no) ac_have_cray_network="no"  ;;
	  *) AC_MSG_ERROR([bad value "$enableval" for --enable-cray-network]) ;:
      esac ]
  )

  if test "$ac_have_alps_emulation" = "yes"; then
    ac_have_alps_cray="yes"
    AC_MSG_NOTICE([Running A ALPS Cray system against an ALPS emulation])
    AC_DEFINE(HAVE_ALPS_EMULATION, 1, [Define to 1 if running against an ALPS emulation])

  elif test "$ac_have_alps_cray_emulation" = "yes"; then
    ac_have_alps_cray="yes"
    AC_MSG_NOTICE([Running in Cray emulation mode])
    AC_DEFINE(HAVE_ALPS_CRAY_EMULATION, 1, [Define to 1 for emulating a Cray XT/XE system using ALPS])

  elif test "$ac_have_native_cray" = "yes" || test "$ac_have_cray_network" = "yes" ; then
    _x_ac_cray_job_dir="job/default"
    _x_ac_cray_alpscomm_dir="alpscomm/default"

    _x_ac_cray_dirs="/opt/cray"

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
        CRAY_JOB_LDFLAGS="$CRAY_JOB_LDFLAGS -L$_test_dir/lib64 -ljob"
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
      CRAY_ALPSC_CN_LDFLAGS="$CRAY_ALPSC_CN_LDFLAGS -L$_test_dir/lib64 -lalpscomm_cn"
      CRAY_ALPSC_SN_LDFLAGS="$CRAY_ALPSC_SN_LDFLAGS -L$_test_dir/lib64 -lalpscomm_sn"

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
	  [AC_MSG_ERROR(There is a problem linking to the Cray api.)])

        # See if we have 5.2UP01 alpscomm functions
        AC_SEARCH_LIBS([alpsc_pre_suspend],
	  [alpscomm_cn],
	  [AC_DEFINE(HAVE_NATIVE_CRAY_GA, 1,
	  [Define to 1 if alpscomm functions new to CLE 5.2UP01 are defined])])

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
          [AC_MSG_ERROR(There is a problem linking to the Cray API.)])
      fi

      LIBS="$saved_LIBS"
      CPPFLAGS="$saved_CPPFLAGS"

      break
    done

    if test -z "$have_cray_files"; then
      AC_MSG_ERROR([Unable to locate Cray API dir install. (usually in /opt/cray)])
    else
      if test "$ac_have_native_cray" = "yes"; then
        AC_MSG_NOTICE([Running on a Cray system in native mode without ALPS])
      elif test "$ac_have_cray_network" = "yes"; then
        AC_MSG_NOTICE([Running on a system with a Cray network])
      fi
    fi

    if test "$ac_have_native_cray" = "yes"; then
      ac_have_real_cray="yes"
      ac_have_native_cray="yes"
      AC_DEFINE(HAVE_NATIVE_CRAY, 1, [Define to 1 for running on a Cray in native mode without ALPS])
      AC_DEFINE(HAVE_REAL_CRAY,   1, [Define to 1 for running on a real Cray system])
    elif test "$ac_have_cray_network" = "yes"; then
      ac_have_cray_network="yes"
      AC_DEFINE(HAVE_3D,            1, [Define to 1 if 3-dimensional architecture])
      AC_DEFINE(SYSTEM_DIMENSIONS,  3, [3-dimensional architecture])
      AC_DEFINE(HAVE_CRAY_NETWORK,  1, [Define to 1 for systems with a Cray network])
    fi

  else
    # Check for a Cray-specific file:
    #  * older XT systems use an /etc/xtrelease file
    #  * newer XT/XE systems use an /etc/opt/cray/release/xtrelease file
    #  * both have an /etc/xthostname
    AC_MSG_CHECKING([whether this is a Cray XT or XE system running on ALPS or ALPS simulator])

    if test -f /etc/xtrelease || test -d /etc/opt/cray/release; then
      ac_have_alps_cray="yes"
      ac_have_real_cray="yes"
    fi
    AC_MSG_RESULT([$ac_have_alps_cray])
  fi

  if test "$ac_have_alps_cray" = "yes"; then
    # libexpat is always required for the XML-RPC interface
    AC_CHECK_HEADER(expat.h, [],
		    AC_MSG_ERROR([Cray BASIL requires expat headers/rpm]))
    AC_CHECK_LIB(expat, XML_ParserCreate, [],
		 AC_MSG_ERROR([Cray BASIL requires libexpat.so (i.e. libexpat1-dev)]))

    if test "$ac_have_real_cray" = "yes"; then
      AC_CHECK_LIB([job], [job_getjid], [],
	      AC_MSG_ERROR([Need cray-job (usually in /opt/cray/job/default)]))
      AC_DEFINE(HAVE_REAL_CRAY, 1, [Define to 1 for running on a real Cray system])
    fi

    if test -z "$MYSQL_CFLAGS" || test -z "$MYSQL_LIBS"; then
      AC_MSG_ERROR([Cray BASIL requires the cray-MySQL-devel-enterprise rpm])
    fi

    # Used by X_AC_DEBUG to set default SALLOC_RUN_FOREGROUND value to 1
    x_ac_salloc_background=no

    AC_DEFINE(HAVE_3D,           1, [Define to 1 if 3-dimensional architecture])
    AC_DEFINE(SYSTEM_DIMENSIONS, 3, [3-dimensional architecture])
    AC_DEFINE(HAVE_FRONT_END,    1, [Define to 1 if running slurmd on front-end only])
    AC_DEFINE(HAVE_ALPS_CRAY,    1, [Define to 1 for Cray XT/XE systems using ALPS])
    AC_DEFINE(SALLOC_KILL_CMD,   1, [Define to 1 for salloc to kill child processes at job termination])
  fi

  AM_CONDITIONAL(HAVE_NATIVE_CRAY, test "$ac_have_native_cray" = "yes")
  AM_CONDITIONAL(HAVE_ALPS_CRAY, test "$ac_have_alps_cray" = "yes")
  AM_CONDITIONAL(HAVE_REAL_CRAY, test "$ac_have_real_cray" = "yes")
  AM_CONDITIONAL(HAVE_CRAY_NETWORK, test "$ac_have_cray_network" = "yes")
  AM_CONDITIONAL(HAVE_ALPS_EMULATION, test "$ac_have_alps_emulation" = "yes")
  AM_CONDITIONAL(HAVE_ALPS_CRAY_EMULATION, test "$ac_have_alps_cray_emulation" = "yes")

  AC_SUBST(CRAY_JOB_CPPFLAGS)
  AC_SUBST(CRAY_JOB_LDFLAGS)
  AC_SUBST(CRAY_SELECT_CPPFLAGS)
  AC_SUBST(CRAY_SELECT_LDFLAGS)
  AC_SUBST(CRAY_SWITCH_CPPFLAGS)
  AC_SUBST(CRAY_SWITCH_LDFLAGS)
  AC_SUBST(CRAY_TASK_CPPFLAGS)
  AC_SUBST(CRAY_TASK_LDFLAGS)

])
