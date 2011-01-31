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

AC_DEFUN([X_AC_CRAY],
[
	x_ac_cray="no"
	ac_have_native_cray="no"

   	AC_ARG_ENABLE(cray-emulation, AS_HELP_STRING(--enable-cray-emulation, Run SLURM in Cray mode on a non-cray system),
	[ case "$enableval" in
	  yes) ac_have_cray=yes ;;
	  no)  ac_have_cray=no ;;
	  *)   AC_MSG_ERROR([bad value "$enableval" for --enable-cray-emulation])  ;;
    	esac ])

	if test "x$ac_have_cray" = "xyes"; then
		# FIXME: This doesn't do much just yet.
    		AC_MSG_NOTICE([Running in Cray emulation mode])
	else
		AC_MSG_CHECKING([whether this is a native Cray XT or XE system])
                # Check for a Cray-specific file:
                #  * older XT systems use an /etc/xtrelease file
                #  * newer XT/XE systems use an /etc/opt/cray/release/xtrelease file
                #  * both have an /etc/xthostname
		if test -f /etc/xtrelease  || test -d /etc/opt/cray/release ; then
			ac_have_native_cray="yes"
			# FIXME: This should be used to distinguish we are on 
			# a real system, It appears what HAVE_NATIVE_CRAY 
			# does now should probably be renamed to HAVE_CRAY 
			AC_DEFINE(HAVE_CRAY_FILES, 1, [Define to 1 for native Cray XT/XE system])
		fi
		AC_MSG_RESULT([$ac_have_cray])
	fi

	if test "$ac_have_cray" = "yes"; then
		if test -z "$MYSQL_CFLAGS" || test -z "$MYSQL_LIBS"; then
			AC_MSG_ERROR([BASIL requires the cray-MySQL-devel-enterprise rpm])
		fi

		AC_CHECK_HEADER(expat.h, [],
			AC_MSG_ERROR([BASIL requires expat headers/rpm]))
		AC_CHECK_LIB(expat, XML_ParserCreate, [],
			AC_MSG_ERROR([BASIL requires libexpat.so (i.e. libexpat1-dev)]))

		AC_DEFINE(HAVE_3D,           1, [Define to 1 if 3-dimensional architecture])
		AC_DEFINE(SYSTEM_DIMENSIONS, 3, [3-dimensional architecture])
		AC_DEFINE(HAVE_FRONT_END,    1, [Define to 1 if running slurmd on front-end only])
		AC_DEFINE(HAVE_NATIVE_CRAY,  1, [Define to 1 for native Cray XT/XE system])
	fi
	AM_CONDITIONAL(HAVE_NATIVE_CRAY, test "$ac_have_cray" = "yes")
])
