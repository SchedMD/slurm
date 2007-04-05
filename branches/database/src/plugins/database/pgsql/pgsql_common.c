/*****************************************************************************\
 *  pgsql_common.c - common functions for the the pgsql database plugin.
 *****************************************************************************
 *
 *  Copyright (C) 2004-2007 The Regents of the University of California.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Danny Auble <da@llnl.gov>
 *  
 *  This file is part of SLURM, a resource management program.
 *  For details, see <http://www.llnl.gov/linux/slurm/>.
 *  
 *  SLURM is free software; you can redistribute it and/or modify it under
 *  the terms of the GNU General Public License as published by the Free
 *  Software Foundation; either version 2 of the License, or (at your option)
 *  any later version.
 *
 *  In addition, as a special exception, the copyright holders give permission 
 *  to link the code of portions of this program with the OpenSSL library under
 *  certain conditions as described in each individual source file, and 
 *  distribute linked combinations including the two. You must obey the GNU 
 *  General Public License in all respects for all of the code used other than 
 *  OpenSSL. If you modify file(s) with this exception, you may extend this 
 *  exception to your version of the file(s), but you are not obligated to do 
 *  so. If you do not wish to do so, delete this exception statement from your
 *  version.  If you delete this exception statement from all source files in 
 *  the program, then also delete it here.
 *  
 *  SLURM is distributed in the hope that it will be useful, but WITHOUT ANY
 *  WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 *  FOR A PARTICULAR PURPOSE.  See the GNU General Public License for more
 *  details.
 *  
 *  You should have received a copy of the GNU General Public License along
 *  with SLURM; if not, write to the Free Software Foundation, Inc.,
 *  51 Franklin Street, Fifth Floor, Boston, MA 02110-1301  USA.
 *
 *  This file is patterned after jobcomp_linux.c, written by Morris Jette and
 *  Copyright (C) 2002 The Regents of the University of California.
\*****************************************************************************/

#include "pgsql_common.h"
#include <stdlib.h>

bool thread_safe = true;
pthread_mutex_t pgsql_lock = PTHREAD_MUTEX_INITIALIZER;

#ifdef HAVE_PGSQL
extern pgsql_db_info_t *create_pgsql_db_info()
{
	pgsql_db_info_t *db_info = xmalloc(sizeof(pgsql_db_info_t));
	db_info->port = slurm_get_database_port();
	/* it turns out it is better if using defaults to let postgres
	   handle them on it's own terms */
	if(!db_info->port)
		db_info->port = 5432;
	db_info->host = slurm_get_database_host();
	db_info->user = slurm_get_database_user();	
	db_info->pass = slurm_get_database_pass();	
	return db_info;
}

extern int *destroy_pgsql_db_info(pgsql_db_info_t *db_info)
{
	if(db_info) {
		xfree(db_info->host);
		xfree(db_info->user);
		xfree(db_info->pass);
		xfree(db_info);
	}
	return SLURM_SUCCESS;
}

extern int pgsql_create_db(PGconn *pgsql_db, char *db_name,
			   pgsql_db_info_t *db_info)
{
	char create_line[50];
	char *connect_line = xstrdup_printf("dbname = 'postgres'"
					    " host = '%s'"
					    " port = '%u'"
					    " user = '%s'"
					    " password = '%s'",
					    db_info->host,
					    db_info->port,
					    db_info->user,
					    db_info->pass);

	pgsql_db = PQconnectdb(connect_line);

	if (PQstatus(pgsql_db) == CONNECTION_OK) {
		PGresult *result = NULL;
		snprintf(create_line, sizeof(create_line),
			 "create database %s", db_name);
		result = PQexec(pgsql_db, create_line);
		if (PQresultStatus(result) != PGRES_COMMAND_OK) {
			fatal("PQexec failed: %d %s\n%s",
			     PQresultStatus(result), PQerrorMessage(pgsql_db), create_line);
		}
		PQclear(result);
	} else {
		info("Connection failed to %s", connect_line);
		fatal("Status was: %d %s",
		      PQstatus(pgsql_db), PQerrorMessage(pgsql_db));
	}
	xfree(connect_line);
	return SLURM_SUCCESS;
}

