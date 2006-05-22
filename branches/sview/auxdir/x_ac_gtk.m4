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
    AC_PATH_PROG(HAVEPKGCONFIG, pkg-config, $PATH)

    if test ! -z "$HAVEPKGCONFIG"; then
        $HAVEPKGCONFIG --exists libglade-2.0 gtk+-2.0 
        if test $? -eq 0 ; then
            GTK2_CFLAGS=`$HAVEPKGCONFIG --cflags libglade-2.0 gtk+-2.0`
            GTK2_LIBS=`$HAVEPKGCONFIG --libs libglade-2.0 gtk+-2.0`
	    ac_have_gtk="yes"
            if test ! -z "GLADE_STATIC"  ; then
                GTK2_LIBS=`echo $GTK2_LIBS | sed "s/-lglade-2.0/$GLADE_STATIC -lglade-2.0 $BDYNAMIC/g"`
            fi
            GUI_LIBS="$GUI_LIBS $GTK2_LIBS"
			SETUP_GTK="setup.gtk2"
			GTK_SUBDIR=gtk2
			XMLVER="2"
	    AC_SUBST(GTK2_CFLAGS)
   	    AC_SUBST(GTK2_LIBS)
        else
            AC_MSG_ERROR([*** Either libglade-2.0 or gtk+-2.0 are not available.])
        fi
    else
        AC_MSG_ERROR([*** pkg-config not found. Cannot probe for libglade-2.0 or gtk+-2.0.])
    fi

])
