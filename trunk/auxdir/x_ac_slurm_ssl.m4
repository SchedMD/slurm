##*****************************************************************************
## $Id$
##*****************************************************************************
#  AUTHOR:
#     Mark Grondona <mgrondona@llnl.gov>
#     (Mostly taken from OpenSSH configure.ac)
#
#  SYNOPSIS:
#    X_AC_SLURM_WITH_SSL
#
#  DESCRIPTION:
#    Process --with-ssl configure flag and search for OpenSSL support.
#
##*****************************************************************************

AC_DEFUN([X_AC_SLURM_WITH_SSL], [

  ac_slurm_with_ssl=no
  ssl_default_dirs="/usr/local/openssl64 /usr/local/openssl /usr/lib/openssl    \
                    /usr/local/ssl /usr/lib/ssl /usr/local \
                    /usr/pkg /opt /opt/openssl"
  
  AC_SUBST(SSL_LDFLAGS)
  AC_SUBST(SSL_LIBS)
  AC_SUBST(SSL_CPPFLAGS)
  
  SSL_LIBS="-lcrypto"
  
  AC_ARG_WITH(ssl,
    AS_HELP_STRING(--with-ssl=PATH,Specify path to OpenSSL installation),
    [
  	tryssldir=$withval

  	# Hack around a libtool bug on AIX.
  	# libcrypto is in a non-standard library path on AIX (/opt/freeware
  	# which is specified with --with-ssl), and libtool is not setting
  	# the correct runtime library path in the binaries.
  	if test "x$ac_have_aix" = "xyes"; then
  		SSL_LIBS="-lcrypto-static"
  	fi
    ])
  
  saved_LIBS="$LIBS"
  saved_LDFLAGS="$LDFLAGS"
  saved_CPPFLAGS="$CPPFLAGS"
  if test "x$prefix" != "xNONE" ; then
  	tryssldir="$tryssldir $prefix"
  fi
  if test "x$tryssldir" == "xno" ; then
     AC_MSG_ERROR([OpenSSL libary is required for SLURM operation, download from www.openssl.org])
  fi
  
  AC_CACHE_CHECK([for OpenSSL directory], ac_cv_openssldir, [
  	for ssldir in $tryssldir "" $ssl_default_dirs; do 
  		CPPFLAGS="$saved_CPPFLAGS"
  		LDFLAGS="$saved_LDFLAGS"
  		LIBS="$saved_LIBS $SSL_LIBS"
  		
  		# Skip directories if they don't exist
  		if test ! -z "$ssldir" -a ! -d "$ssldir" ; then
  			continue;
  		fi
  		if test ! -z "$ssldir" -a "x$ssldir" != "x/usr"; then
  			# Try to use $ssldir/lib if it exists, otherwise 
  			# $ssldir
  			if test -d "$ssldir/lib" ; then
  				LDFLAGS="-L$ssldir/lib $saved_LDFLAGS"
  				if test ! -z "$need_dash_r" ; then
  					LDFLAGS="-R$ssldir/lib $LDFLAGS"
  				fi
  			else
  				LDFLAGS="-L$ssldir $saved_LDFLAGS"
  				if test ! -z "$need_dash_r" ; then
  					LDFLAGS="-R$ssldir $LDFLAGS"
  				fi
  			fi
  			# Try to use $ssldir/include if it exists, otherwise 
  			# $ssldir
  			if test -d "$ssldir/include" ; then
  				CPPFLAGS="-I$ssldir/include $saved_CPPFLAGS"
  			else
  				CPPFLAGS="-I$ssldir $saved_CPPFLAGS"
  			fi
  		fi
  
  		# Basic test to check for compatible version and correct linking
  		AC_RUN_IFELSE([AC_LANG_SOURCE([[
  #include <stdlib.h>
  #include <openssl/rand.h>
  #define SIZE 8
  int main(void) 
  {
  	int a[SIZE], i;
	for (i=0; i<SIZE; i++)
		a[i] = rand();
  	RAND_add(a, sizeof(a), sizeof(a));
  	return(RAND_status() <= 0);
  }
  			]])],[
  				found_crypto=1
  				break;
  			],[
  		],[])
  
  		if test ! -z "$found_crypto" ; then
  			break;
  		fi
  	done
  
  	if test -z "$found_crypto" ; then
  		AC_MSG_ERROR([Could not find working OpenSSL library)]
		AC_MSG_ERROR([Download and install OpenSSL from http://www.openssl.org/])
  	fi
  	if test -z "$ssldir" ; then
  		ssldir="(system)"
  	fi
  
  	ac_cv_openssldir=$ssldir
  ])
  
  if (test ! -z "$ac_cv_openssldir" && test "x$ac_cv_openssldir" != "x(system)") ; then
  	dnl Need to recover ssldir - test above runs in subshell
  	ssldir=$ac_cv_openssldir
  	if test ! -z "$ssldir" -a "x$ssldir" != "x/usr"; then
  		# Try to use $ssldir/lib if it exists, otherwise 
  		# $ssldir
  		if test -d "$ssldir/lib" ; then
  			SSL_LDFLAGS="-L$ssldir/lib"
  		else
  			SSL_LDFLAGS="-L$ssldir"
  		fi
  		# Try to use $ssldir/include if it exists, otherwise 
  		# $ssldir
  		if test -d "$ssldir/include" ; then
  			SSL_CPPFLAGS="-I$ssldir/include"
  		else
  			SSL_CPPFLAGS="-I$ssldir"
  		fi
  	fi
  fi
  
  AC_LINK_IFELSE([AC_LANG_PROGRAM([[#include <openssl/evp.h>]], [[EVP_MD_CTX_cleanup(NULL);]])],[AC_DEFINE(HAVE_EVP_MD_CTX_CLEANUP, 1,
               [Define to 1 if function EVP_MD_CTX_cleanup exists.])],[])
  
  LIBS="$saved_LIBS"
  CPPFLAGS="$saved_CPPFLAGS"
  LDFLAGS="$saved_LDFLAGS"
  
])dnl AC_SLURM_WITH_SSL
  
  
