dnl
dnl Add support for web/HTML needs
dnl/*D
dnl PAC_ARG_WWW - Add support for wwwdir to the configure command line
dnl
dnl Output Effects:
dnl Sets the variable 'wwwdir' to the specified directory; if no directory
dnl is given, it uses '${prefix}/www'.
dnl
dnl D*/
dnl 
AC_DEFUN(PAC_ARG_WWWDIR,[
AC_ARG_WITH([wwwdir],[
--with-wwwdir=directory - Specify the root directory for HTML documentation],
wwwdir=$withval,wwwdir='${prefix}/www')
AC_SUBST(wwwdir)])
dnl
dnl This will eventually include other tools
dnl
