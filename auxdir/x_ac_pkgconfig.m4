##*****************************************************************************
#  AUTHOR:
#    Tim McMullan <mcmullan@schedmd.com>
#
#  SYNOPSIS:
#    X_AC_PKGCONFIG
#
#  DESCRIPTION:
#    Check if installing pkg-config file is desired.
##*****************************************************************************

AC_DEFUN([X_AC_PKGCONFIG],
[
  PKG_PROG_PKG_CONFIG([0.9.0])
  PKG_INSTALLDIR()
])
