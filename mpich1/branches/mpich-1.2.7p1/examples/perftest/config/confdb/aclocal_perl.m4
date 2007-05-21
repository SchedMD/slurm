dnl
dnl
dnl Find perl5
AC_DEFUN(PAC_PROG_PERL5,[
AC_CACHE_VAL(pac_cv_prog_perl,
[
AC_PATH_PROGS(PERL,perl5 perl)
if test -n "$PERL" ; then
    changequote(,)
    pac_perlversion=`$PERL -v | grep 'This is perl' | \
	sed -e 's/^.*version *\([0-9]\).*$/\1/'`
    changequote([,])
    # Should do a test first for ch_p4 etc.
    if test "$pac_perlversion" != 5 ; then
        AC_MSG_WARN([Perl version 5 was not found])
	AC_MSG_WARN([$PERL is version $pac_perlversion])
    fi
    AC_SUBST(PERL)
fi
pac_cv_prog_perl=$PERL
])dnl
])
