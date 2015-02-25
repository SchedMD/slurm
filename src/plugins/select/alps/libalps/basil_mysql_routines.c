/*
 * Database interaction routines for Cray XT/XE systems.
 *
 * Copyright (c) 2009-2011 Centro Svizzero di Calcolo Scientifico (CSCS)
 * Licensed under the GPLv2.
 */
#include "../basil_alps.h"

/** Read options from the appropriate my.cnf configuration file. */
static int cray_get_options_from_default_conf(MYSQL *handle)
{
	const char **path;
	/*
	 * Hardcoded list of paths my.cnf is known to exist on a Cray XT/XE
	 */
	const char *default_conf_paths[] = {
		"/etc/my.cnf",
		"/etc/opt/cray/MySQL/my.cnf",
		"/etc/mysql/my.cnf",
		"/root/.my.cnf",
		NULL
	};

	for (path = default_conf_paths; *path; path++)
		if (access(*path, R_OK) == 0)
			break;
	if (*path == NULL)
		fatal("no readable 'my.cnf' found");
	return  mysql_options(handle, MYSQL_READ_DEFAULT_FILE, *path);
}

/**
 * cray_connect_sdb - Connect to the XTAdmin database on the SDB host
 */
extern MYSQL *cray_connect_sdb(void)
{
	MYSQL *handle = mysql_init(NULL);

	if (handle == NULL)
		return NULL;

	if (cray_get_options_from_default_conf(handle) != 0) {
		error("can not get options from configuration file (%u) - %s",
		      mysql_errno(handle), mysql_error(handle));
		goto connect_failed;
	}

	if (mysql_real_connect(handle, cray_conf->sdb_host, cray_conf->sdb_user,
			       cray_conf->sdb_pass, cray_conf->sdb_db,
			       cray_conf->sdb_port, NULL, 0) == NULL) {
		error("can not connect to %s.%s (%u) - %s", cray_conf->sdb_host,
		      cray_conf->sdb_db, mysql_errno(handle),
		      mysql_error(handle));
		goto connect_failed;
	}

	return handle;

connect_failed:
	mysql_close(handle);
	return NULL;
}

/**
 * cray_is_gemini_system -  Figure out whether SeaStar (XT) or Gemini (XE)
 * @handle:	connected to sdb.XTAdmin database
 * Returns
 * -1 on error
 *  1 if on a Gemini system
 *  0 if on a SeaStar system
 */
int cray_is_gemini_system(MYSQL *handle)
{
	/*
	 * Rationale:
	 * - XT SeaStar systems have one SeaStar ASIC per node.
	 *   There are 4 nodes and 4 SeaStar ASICS on each blade, giving
	 *   4 distinct (X,Y,Z) coordinates per blade, so that the total
	 *   node count equals the total count of torus coordinates.
	 * - XE Gemini systems connect pairs of nodes to a Gemini chip.
	 *   There are 4 nodes on a blade and 2 Gemini chips. Nodes 0/1
	 *   are connected to Gemini chip 0, nodes 2/3 are connected to
	 *   Gemini chip 1. This configuration acts as if the nodes were
	 *   internally joined in Y dimension; hence there are half as
	 *   many (X,Y,Z) coordinates than there are nodes in the system.
	 * - Coordinates may be NULL if a network chip is deactivated.
	 */
	const char query[] =
		"SELECT COUNT(DISTINCT x_coord, y_coord, z_coord) < COUNT(*) "
		"FROM processor "
		"WHERE x_coord IS NOT NULL "
		"AND   y_coord IS NOT NULL "
		"AND   z_coord IS NOT NULL";
	MYSQL_BIND	result[1];
	signed char	answer;
	my_bool		is_null;
	my_bool		is_error;
	MYSQL_STMT	*stmt;

	memset(result, 0, sizeof(result));
	result[0].buffer_type	= MYSQL_TYPE_TINY;
	result[0].buffer	= (char *)&answer;
	result[0].is_null	= &is_null;
	result[0].error		= &is_error;

	stmt = prepare_stmt(handle, query, NULL, 0, result, 1);
	if (stmt == NULL)
		return -1;
	if (exec_stmt(stmt, query, result, 1) < 0)
		answer = -1;
	mysql_stmt_close(stmt);
	return answer;
}

/*
 *	Auxiliary routines for using prepared statements
 */

/**
 * validate_stmt_column_count - Validate column count of prepared statement
 * @stmt:	 prepared statement
 * @query:	 query text
 * @expect_cols: expected number of columns
 * Return true if ok.
 */
static bool validate_stmt_column_count(MYSQL_STMT *stmt, const char *query,
				       unsigned long expect_cols)
{
	unsigned long	column_count;
	MYSQL_RES	*result_metadata = mysql_stmt_result_metadata(stmt);

	/* Fetch result-set meta information */
	if (!result_metadata) {
		error("can not obtain statement meta "
		      "information for \"%s\": %s",
		      query, mysql_stmt_error(stmt));
		return false;
	}

	/* Check total column count of query */
	column_count = mysql_num_fields(result_metadata);
	if (column_count != expect_cols) {
		error("expected %lu columns for \"%s\", but got %lu",
		      expect_cols, query, column_count);
		mysql_free_result(result_metadata);
		return false;
	}

	/* Free the prepared result metadata */
	mysql_free_result(result_metadata);

	return true;
}

