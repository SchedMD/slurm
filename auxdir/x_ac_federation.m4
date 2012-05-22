##*****************************************************************************
## $Id$
##*****************************************************************************
#  AUTHOR:
#    Jason King <jking@llnl.gov>
#
#  SYNOPSIS:
#    AC_FEDERATION
#
#  DESCRIPTION:
#    Checks for availability of the libraries necessary to support
#     communication via User Space over the Federation switch.
#
#  WARNINGS:
#    This macro must be placed after AC_PROG_CC or equivalent.
##*****************************************************************************


AC_DEFUN([X_AC_FEDERATION],
[
   AC_MSG_CHECKING([whether to enable AIX Federation switch support])
   ntbl_default_dirs="/usr/lib"
   for ntbl_dir in $ntbl_default_dirs; do
      # skip dirs that don't exist
      if test ! -z "$ntbl_dir" -a ! -d "$ntbl_dir" ; then
         continue;
      fi

      if test "$OBJECT_MODE" = "64"; then
	 libntbl="ntbl_64"
      else
         libntbl="ntbl"
      fi

      # search for required NTBL API libraries
      if test -f "$ntbl_dir/lib${libntbl}.so"; then
         ac_have_federation="yes"
         FEDERATION_LDFLAGS="-l$libntbl"
         break;
      fi

   done

   if test "x$ac_have_federation" != "xyes" ; then
      AC_MSG_RESULT([no])
      AC_MSG_NOTICE([Cannot support Federation without libntbl])
   else
      AC_MSG_RESULT([yes])
      AC_DEFINE(HAVE_LIBNTBL, 1, [define if you have libntbl.])
   fi

   AC_SUBST(FEDERATION_LDFLAGS)
])
