##*****************************************************************************
## $Id$
##*****************************************************************************
#  AUTHOR:
#    Morris Jette <jette1@llnl.gov>
#
#  SYNOPSIS:
#    X_AC_BGL
#
#  DESCRIPTION:
#    Test for Blue Gene/L specific files. If found define HAVE_BGL
##*****************************************************************************


AC_DEFUN([X_AC_BGL],
[
   AC_SUBST(BGL_AR_LOC)
   AC_SUBST(BGL_HDR_LOC)

   bgl_default_dirs="/usr /usr/local /bgl /bgl/bglsched"

   for bgl_dir in $bgl_default_dirs; do
      # Skip directories that don't exist
      if test ! -z "$bgl_dir" -a ! -d "$bgl_dir" ; then
         continue;
      fi

      # Search for "bglbridge.a" in the directory
      if test -z "$have_bgl_ar" -a -f "$bgl_dir/bglbridge.a" ; then
         have_bgl_ar=yes
         BGL_AR_LOC="$bgl_dir"
      fi
      if test -z "$have_bgl_ar" -a -f "$bgl_dir/lib/bglbridge.a" ; then
         have_bgl_ar=yes
         BGL_AR_LOC="$bgl_dir/lib"
      fi

      # Search for "rm_api.h" in the directory
      if test -z "$have_bgl_hdr" -a -f "$bgl_dir/rm_api.h" ; then
         have_bgl_hdr=yes
         BGL_HDR_LOC="$bgl_dir"
      fi
      if test -z "$have_bgl_hdr" -a -f "$bgl_dir/include/rm_api.h" ; then
         have_bgl_hdr=yes
         BGL_HDR_LOC="$bgl_dir/include"
      fi
   done

   if test ! -z "$have_bgl_ar" -a ! -z "$have_bgl_hdr" ; then
      AC_DEFINE(HAVE_BGL, 1, [Define to 1 if running on Blue Gene L front end])
   fi
])
