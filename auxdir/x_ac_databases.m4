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
	_x_ac_mysql_bin=""
	### Check for mysql_config program
	AC_ARG_WITH(
		[mysql_config],
		AS_HELP_STRING(--with-mysql_config=PATH,
			Specify path of directory where mysql_config binary exists),
		[AS_IF([test "x$with_mysql_config" != xno && test "x$with_mysql_config" != xyes],
		       [_x_ac_mysql_bin="$with_mysql_config"])])

	if test -z "$_x_ac_mysql_bin"; then
		AC_PATH_PROGS(HAVEMYSQLCONFIG, [mysql_config mariadb_config], no)
	else
		AC_PATH_PROGS(HAVEMYSQLCONFIG, [mysql_config mariadb_config], no, $_x_ac_mysql_bin)
	fi

	if test x$with_mysql_config = xno; then
		AC_MSG_NOTICE([support for mysql is disabled])
	elif test x$HAVEMYSQLCONFIG = xno; then
		if test -z "$with_mysql_config"; then
			AC_MSG_WARN([*** mysql_config not found. Evidently no MySQL development libs installed on system.])
		else
			AC_MSG_ERROR([*** mysql_config not found. Evidently no MySQL development libs installed on system.])
		fi
	else
		# check for MySQL >= 8.0.21 or MariaDB >= 10.2.7
		mysql_version_string=`$HAVEMYSQLCONFIG --version`
		mysql_config_major_version=`echo "$mysql_version_string" | \
			sed -n 's/^\([[0-9]][[0-9]]*\)\..*/\1/p'`
		mysql_config_minor_version=`echo "$mysql_version_string" | \
			sed -n 's/^[[0-9]][[0-9]]*\.\([[0-9]][[0-9]]*\).*/\1/p'`
		mysql_config_micro_version=`echo "$mysql_version_string" | \
			sed -n 's/^[[0-9]][[0-9]]*\.[[0-9]][[0-9]]*\.\([[0-9]][[0-9]]*\).*/\1/p'`

		#
		# MariaDB moved to Connector/C, and thus -lmariadb, in 10.2.
		# MariaDB 10.0 and 10.1 still report -lmysqlclient, so also
		# treat a major version of exactly 10 as MariaDB. MySQL went
		# from 9.x straight to calendar versions (26.7 and later), so it
		# has never used and will not use a major version of 10.
		#
		ac_db_type="MySQL"
		ac_db_min_version="8.0.21"
		ac_db_min_numeric=80021
		ac_db_type_known="no"
		case `$HAVEMYSQLCONFIG --libs` in
		*-lmariadb*)
			ac_db_type="MariaDB"
			ac_db_type_known="yes"
			;;
		esac
		if test "x$mysql_config_major_version" = x10; then
			ac_db_type="MariaDB"
			ac_db_type_known="yes"
		fi
		#
		# MariaDB only overlaps with MySQL major version 5. After that
		# they no longer overlap. MariaDB went from 5 to 10.
		#
		if test "$mysql_config_major_version" -gt 5; then
			ac_db_type_known="yes"
		fi
		if test "$ac_db_type" = MariaDB; then
			ac_db_min_version="10.2.7"
			ac_db_min_numeric=100207
		fi

		#
		# A MariaDB below 10.0 reports -lmysqlclient and a bare version
		# with no vendor suffix, so it cannot be told apart from a MySQL
		# of the same version. Name both in that case rather than guess.
		#
		ac_db_label="$ac_db_type"
		ac_db_need="$ac_db_type-$ac_db_min_version"
		if test "$ac_db_type_known" = no; then
			ac_db_label="MySQL/MariaDB"
			ac_db_need="MySQL-8.0.21 or MariaDB-10.2.7"
		fi

		#
		# Report an unparsable version rather than silently treating it
		# as 0, which disabled MySQL support with a nonsense message.
		#
		ac_db_version_error=""
		if test -z "$mysql_config_major_version" ||
		   test -z "$mysql_config_minor_version" ||
		   test -z "$mysql_config_micro_version"; then
			ac_db_version_error="*** no version could be parsed out of the \"$mysql_version_string\" reported by $HAVEMYSQLCONFIG --version, we need >= $ac_db_need installed for the mysql interface."
		else
			mysql_numeric_version=`expr $mysql_config_major_version \* 10000 + $mysql_config_minor_version \* 100 + $mysql_config_micro_version`
			if test $mysql_numeric_version -lt $ac_db_min_numeric; then
				ac_db_version_error="*** $ac_db_label-$mysql_config_major_version.$mysql_config_minor_version.$mysql_config_micro_version available, we need >= $ac_db_need installed for the mysql interface."
			fi
		fi

		if test -n "$ac_db_version_error"; then
			if test -z "$with_mysql_config"; then
				AC_MSG_WARN([$ac_db_version_error])
			else
				AC_MSG_ERROR([$ac_db_version_error])
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
				AC_MSG_RESULT([$ac_db_type $mysql_config_major_version.$mysql_config_minor_version.$mysql_config_micro_version test program built properly.])
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
