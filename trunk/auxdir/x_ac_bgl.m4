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
   bgl_default_dirs="/bgl/BlueLight/ppcfloor/bglsys /bgl/bglsched /home/bgdb2cli/sqllib"

   for bgl_dir in $bgl_default_dirs; do
      # Skip directories that don't exist
      if test ! -z "$bgl_dir" -a ! -d "$bgl_dir" ; then
         continue;
      fi

      # Search for required BGL API libraries in the directory
      if test -z "$have_bgl_ar" -a -f "$bgl_dir/lib/bglbootload.a" -a -f "$bgl_dir/lib/bglsp440supt.a" ; then
         if test ! -f "$bgl_dir/lib/libbglbridge.a" ; then
            # Establish a link as required. Libtool requires the "lib" prefix
            # to function properly. See 
            # "How to use --whole-archive arg with libtool"
            # http://www.mail-archive.com/libtool@gnu.org/msg02792.html
            AC_MSG_ERROR([$bgl_dir/lib/libbglbridge.a is required and does not exist])
         fi

         have_bgl_ar=yes
         BGL_LDFLAGS="$BGL_LDFLAGS -Wl,-rpath $bgl_dir/lib -Wl,-L$bgl_dir/lib -Wl,-whole-archive -Wl,-lbglbridge -Wl,-no-whole-archive $bgl_dir/lib/bglbootload.a $bgl_dir/lib/bglsp440supt.a -lbgldb -lbglmachine -ltableapi -lexpat -lbglsp"
      fi

      # Search for required DB2 library in the directory
      if test -z "$have_db2" -a -f "$bgl_dir/lib/libdb2.so" ; then
         have_db2=yes
         BGL_LDFLAGS="$BGL_LDFLAGS -Wl,-rpath $bgl_dir/lib -L$bgl_dir/lib -ldb2"
      fi

      # Search for headers in the directory
      if test -z "$have_bgl_hdr" -a -f "$bgl_dir/include/rm_api.h" ; then
         have_bgl_hdr=yes
         BGL_INCLUDES="-I$bgl_dir/include"
      fi
   done

   AC_SUBST(BGL_INCLUDES)
   AC_SUBST(BGL_LDFLAGS)

   if test ! -z "$have_bgl_ar" -a ! -z "$have_bgl_hdr" -a ! -z "$have_db2" ; then
      ac_with_readline="no"

      AC_DEFINE(HAVE_BGL, 1, [Define to 1 if emulating or running on Blue Gene system])
      AC_DEFINE(HAVE_BGL_FILES, 1, [Define to 1 if have Blue Gene files])
      AC_DEFINE(HAVE_FRONT_END, 1, [Define to 1 if running slurmd on front-end only])
   fi
])
