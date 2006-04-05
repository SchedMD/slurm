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
   ntbl_default_dirs="/usr/lib"
   for ntbl_dir in $ntbl_default_dirs; do
      # skip dirs that don't exist
      if test ! -z "$ntbl_dir" -a ! -d "$ntbl_dir" ; then
         continue;
      fi

      # search for required NTBL API libraries
      if test -f "$ntbl_dir/libntbl.a"; then
         ac_have_federation="yes"
         FEDERATION_LDFLAGS="$ntbl_dir/libntbl.a -lntbl"
         echo "checking for libntbl.a in $ntbl_dir... yes"
         break;
      fi

      echo "checking for libntbl.a in $ntbl_dir... no"
   done

   if test "x$ac_have_federation" != "xyes" ; then
      AC_MSG_NOTICE([Cannot support Federation without libntbl])        
   else
      AC_DEFINE(HAVE_LIBNTBL, 1, [define if you have libntbl.])
   fi

   AC_SUBST(FEDERATION_LDFLAGS)
])
