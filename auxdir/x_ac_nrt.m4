##*****************************************************************************
#  AUTHOR:
#    Morris Jette <jette@schedmd.com>
#
#  SYNOPSIS:
#    AC_NRT
#
#  DESCRIPTION:
#    Checks for availability of the libraries necessary to support
#     IBM NRT (Network Resource Table) switch management
#
#  WARNINGS:
#    This macro must be placed after AC_PROG_CC or equivalent.
##*****************************************************************************


AC_DEFUN([X_AC_NRT],
[
   if test "$OBJECT_MODE" = "64"; then
      nrt_default_dirs="/usr/lib64"
   else
      nrt_default_dirs="/usr/lib"
   fi

   AC_ARG_WITH([libnrt], AS_HELP_STRING(--with-libnrt=PATH,Specify path to libnrt.so), [ nrt_default_dirs="$withval $nrt_default_dirs"])

   AC_MSG_CHECKING([whether to enable IBM NRT support])
   for nrt_dir in $nrt_default_dirs; do
      # skip dirs that don't exist
      if test ! -z "$nrt_dir" -a ! -d "$nrt_dir" ; then
         continue;
      fi
      # search for required NRT API libraries
      if test -f "$nrt_dir/libnrt.so"; then
         ac_have_nrt="yes"
         NRT_LDFLAGS="-lnrt"
	 break;
      fi

   done

   if test "x$ac_have_nrt" != "xyes" ; then
      AC_MSG_RESULT([no])
      AC_MSG_NOTICE([Cannot support IBM NRT API without libnrt.])
   else
      AC_MSG_RESULT([yes])
      AC_DEFINE(HAVE_LIBNRT, 1, [define if you have libnrt.])
   fi

   AC_SUBST(NRT_LDFLAGS)
])
