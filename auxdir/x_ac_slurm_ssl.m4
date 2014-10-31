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

  ssl_default_dirs="/usr/local/openssl64 /usr/local/openssl /usr/lib/openssl    \
                    /usr/local/ssl /usr/lib/ssl /usr/local \
                    /usr/pkg /opt /opt/openssl /usr"
  
  AC_SUBST(SSL_LDFLAGS)
  AC_SUBST(SSL_LIBS)
  AC_SUBST(SSL_CPPFLAGS)
  
  SSL_LIB_TEST="-lcrypto"
  
  AC_ARG_WITH(ssl,
    AS_HELP_STRING(--with-ssl=PATH,Specify path to OpenSSL installation),
    [
  	tryssldir=$withval

  	# Hack around a libtool bug on AIX.
  	# libcrypto is in a non-standard library path on AIX (/opt/freeware
  	# which is specified with --with-ssl), and libtool is not setting
  	# the correct runtime library path in the binaries.
  	if test "x$ac_have_aix" = "xyes"; then
  		SSL_LIB_TEST="-lcrypto-static"
	elif test "x$ac_have_nrt" = "xyes"; then
		# it appears on p7 machines the openssl doesn't
		# link correctly so we need to add -ldl
		SSL_LIB_TEST="$SSL_LIB_TEST -ldl"
  	fi
    ])
  
  saved_LIBS="$LIBS"
  saved_LDFLAGS="$LDFLAGS"
  saved_CPPFLAGS="$CPPFLAGS"
  if test "x$prefix" != "xNONE" ; then
  	tryssldir="$tryssldir $prefix"
  fi

  if test "x$tryssldir" != "xno" ; then
    AC_CACHE_CHECK([for OpenSSL directory], ac_cv_openssldir, [
  	for ssldir in $tryssldir "" $ssl_default_dirs; do 
  		CPPFLAGS="$saved_CPPFLAGS"
  		LDFLAGS="$saved_LDFLAGS"
  		LIBS="$saved_LIBS $SSL_LIB_TEST"
  		
  		# Skip directories if they don't exist
  		if test ! -z "$ssldir" -a ! -d "$ssldir" ; then
  			continue;
  		fi
  		sslincludedir="$ssldir"
  		if test ! -z "$ssldir"; then
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
				sslincludedir="$ssldir/include"
  				CPPFLAGS="-I$ssldir/include $saved_CPPFLAGS"
  			else
  				CPPFLAGS="-I$ssldir $saved_CPPFLAGS"
  			fi
  		fi
		test -f "$sslincludedir/openssl/rand.h" || continue
		test -f "$sslincludedir/openssl/hmac.h" || continue
		test -f "$sslincludedir/openssl/sha.h" || continue

  		# Basic test to check for compatible version and correct linking
  		AC_RUN_IFELSE([AC_LANG_SOURCE([[
  #include <stdlib.h>
  #include <openssl/rand.h>
  #include <openssl/hmac.h>
  #include <openssl/sha.h>
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
  				ac_have_openssl="yes"
  				break;
  			],[
  		],[])
  
  		if test ! -z "$ac_have_openssl" ; then
  			break;
  		fi
  	done
  
	if test ! -z "$ac_have_openssl" ; then
		ac_cv_openssldir=$ssldir
	fi
    ])
  fi

  if test ! -z "$ac_have_openssl" ; then
    SSL_LIBS="$SSL_LIB_TEST"
    AC_DEFINE(HAVE_OPENSSL, 1, [define if you have openssl.])
    if (test ! -z "$ac_cv_openssldir") ; then
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
  else
    SSL_LIBS=""
    AC_MSG_WARN([could not find working OpenSSL library])
  fi
  
  LIBS="$saved_LIBS"
  CPPFLAGS="$saved_CPPFLAGS"
  LDFLAGS="$saved_LDFLAGS"
  
])dnl AC_SLURM_WITH_SSL
  
  
