##*****************************************************************************
## $Id: x_ac_perl.m4 7443 2006-03-08 20:23:25Z da $
##*****************************************************************************
#  AUTHOR:
#    Danny Auble <da@llnl.gov>
#
#  SYNOPSIS:
#    X_AC_PERL
#
#  DESCRIPTION:
#    Test for PERL, and perl core dir for perl.h. 
#    If found define HAVE_PERL and PERL_CORE_DIR.
#    Explicitly set path with --with-perl=PATH.
#
##*****************************************************************************

AC_DEFUN([X_AC_PERL],
[
   AC_MSG_CHECKING([perl usability])

   perl_dir=`perl -MConfig -e 'print $Config{archlib};'`

   AC_ARG_WITH([site-perl],
    AS_HELP_STRING(--with-site-perl=PATH,specify path to site perl directory),
    [ perl_dir=$withval ]
   )
   PERL_INCLUDES="`perl -MExtUtils::Embed -e ccopts`"
   PERL_LIBS="`perl -MExtUtils::Embed -e ldopts`"
 	
   ac_perl='Not Found'
   if test -d "$perl_dir/CORE" ; then
         ac_perl=$perl_dir
 	 save_LIBS="$LIBS"
   	 LIBS="$save_LIBS $PERL_LIBS"
 #  	 LIBS="$save_LIBS -L$perl_dir/CORE -lperl"
 	 save_CFLAGS="$CFLAGS"
   	 CFLAGS="$save_CFLAGS $PERL_INCLUDES"
 #	 CFLAGS="$save_CFLAGS -I$perl_dir/CORE"
	 AC_TRY_LINK([#include <EXTERN.h>
	    	      #include <perl.h>],
		      [PerlInterpreter * interp; interp=perl_alloc();],
		      [],[ac_perl="Not Found"])
         
        LIBS="$save_LIBS"
        CFLAGSS="$save_CFLAGS"
        if test "$ac_perl" != "Not Found"; then
            AC_MSG_RESULT([PERL test program built properly.])    
            AC_DEFINE_UNQUOTED(PERL_SITE_DIR, "$perl_dir", [Define location of PERL directory])
            AC_DEFINE_UNQUOTED(PERL_CORE_DIR, "$perl_dir/CORE", [Define location of PERL CORE directory])
	    AC_SUBST(PERL_INCLUDES);
	    AC_SUBST(PERL_LIBS);
	else
            AC_MSG_WARN([*** PERL test program execution failed.])
        fi	
  
         
   fi
 
])

