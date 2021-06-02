##*****************************************************************************
# AUTHOR:
#	Written by Alejandro Sanchez - alex@schedmd.com
#
# SYNOPSIS:
#	X_AC_SYSTEMD
#
# DESCRIPTION:
#	Determine systemd presence
#	Substitute SYSTEMD_TASKSMAX_OPTION output var if systemd version >= 227
##*****************************************************************************

AC_DEFUN([X_AC_SYSTEMD],
[

	AC_CACHE_CHECK([for systemd presence],
			[_cv_systemd_presence],
			[PKG_CHECK_EXISTS([systemd],
					  [_cv_systemd_presence=yes],
					  [_cv_systemd_presence=no])])

	if [test "x$_cv_systemd_presence" != "xno"]; then
	    AC_DEFINE([HAVE_SYSTEMD],
		      [1],
		      [Define systemd presence])

	    SYSTEMD_TASKSMAX_OPTION=""
	    $PKG_CONFIG --atleast-version 227 systemd
	    if [test "$?" -eq 0]; then
		    SYSTEMD_TASKSMAX_OPTION="TasksMax=infinity"
	    fi
	    AC_SUBST(SYSTEMD_TASKSMAX_OPTION)

	fi

	# Adding a --with-systemdsystemunitdir option.
	# https://www.freedesktop.org/software/systemd/man/daemon.html#Installing%20Systemd%20Service%20Files
	AC_ARG_WITH([systemdsystemunitdir],
		    [AS_HELP_STRING([--with-systemdsystemunitdir=DIR],
				    [Directory for systemd service files])],,
		    [with_systemdsystemunitdir=no])

	AS_IF([test "x$with_systemdsystemunitdir" = "xyes"],
	      [def_systemdsystemunitdir=$($PKG_CONFIG --variable=systemdsystemunitdir systemd)
	      AS_IF([test "x$def_systemdsystemunitdir" = "x"],
		    [AC_MSG_ERROR([systemd support requested but pkg-config unable to query systemd package])])])

	AS_IF([test "x$with_systemdsystemunitdir" != "xno"],
	      [AC_SUBST([systemdsystemunitdir], [$with_systemdsystemunitdir])])

	AM_CONDITIONAL([WITH_SYSTEMD_UNITS],
		       [test "x$with_systemdsystemunitdir" != "xno"])
])
