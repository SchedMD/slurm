AC_DEFUN([X_AC_HLML],
[
  AC_ARG_WITH(
    [hlml],
    AS_HELP_STRING(--with-hlml, Build Habana Labs HLML-related code),
    []
  )

  if [test "x$with_hlml" = xno]; then
     AC_MSG_WARN([support for hlml disabled])
  else
    # /usr/include/habanalabs is the main location. Others are just in case
    hlml_includes="-I/usr/include/habanalabs"
    hlml_libs="-L/usr/lib/habanalabs"
    # Check for HLML header and library in the default locations
    AC_MSG_RESULT([])
    cppflags_save="$CPPFLAGS"
    ldflags_save="$LDFLAGS"
    CPPFLAGS="$hlml_includes $CPPFLAGS"
    LDFLAGS="$hlml_libs $LDFLAGS"
    AC_CHECK_HEADER([hlml.h], [ac_hlml_h=yes], [ac_hlml_h=no])
    AC_CHECK_LIB([hlml], [hlml_init], [ac_hlml=yes], [ac_hlml=no])
    CPPFLAGS="$cppflags_save"
    LDFLAGS="$ldflags_save"
    if test "$ac_hlml" = "yes" && test "$ac_hlml_h" = "yes"; then
      HLML_LIBS="-lhlml"
      HLML_CPPFLAGS="$hlml_includes"
      HLML_LDFLAGS="$hlml_libs"
      AC_DEFINE(HAVE_HLML, 1, [Define to 1 if HLML library found])
    else
      AC_MSG_WARN([unable to locate libhlml.so and/or hlml.h])
    fi
    AC_SUBST(HLML_LIBS)
    AC_SUBST(HLML_CPPFLAGS)
    AC_SUBST(HLML_LDFLAGS)
  fi
  AM_CONDITIONAL(BUILD_HLML, test "$ac_hlml" = "yes" && test "$ac_hlml_h" = "yes")
])
