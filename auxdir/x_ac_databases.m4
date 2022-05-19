##*****************************************************************************
#  AUTHOR:
#    Danny Auble  <da@llnl.gov>
#
#  SYNOPSIS:
#    X_AC_DATABASES
#
#  DESCRIPTION:
#    Test for Different Database apis. If found define appropriate ENVs.
##*****************************************************************************

AC_DEFUN([X_AC_DATABASES],
[
	#Check for MySQL
	ac_have_mysql="no"
	_x_ac_mysql_bin="no"
	### Check for mysql_config program
	AC_ARG_WITH(
		[mysql_config],
		AS_HELP_STRING(--with-mysql_config=PATH,
			Specify path of directory where mysql_config binary exists),
		[_x_ac_mysql_bin="$withval"])

	if test x$_x_ac_mysql_bin = xno; then
		AC_PATH_PROGS(HAVEMYSQLCONFIG, [mysql_config mariadb_config], no)
	else
		AC_PATH_PROGS(HAVEMYSQLCONFIG, [mysql_config mariadb_config], no, $_x_ac_mysql_bin)
	fi

	if test x$HAVEMYSQLCONFIG = xno; then
		AC_MSG_WARN([*** mysql_config not found. Evidently no MySQL development libs installed on system.])
	else
		# check for mysql-5.0.0+
		mysql_config_major_version=`$HAVEMYSQLCONFIG --version | \
			sed 's/\([[0-9]]*\).\([[0-9]]*\).\([[a-zA-Z0-9]]*\)/\1/'`
    		mysql_config_minor_version=`$HAVEMYSQLCONFIG --version | \
			sed 's/\([[0-9]]*\).\([[0-9]]*\).\([[a-zA-Z0-9]]*\)/\2/'`
    		mysql_config_micro_version=`$HAVEMYSQLCONFIG --version | \
			sed 's/\([[0-9]]*\).\([[0-9]]*\).\([[a-zA-Z0-9]]*\)/\3/'`

		if test $mysql_config_major_version -lt 5; then
			if test -z "$with_mysql_config"; then
				AC_MSG_WARN([*** mysql-$mysql_config_major_version.$mysql_config_minor_version.$mysql_config_micro_version available, we need >= mysql-5.0.0 installed for the mysql interface.])
			else
				AC_MSG_ERROR([*** mysql-$mysql_config_major_version.$mysql_config_minor_version.$mysql_config_micro_version available, we need >= mysql-5.0.0 installed for the mysql interface.])
			fi
			ac_have_mysql="no"
		else
		# mysql_config puts -I on the front of the dir.  We don't
		# want that so we remove it.
			MYSQL_CFLAGS=`$HAVEMYSQLCONFIG --include`
			MYSQL_LIBS=`$HAVEMYSQLCONFIG --libs_r`
			save_CFLAGS="$CFLAGS"
			save_LIBS="$LIBS"
       			CFLAGS="$MYSQL_CFLAGS $save_CFLAGS"
			LIBS="$MYSQL_LIBS $save_LIBS"
			AC_LINK_IFELSE(
				[AC_LANG_PROGRAM(
					 [[
					   #include <mysql.h>
					 ]],
					 [[
					   MYSQL mysql;
					   (void) mysql_init(&mysql);
					   (void) mysql_close(&mysql);
					 ]],
				)],
				[ac_have_mysql="yes"],
				[ac_have_mysql="no"])
			CFLAGS="$save_CFLAGS"
			LIBS="$save_LIBS"
			if test "$ac_have_mysql" = yes; then
				AC_MSG_RESULT([MySQL $mysql_config_major_version.$mysql_config_minor_version.$mysql_config_micro_version test program built properly.])
				AC_SUBST(MYSQL_LIBS)
				AC_SUBST(MYSQL_CFLAGS)
				AC_DEFINE(HAVE_MYSQL, 1, [Define to 1 if using MySQL libaries])
			else
				MYSQL_CFLAGS=""
				MYSQL_LIBS=""
				if test -z "$with_mysql_config"; then
					AC_MSG_WARN([*** MySQL test program execution failed. A thread-safe MySQL library is required.])
				else
					AC_MSG_ERROR([*** MySQL test program execution failed. A thread-safe MySQL library is required.])
				fi
			fi
		fi
      	fi
	AM_CONDITIONAL(WITH_MYSQL, test x"$ac_have_mysql" = x"yes")
])