extern int pgsql_get_db_connection(PGconn **pgsql_db, char *db_name,
				   pgsql_db_info_t *db_info,
				   int *database_init)
{
	int rc = SLURM_SUCCESS;
	char *connect_line = xstrdup_printf("dbname = '%s'"
					    " host = '%s'"
					    " port = '%u'"
					    " user = '%s'"
					    " password = '%s'",
					    db_name,
					    db_info->host,
					    db_info->port,
					    db_info->user,
					    db_info->pass);

	while(!*database_init) {
		*pgsql_db = PQconnectdb(connect_line);
		
		if(PQstatus(*pgsql_db) != CONNECTION_OK) {
			if(!strcmp(PQerrorMessage(*pgsql_db),
				   PQnoPasswordSupplied)) {
				PQfinish(*pgsql_db);
				fatal("This Postgres connection needs "
				      "a password.  It doesn't appear to "
				      "like blank ones");
			} 
			
			info("Database %s not created. Creating %d", db_name);
			PQfinish(*pgsql_db);
			pgsql_create_db(*pgsql_db, db_name, db_info);
			
		} else {
			*database_init = true;
			debug2("connected to %s", db_name);
		} 
	}
	xfree(connect_line);
	return rc;
}

extern int pgsql_db_query(PGconn *pgsql_db, int database_init, char *query)
{
	PGresult *result = NULL;
	
	if(!database_init)
		fatal("You haven't inited this database yet.");
	
	if(!(result = pgsql_db_query_ret(pgsql_db, database_init, query))) 
		return SLURM_ERROR;
	
	PQclear(result);
	return SLURM_SUCCESS;
}

extern PGresult *pgsql_db_query_ret(PGconn *pgsql_db, int database_init,
				    char *query)
{
	PGresult *result = NULL;
	
	if(!database_init)
		fatal("You haven't inited this database yet.");

	result = PQexec(pgsql_db, query);

	if(PQresultStatus(result) != PGRES_COMMAND_OK
	   && PQresultStatus(result) != PGRES_TUPLES_OK) {
		error("PQexec failed: %d %s", PQresultStatus(result), 
		      PQerrorMessage(pgsql_db));
		info("query was %s", query);
		PQclear(result);
		return NULL;
	}
	return result;
}

extern int pgsql_insert_ret_id(PGconn *pgsql_db, int database_init,
			       char *sequence_name, char *query)
{
	int new_id = 0;
	PGresult *result = NULL;

	if(pgsql_db_query(pgsql_db, database_init, query) != SLURM_ERROR)  {
		char *new_query = xstrdup_printf(
			"select last_value from %s", sequence_name);
		
		if((result = pgsql_db_query_ret(pgsql_db,
						database_init, new_query))) {
			new_id = atoi(PQgetvalue(result, 0, 0));
			PQclear(result);		
		}
		xfree(new_query);
		if(!new_id) {
			/* should have new id */
			error("We should have gotten a new id: %s", 
			      PQerrorMessage(pgsql_db));
		}
	}

	return new_id;
	
}

extern int pgsql_db_create_table(PGconn *pgsql_db, int database_init, 
				 char *table_name, database_field_t *fields,
				 char *ending)
{
	char *query = NULL;
	char *tmp = NULL;
	char *next = NULL;
	int i = 0;

	query = xstrdup_printf("create table %s (", table_name);
	i=0;
	while(fields && fields->name) {
		next = xstrdup_printf(" %s %s",
				      fields->name, 
				      fields->options);
		if(i) 
			xstrcat(tmp, ",");
		xstrcat(tmp, next);
		xfree(next);
		fields++;
		i++;
	}
	xstrcat(query, tmp);
	xfree(tmp);
	xstrcat(query, ending);

	if(pgsql_db_query(pgsql_db, database_init, query) == SLURM_ERROR) {
		xfree(query);
		return SLURM_ERROR;
	}
	xfree(query);

	return SLURM_SUCCESS;
}

#endif

