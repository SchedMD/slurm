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
   nrt_default_dirs="/usr/include"
   AC_ARG_WITH([nrth], AS_HELP_STRING(--with-nrth=PATH,Parent directory of nrt.h and permapi.h), [ nrt_default_dirs="$withval $nrt_default_dirs"])
   AC_MSG_CHECKING([Checking NRT and PERMAPI header files])
   for nrt_dir in $nrt_default_dirs; do
      # skip dirs that don't exist
      if test ! -z "$nrt_dir" -a ! -d "$nrt_dir" ; then
         continue;
      fi
      # search for required NRT and PERMAPI header files
      if test -f "$nrt_dir/nrt.h" -a -f "$nrt_dir/permapi.h"; then
         ac_have_nrt_h="yes"
         NRT_CPPFLAGS="-I$nrt_dir"
         AC_DEFINE(HAVE_NRT_H, 1, [define if you have nrt.h])
         AC_DEFINE(HAVE_PERMAPI_H, 1, [define if you have permapi_h])
         break;
      fi
   done
   if test "x$ac_have_nrt_h" != "xyes" ; then
      AC_MSG_RESULT([no])
      AC_MSG_NOTICE([Cannot support IBM NRT without nrt.h and permapi.h])
   else
      AC_MSG_RESULT([yes])
   fi
   AC_SUBST(NRT_CPPFLAGS)


   nrt_default_dirs="/usr/lib64 /usr/lib"
   AC_ARG_WITH([libnrt], AS_HELP_STRING(--with-libnrt=PATH,Parent directory of libnrt.so), [ nrt_default_dirs="$withval $nrt_default_dirs"])
   AC_MSG_CHECKING([whether to enable IBM NRT support])
   for nrt_dir in $nrt_default_dirs; do
      # skip dirs that don't exist
      if test ! -z "$nrt_dir" -a ! -d "$nrt_dir" ; then
         continue;
      fi
      # search for required NRT API libraries
      if test -f "$nrt_dir/libnrt.so"; then
      	AC_DEFINE_UNQUOTED(LIBNRT_SO, "$nrt_dir/libnrt.so", [Define the libnrt.so location])
         ac_have_libnrt="yes"
	 break;
      fi
   done

   if test "x$ac_have_libnrt" != "xyes" ; then
      AC_MSG_RESULT([no])
   else
      AC_MSG_RESULT([yes])
   fi

   if test "x$ac_have_nrt_h" = "xyes"; then
      ac_have_nrt="yes"
   fi
   AM_CONDITIONAL(HAVE_NRT, test "x$ac_have_nrt" = "xyes")
   AC_SUBST(HAVE_NRT)
])
