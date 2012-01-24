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
   AC_MSG_CHECKING([whether to enable IBM NRT switch support])
   nrt_default_dirs="/usr/lib"
   for nrt_dir in $nrt_default_dirs; do
      # skip dirs that don't exist
      if test ! -z "$nrt_dir" -a ! -d "$nrt_dir" ; then
         continue;
      fi

      if test "$OBJECT_MODE" = "64"; then
	 libnrt="nrt_64"
      else
         libnrt="nrt"
      fi

      # search for required NTBL API libraries
      if test -f "$nrt_dir/lib${libnrt}.so"; then
         ac_have_nrt="yes"
         NRT_LDFLAGS="-l$libnrt"
         break;
      fi

   done

   if test "x$ac_have_nrt" != "xyes" ; then
      AC_MSG_RESULT([no])
      AC_MSG_NOTICE([Cannot support IBM NRT switch without libnrt])        
   else
      AC_MSG_RESULT([yes])
      AC_DEFINE(HAVE_LIBNRT, 1, [define if you have libntbl.])
   fi

   AC_SUBST(NRT_LDFLAGS)
])
