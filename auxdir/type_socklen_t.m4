##*****************************************************************************
## $Id$
##*****************************************************************************
#  AUTHOR:
#    Lars Brinkhoff <lars@nocrew.org>
#
#  SYNOPSIS:
#    TYPE_SOCKLEN_T
#
#  DESCRIPTION:
#    Check whether sys/socket.h defines type socklen_t. 
#    Please note that some systems require sys/types.h to be included 
#    before sys/socket.h can be compiled.
##*****************************************************************************

AC_DEFUN([TYPE_SOCKLEN_T],
[AC_CACHE_CHECK([for socklen_t], ac_cv_type_socklen_t,
[
  AC_COMPILE_IFELSE([AC_LANG_PROGRAM([[#include <sys/types.h>
   #include <sys/socket.h>]], [[socklen_t len = 42; return 0;]])],[ac_cv_type_socklen_t=yes],[ac_cv_type_socklen_t=no])
])

if test "$ac_cv_type_socklen_t" = "yes"; then
  AC_DEFINE([HAVE_SOCKLEN_T], [1], [Define if you have the socklen_t type.])
fi

AH_VERBATIM([HAVE_SOCKLEN_T_], 
[#ifndef HAVE_SOCKLEN_T
#  define HAVE_SOCKLEN_T
   typedef int socklen_t;
#endif])
])
