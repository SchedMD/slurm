##*****************************************************************************
## $Id$
##*****************************************************************************
#  AUTHOR:
#    Chris Dunlap <cdunlap@llnl.gov>
#
#  SYNOPSIS:
#    AC_GPL_LICENSED
#
#  DESCRIPTION:
#    Acknowledge being licensed under terms of the GNU General Public License.
##*****************************************************************************

AC_DEFUN([X_AC_GPL_LICENSED],
[
  AC_DEFINE([GPL_LICENSED], [1],
    [Define to 1 if licensed under terms of the GNU General Public License.]
  )
])