/**
 * prepare_stmt - Initialize and prepare a query statement.
 * @handle:	connected handle
 * @query:	query statement string to execute
 * @bind_parm:  values for unbound variables (parameters) in @query
 * @nparams:	length of @bind_parms
 * @bind_col:	typed array to contain the column results
 *		==> non-NULL 'is_null'/'error' fields are taken to mean
 *		    that NULL values/errors are not acceptable
 * @ncols:	number of expected columns (length of @bind_col)
 * Return prepared statement handle on success, NULL on error.
 */
MYSQL_STMT *prepare_stmt(MYSQL *handle, const char *query,
			 MYSQL_BIND bind_parm[], unsigned long nparams,
			 MYSQL_BIND bind_col[], unsigned long ncols)
{
	MYSQL_STMT	*stmt;
	unsigned long	param_count;

	if (query == NULL || *query == '\0')
		return NULL;

	/* Initialize statement (fails only if out of memory). */
	stmt = mysql_stmt_init(handle);
	if (stmt == NULL) {
		error("can not allocate handle for \"%s\"", query);
		return NULL;
	}

	if (mysql_stmt_prepare(stmt, query, strlen(query))) {
		error("can not prepare statement \"%s\": %s",
		      query, mysql_stmt_error(stmt));
		goto prepare_failed;
	}

	/* Verify the parameter count */
	param_count = mysql_stmt_param_count(stmt);
	if (nparams != nparams) {
		error("expected %lu parameters for \"%s\" but got %lu",
		      nparams, query, param_count);
		goto prepare_failed;
	}

	if (!validate_stmt_column_count(stmt, query, ncols))
		goto prepare_failed;

	if (nparams && mysql_stmt_bind_param(stmt, bind_parm)) {
		error("can not bind parameter buffers for \"%s\": %s",
		      query, mysql_stmt_error(stmt));
		goto prepare_failed;
	}

	if (mysql_stmt_bind_result(stmt, bind_col)) {
		error("can not bind output buffers for \"%s\": %s",
		      query, mysql_stmt_error(stmt));
		goto prepare_failed;
	}

	return stmt;

prepare_failed:
	(void)mysql_stmt_close(stmt);
	return NULL;
}

/**
 * store_stmt_results - Buffer all results of a query on the client
 * Returns -1 on error, number_of_rows >= 0 if ok.
 */
static int store_stmt_results(MYSQL_STMT *stmt, const char *query,
			      MYSQL_BIND bind_col[], unsigned long ncols)
{
	my_ulonglong nrows;
	int i;

	if (stmt == NULL || ncols == 0)
		return -1;

	if (mysql_stmt_store_result(stmt)) {
		error("can not store query result for \"%s\": %s",
		      query, mysql_stmt_error(stmt));
		return -1;
	}

	nrows = mysql_stmt_affected_rows(stmt);
	if (nrows == (my_ulonglong)-1) {
		error("query \"%s\" returned an error: %s",
		      query, mysql_stmt_error(stmt));
		return -1;
	}

	while (mysql_stmt_fetch(stmt) == 0)
		for (i = 0; i < ncols; i++) {
			if (bind_col[i].error && *bind_col[i].error)  {
				error("result value in column %d truncated: %s",
				      i, mysql_stmt_error(stmt));
				return -1;
			}
		}

	/* Seek back to begin of data set */
	mysql_stmt_data_seek(stmt, 0);

	return nrows;
}

/**
 * exec_stmt - Execute, store and validate a prepared statement
 * @query:	query text
 * @bind_col:	as in prepare_stmt()
 * @ncols:	as in prepare_stmt()
 * Returns -1 on error, number_of_rows >= 0 if ok.
 */
int exec_stmt(MYSQL_STMT *stmt, const char *query,
	      MYSQL_BIND bind_col[], unsigned long ncols)
{
	if (mysql_stmt_execute(stmt)) {
		error("failed to execute \"%s\": %s",
		      query, mysql_stmt_error(stmt));
		return -1;
	}
	return store_stmt_results(stmt, query, bind_col, ncols);
}

/**
 * fetch_stmt - return the next row in the result set.
 * Returns 1 on error,  0 if ok.
 */
int fetch_stmt(MYSQL_STMT *stmt)
{
	return mysql_stmt_fetch(stmt);
}

my_bool free_stmt_result(MYSQL_STMT *stmt)
{
	return mysql_stmt_free_result(stmt);
}

my_bool stmt_close(MYSQL_STMT *stmt)
{
	return mysql_stmt_close(stmt);
}

void cray_close_sdb(MYSQL *handle)
{
	mysql_close(handle);
}
