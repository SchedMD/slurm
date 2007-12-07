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
   AC_MSG_CHECKING([for PERL site dir])

   perl_dir=`perl -MConfig -e 'print $Config{archlib};'`

   AC_ARG_WITH([site-perl],
    AS_HELP_STRING(--with-site-perl=PATH,specify path to site perl directory),
    [ perl_dir=$withval ]
   )

   ac_perl='Not Found'
   ac_have_perl_core="no"
   if test -d "$perl_dir/CORE" ; then
         ac_perl=$perl_dir
	 ac_have_perl_core="yes"
         AC_DEFINE_UNQUOTED(PERL_SITE_DIR, "$perl_dir", [Define location of PERL directory])
         AC_DEFINE_UNQUOTED(PERL_CORE_DIR, "$perl_dir/CORE", [Define location of PERL CORE directory])
   fi
   

   AC_MSG_RESULT($ac_perl)
])

