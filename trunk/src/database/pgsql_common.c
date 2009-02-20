/*****************************************************************************\
 *  pgsql_common.c - common functions for the the pgsql storage plugin.
 *****************************************************************************
 *
 *  Copyright (C) 2004-2007 The Regents of the University of California.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Danny Auble <da@llnl.gov>
 *  
 *  This file is part of SLURM, a resource management program.
 *  For details, see <https://computing.llnl.gov/linux/slurm/>.
 *  Please also read the included file: DISCLAIMER.
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

pthread_mutex_t pgsql_lock = PTHREAD_MUTEX_INITIALIZER;

#ifdef HAVE_PGSQL

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

extern int _create_db(char *db_name, pgsql_db_info_t *db_info)
{
	char create_line[50];
	PGconn *pgsql_db = NULL;
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
		pgsql_close_db_connection(&pgsql_db);
	} else {
		info("Connection failed to %s", connect_line);
		fatal("Status was: %d %s",
		      PQstatus(pgsql_db), PQerrorMessage(pgsql_db));
	}
	xfree(connect_line);

	return SLURM_SUCCESS;
}

extern int pgsql_get_db_connection(PGconn **pgsql_db, char *db_name,
				   pgsql_db_info_t *db_info)
{
	int rc = SLURM_SUCCESS;
	bool storage_init = false;
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

	while(!storage_init) {
		*pgsql_db = PQconnectdb(connect_line);
		
		if(PQstatus(*pgsql_db) != CONNECTION_OK) {
			if(!strcmp(PQerrorMessage(*pgsql_db),
				   "no password supplied")) {
				PQfinish(*pgsql_db);
				fatal("This Postgres connection needs "
				      "a password.  It doesn't appear to "
				      "like blank ones");
			} 
			
			info("Database %s not created. Creating", db_name);
			pgsql_close_db_connection(pgsql_db);
			_create_db(db_name, db_info);		
		} else {
			storage_init = true;
		} 
	}
	xfree(connect_line);
	return rc;
}

extern int pgsql_close_db_connection(PGconn **pgsql_db)
{
	if(pgsql_db && *pgsql_db) {
		PQfinish(*pgsql_db);
		*pgsql_db = NULL;
	}	      
	return SLURM_SUCCESS;
}

extern int pgsql_db_query(PGconn *pgsql_db, char *query)
{
	PGresult *result = NULL;
	
	if(!pgsql_db)
		fatal("You haven't inited this storage yet.");
	
	if(!(result = pgsql_db_query_ret(pgsql_db, query))) 
		return SLURM_ERROR;
	
	PQclear(result);
	return SLURM_SUCCESS;
}

extern int pgsql_db_commit(PGconn *pgsql_db)
{
	return pgsql_db_query(pgsql_db, "COMMIT WORK");	
}

extern int pgsql_db_rollback(PGconn *pgsql_db)
{
	return pgsql_db_query(pgsql_db, "ROLLBACK WORK");

}

extern PGresult *pgsql_db_query_ret(PGconn *pgsql_db, char *query)
{
	PGresult *result = NULL;
	
	if(!pgsql_db)
		fatal("You haven't inited this storage yet.");

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

extern int pgsql_insert_ret_id(PGconn *pgsql_db, char *sequence_name,
			       char *query)
{
	int new_id = 0;
	PGresult *result = NULL;

	slurm_mutex_lock(&pgsql_lock);
	if(pgsql_db_query(pgsql_db, query) != SLURM_ERROR)  {
		char *new_query = xstrdup_printf(
			"select last_value from %s", sequence_name);
		
		if((result = pgsql_db_query_ret(pgsql_db, new_query))) {
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
	slurm_mutex_unlock(&pgsql_lock);
	
	return new_id;
	
}

extern int pgsql_db_create_table(PGconn *pgsql_db,  
				 char *table_name, storage_field_t *fields,
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

	if(pgsql_db_query(pgsql_db, query) == SLURM_ERROR) {
		xfree(query);
		return SLURM_ERROR;
	}
	xfree(query);

	return SLURM_SUCCESS;
}

extern int pgsql_db_make_table_current(PGconn *pgsql_db, char *table_name,
				       storage_field_t *fields)
{
	char *query = NULL, *opt_part = NULL, *temp_char = NULL;
	char *type = NULL;
	int not_null = 0;
	char *default_str = NULL;
	char* original_ptr = NULL;
	int i = 0;
	PGresult *result = NULL;
	List columns = NULL;
	ListIterator itr = NULL;
	char *col = NULL;

	DEF_TIMERS;

	query = xstrdup_printf("select column_name from "
			       "information_schema.columns where "
			       "table_name='%s'", table_name);

	if(!(result = pgsql_db_query_ret(pgsql_db, query))) {
		xfree(query);
		return SLURM_ERROR;
	}
	xfree(query);
	columns = list_create(slurm_destroy_char);
	for (i = 0; i < PQntuples(result); i++) {
		col = xstrdup(PQgetvalue(result, i, 0)); //column_name
		list_append(columns, col);
	}
	PQclear(result);
	itr = list_iterator_create(columns);
	query = xstrdup_printf("alter table %s", table_name);
	START_TIMER;
	i=0;
	while(fields[i].name) {
		int found = 0;
		if(!strcmp("serial", fields[i].options)) {
			i++;
			continue;
		} 
		opt_part = xstrdup(fields[i].options);
		original_ptr = opt_part;
		opt_part = strtok_r(opt_part, " ", &temp_char);
		if(opt_part) {
			type = xstrdup(opt_part);
			opt_part = temp_char;
			opt_part = strtok_r(opt_part, " ", &temp_char);
			while(opt_part) {
				if(!strcmp("not null", opt_part)) {
					not_null = 1;
					opt_part = temp_char;
					opt_part = strtok_r(opt_part,
							    " ", &temp_char);
				} else if(!strcmp("default", opt_part)){
					opt_part = temp_char;
					opt_part = strtok_r(opt_part,
							    " ", &temp_char);
					default_str = xstrdup(opt_part);
				}
				if(opt_part) {
					opt_part = temp_char;
					opt_part = strtok_r(opt_part,
							    " ", &temp_char);
				}
			}
		} else {
			type = xstrdup(fields[i].options);
		}
		xfree(original_ptr);
		list_iterator_reset(itr);
		while((col = list_next(itr))) {
			if(!strcmp(col, fields[i].name)) {
				list_delete_item(itr);
				found = 1;
				break;
			}
		}
		
		temp_char = NULL;
		if(!found) {
			info("adding column %s", fields[i].name);
			if(default_str) 
				xstrfmtcat(temp_char,
					   " default %s", default_str);
						
			if(not_null) 
				xstrcat(temp_char, " not null");
			
			xstrfmtcat(query,
				   " add %s %s",
				   fields[i].name, type);
			if(temp_char)
				xstrcat(query, temp_char);
			xstrcat(query, ",");
		} else {
			if(default_str) 
				xstrfmtcat(temp_char,
					   " alter %s set default %s,",
					   fields[i].name, default_str);
			else 
				xstrfmtcat(temp_char,
					   " alter %s drop default,",
					   fields[i].name);
			
			if(not_null) 
				xstrfmtcat(temp_char,
					   " alter %s set not null,",
					   fields[i].name);
			else 
				xstrfmtcat(temp_char,
					   " alter %s drop not null,",
					   fields[i].name);
			xstrfmtcat(query, " alter %s type %s,%s",
				   fields[i].name, type, temp_char);
		}
		xfree(temp_char);
		xfree(default_str);
		xfree(type);
	
		i++;
	}
	list_iterator_destroy(itr);
	list_destroy(columns);
	query[strlen(query)-1] = ';';

	if(pgsql_db_query(pgsql_db, query)) {
		xfree(query);
		return SLURM_ERROR;
	}
	xfree(query);
		
	END_TIMER2("make table current");
	return SLURM_SUCCESS;
}


#endif

