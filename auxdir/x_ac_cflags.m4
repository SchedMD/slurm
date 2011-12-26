##*****************************************************************************
## $Id: x_ac_cflags.m4 5401 2005-09-22 01:56:49Z morrone $
##*****************************************************************************
#  AUTHOR:
#    Danny Auble  <da@llnl.gov>
#
#  SYNOPSIS:
#    X_AC_CFLAGS
#
#  DESCRIPTION:
#    Add extra cflags
##*****************************************************************************


AC_DEFUN([X_AC_CFLAGS],
[
	# This is here to avoid a bug in the gcc compiler 3.4.6
	# Without this flag there is a bug when pointing to other functions
	# and then using them.  It is also advised to set the flag if there
	# are goto statements you may get better performance.
	if test "$GCC" = yes; then
		CFLAGS="$CFLAGS -fno-gcse"
	fi
])
