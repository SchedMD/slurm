##*****************************************************************************
## $Id: x_ac_gtk.m4 5401 2005-09-22 01:56:49Z morrone $
##*****************************************************************************
#  AUTHOR:
#    Danny Auble  <da@llnl.gov>
#
#  SYNOPSIS:
#    X_AC_GTK
#
#  DESCRIPTION:
#    Test for GTK. If found define 
##*****************************************************************************


AC_DEFUN([X_AC_GTK],
[
### Set to "no" if any test fails
    ac_have_gtk="yes"
    _x_ac_pkcfg_bin="no"

    # use the correct libs if running on 64bit
    if test -d "/usr/lib64/pkgconfig"; then
	    PKG_CONFIG_PATH="/usr/lib64/pkgconfig/:$PKG_CONFIG_PATH"
    fi

    if test -d "/opt/gnome/lib64/pkgconfig"; then
	    PKG_CONFIG_PATH="/opt/gnome/lib64/pkgconfig/:$PKG_CONFIG_PATH"
    fi 
 
### Check for pkg-config program
    AC_ARG_WITH(
	    [pkg-config],
	    AS_HELP_STRING(--with-pkg-config=PATH, 
		    Specify path to pkg-config binary),
	    [_x_ac_pkcfg_bin="$withval"])
    
    if test x$_x_ac_pkcfg_bin = xno; then
    	    AC_PATH_PROG(HAVEPKGCONFIG, pkg-config, no)
    else
   	    AC_PATH_PROG(HAVEPKGCONFIG, pkg-config, no, $_x_ac_pkcfg_bin)
    fi
    
    if test x$HAVEPKGCONFIG = xno; then
            AC_MSG_WARN([*** pkg-config not found. Cannot probe for gtk+-2.0.])
            ac_have_gtk="no"
    fi

### Check for min gtk package
    if test "$ac_have_gtk" == "yes" ; then
        $HAVEPKGCONFIG --exists gtk+-2.0
        if ! test $? -eq 0 ; then
            AC_MSG_WARN([*** gtk+-2.0 is not available.])
            ac_have_gtk="no"
	else
	    min_gtk_version="2.7.1"
	    $HAVEPKGCONFIG --atleast-version=$min_gtk_version gtk+-2.0
	    if ! test $? -eq 0 ; then
		    gtk_config_version=`$HAVEPKGCONFIG --modversion gtk+-2.0`
		    AC_MSG_WARN([*** gtk+-$gtk_config_version available, we need >= gtk+-$min_gtk_version installed for sview.])
		    ac_have_gtk="no"
	    fi
        fi
    fi

### Run a test program
    if test "$ac_have_gtk" == "yes" ; then
        GTK2_CFLAGS=`$HAVEPKGCONFIG --cflags gtk+-2.0 gthread-2.0`
        GTK2_LIBS=`$HAVEPKGCONFIG --libs gtk+-2.0 gthread-2.0`
        save_CFLAGS="$CFLAGS"
        save_LIBS="$LIBS"
        CFLAGS="$GTK2_CFLAGS $save_CFLAGS"
        LIBS="$GTK2_LIBS $save_LIBS"
        AC_TRY_LINK([
          #include <gtk/gtk.h>
        ],[
          int main()
          {
            (void) gtk_action_group_new ("MenuActions");
            (void) gtk_ui_manager_new ();
	    (void) gtk_cell_renderer_combo_new();	
          }
        ], , [ac_have_gtk="no"])
	CFLAGS="$save_CFLAGS"
        LIBS="$save_LIBS"
        if test "$ac_have_gtk" == "yes"; then
            AC_MSG_RESULT([GTK test program built properly.])
            AC_SUBST(GTK2_CFLAGS)
            AC_SUBST(GTK2_LIBS)
	    min_gtk_version="2.12.0"
	    $HAVEPKGCONFIG --atleast-version=$min_gtk_version gtk+-2.0
	    if ! test $? -eq 1 ; then
		    AC_DEFINE(GTK2_USE_TOOLTIP, 1, [Define to 1 if using gtk+-2.0 version 2.12.0 or higher])
	    fi
        else
            AC_MSG_WARN([*** GTK test program execution failed.])
        fi
    fi
])
