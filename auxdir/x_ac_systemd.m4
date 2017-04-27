##*****************************************************************************
# AUTHOR:
#	Written by Alejandro Sanchez - alex@schedmd.com
#
# SYNOPSIS:
#	X_AC_SYSTEMD
#
# DESCRIPTION:
#	Determine systemd presence
#	Determine systemd version
#	Determine systemd system unit dir
##*****************************************************************************

AC_DEFUN([X_AC_SYSTEMD],
[

	AC_CACHE_CHECK([for systemd presence],
			[_cv_systemd_presence],
			[PKG_CHECK_EXISTS([systemd],
					[_cv_systemd_presence=yes],
					[_cv_systemd_presence=no])])

	if [ test "x$_cv_systemd_presence" != "xno" ]; then
		AC_DEFINE([HAVE_SYSTEMD],
			[1],
			[Define systemd presence])

		AC_CACHE_CHECK([for systemd version],
			[_cv_systemd_version],
			[AS_IF([_cv_systemd_version=$($PKG_CONFIG --modversion systemd 2>/dev/null)],
				[],
				[_cv_systemd_version=no])])

		AS_IF([test "x$_cv_systemd_version" != "xno"],
			[AC_DEFINE([SYSTEMD_VERSION],
				[$_cv_systemd_version],
				[Define systemd version identified])]

			[AM_CONDITIONAL([HAVE_SYSTEMD_TASKSMAX],
				[ test -n "$_cv_systemd_version" && test "$_cv_systemd_version" -ge 227 ])]

			[AM_COND_IF([HAVE_SYSTEMD_TASKSMAX],
				[AC_SUBST([SYSTEMD_TASKSMAX_OPTION], ["TasksMax=infinity"])],
				[AC_SUBST([SYSTEMD_TASKSMAX_OPTION], [""])])])

		# In the future we might want to enable the configure option
		#  --with-systemdsystemunitdir=DIR, so that users can specify
		# at configure time the directory to install the .service files.
		# https://www.freedesktop.org/software/systemd/man/daemon.html#Installing%20Systemd%20Service%20Files

		#AC_CACHE_CHECK([for systemd system unit dir],
		#		[_cv_systemd_systemunitdir],
		#		[PKG_CHECK_VAR([SYSTEMD_SYSTEM_UNIT_DIR],
		#				[systemd],
		#				[systemdsystemunitdir],
		#				[_cv_systemd_systemunitdir=$SYSTEMD_SYSTEM_UNIT_DIR],
		#				[_cv_systemd_systemunitdir=no])])
	fi

])
