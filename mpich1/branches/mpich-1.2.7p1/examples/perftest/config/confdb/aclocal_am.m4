dnl
dnl Automake updates
dnl
dnl
dnl AM_IGNORE is an extension that tells (a patched) automake not to
dnl include the specified AC_SUBST variable in the Makefile.in that
dnl automake generates.  We don't use AC_DEFUN, since aclocal will 
dnl then complain that AM_IGNORE is a duplicate (if you are using the
dnl patched automake/aclocal).
ifdef([AM_IGNORE],,[
define([AM_IGNORE],)])
dnl
dnl You can use PAC_PROVIDE_IGNORE to ensure that AM_IGNORE is defined
dnl for configures that don't use automake
AC_DEFUN([PAC_PROVIDE_IGNORE],[
ifdef([AM_IGNORE],,[
define([AM_IGNORE],)])
])
