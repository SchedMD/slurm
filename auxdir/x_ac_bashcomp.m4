##*****************************************************************************
#  AUTHOR:
#    Skyler Malinowski <skyler@schedmd.com>
#
#  SYNOPSIS:
#    X_AC_BASH_COMPLETION()
#
#  DESCRIPTION:
#    Set bashcompdir correctly.
##*****************************************************************************

AC_DEFUN([X_AC_BASH_COMPLETION], [
	AC_MSG_CHECKING([for bash-completion package])
	PKG_CHECK_VAR(
		[bashcompdir],
		[bash-completion],
		[completionsdir],
		[ac_have_bash_completion=yes],
		[ac_have_bash_completion=no],
	)

	if test "x$ac_have_bash_completion" != "xyes" ; then
		AC_MSG_WARN([unable to locate bash-completion package])
	fi
	AC_MSG_RESULT($ac_have_bash_completion)

	# Ref: https://github.com/scop/bash-completion/blob/main/README.md#faq
	AC_MSG_CHECKING([for bash-completion completionsdir path])
	if [ $PKG_CONFIG --atleast-version=2.12 bash-completion ]; then
		# For 'bash-completion >= 2.12':
		#   When the real location of the command is in the directory
		#   `<prefix>/bin` or `<prefix>/sbin`, the directory
		#   `<prefix>/share/bash-completion/completions` is considered.
		completionsdir="${datadir}/bash-completion/completions"
	elif test "x$prefix" = "xNONE" || test "x$prefix" = "x/usr/local"; then
		# PREFIX is default value; use a known supported directory.
		if test "x$ac_have_bash_completion" = "xyes"; then
			# PKG_CHECK_VAR:
			# pkg-config --variable=completionsdir bash-completion
			completionsdir="$bashcompdir"
		else
			# Default to known directory path.
			completionsdir="/usr/share/bash-completion/completions"
		fi
	else
		# Default to a permission safe directory.
		completionsdir="${datadir}/bash-completion/completions"
	fi
	AC_MSG_RESULT($completionsdir)

	AC_SUBST([bashcompdir], [$completionsdir])
])
