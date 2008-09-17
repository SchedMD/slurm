/*****************************************************************************\
 *  file_functions.c - functions dealing with files that are generated in the
 *                        accounting system.
 *****************************************************************************
 *  Copyright (C) 2008 Lawrence Livermore National Security.
 *  Copyright (C) 2002-2007 The Regents of the University of California.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Danny Auble <da@llnl.gov>
 *  LLNL-CODE-402394.
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
\*****************************************************************************/

#include "src/sacctmgr/sacctmgr.h"
#include "src/common/uid.h"

typedef struct {
	acct_admin_level_t admin;
	List coord_list; /* char *list */
	char *def_acct;
	char *desc;
	uint32_t fairshare;
	uint32_t max_cpu_mins_pj; 
	uint32_t max_jobs;
	uint32_t max_nodes_pj; 
	uint32_t max_wall_pj;
	char *name;
	char *org;
	char *part;
	List qos_list;
} sacctmgr_file_opts_t;

enum {
	PRINT_ACCOUNT,
	PRINT_ADMIN,
	PRINT_CLUSTER,
	PRINT_COORDS,
	PRINT_DACCT,
	PRINT_DESC,
	PRINT_FAIRSHARE,
	PRINT_ID,
	PRINT_MAXC,
	PRINT_MAXJ,
	PRINT_MAXN,
	PRINT_MAXW,
	PRINT_NAME,
	PRINT_ORG,
	PRINT_QOS,
	PRINT_QOS_RAW,
	PRINT_PID,
	PRINT_PARENT,
	PRINT_PART,
	PRINT_USER
};

typedef enum {
	MOD_CLUSTER,
	MOD_ACCT,
	MOD_USER
} sacctmgr_mod_type_t;

static List qos_list = NULL;

static int _strip_continuation(char *buf, int len)
{
	char *ptr;
	int bs = 0;

	for (ptr = buf+len-1; ptr >= buf; ptr--) {
		if (*ptr == '\\')
			bs++;
		else if (isspace(*ptr) && bs == 0)
			continue;
		else
			break;
	}
	/* Check for an odd number of contiguous backslashes at
	   the end of the line */
	if (bs % 2 == 1) {
		ptr = ptr + bs;
		*ptr = '\0';
		return (ptr - buf);
	} else {
		return len; /* no continuation */
	}
}

/* Strip comments from a line by terminating the string
 * where the comment begins.
 * Everything after a non-escaped "#" is a comment.
 */
static void _strip_comments(char *line)
{
	int i;
	int len = strlen(line);
	int bs_count = 0;

	for (i = 0; i < len; i++) {
		/* if # character is preceded by an even number of
		 * escape characters '\' */
		if (line[i] == '#' && (bs_count%2) == 0) {
			line[i] = '\0';
 			break;
		} else if (line[i] == '\\') {
			bs_count++;
		} else {
			bs_count = 0;
		}
	}
}

/*
 * Strips any escape characters, "\".  If you WANT a back-slash,
 * it must be escaped, "\\".
 */
static void _strip_escapes(char *line)
{
	int i, j;
	int len = strlen(line);

	for (i = 0, j = 0; i < len+1; i++, j++) {
		if (line[i] == '\\')
			i++;
		line[j] = line[i];
	}
}

/*
 * Reads the next line from the "file" into buffer "buf".
 *
 * Concatonates together lines that are continued on
 * the next line by a trailing "\".  Strips out comments,
 * replaces escaped "\#" with "#", and replaces "\\" with "\".
 */
static int _get_next_line(char *buf, int buf_size, FILE *file)
{
	char *ptr = buf;
	int leftover = buf_size;
	int read_size, new_size;
	int lines = 0;

	while (fgets(ptr, leftover, file)) {
		lines++;
		_strip_comments(ptr);
		read_size = strlen(ptr);
		new_size = _strip_continuation(ptr, read_size);
		if (new_size < read_size) {
			ptr += new_size;
			leftover -= new_size;
		} else { /* no continuation */
			break;
		}
	}
	/* _strip_cr_nl(buf); */ /* not necessary */
	_strip_escapes(buf);
	
	return lines;
}

static void _destroy_sacctmgr_file_opts(void *object)
{
	sacctmgr_file_opts_t *file_opts = (sacctmgr_file_opts_t *)object;

	if(file_opts) {
		if(file_opts->coord_list)
			list_destroy(file_opts->coord_list);
		xfree(file_opts->def_acct);
		xfree(file_opts->desc);
		xfree(file_opts->name);
		xfree(file_opts->org);
		xfree(file_opts->part);
		xfree(file_opts);		
	}
}

static sacctmgr_file_opts_t *_parse_options(char *options)
{
	int start=0, i=0, end=0, mins, quote = 0;
 	char *sub = NULL;
	sacctmgr_file_opts_t *file_opts = xmalloc(sizeof(sacctmgr_file_opts_t));
	char *option = NULL;
	char quote_c = '\0';
	
	file_opts->fairshare = 1;
	file_opts->max_cpu_mins_pj = INFINITE;
	file_opts->max_jobs = INFINITE;
	file_opts->max_nodes_pj = INFINITE;
	file_opts->max_wall_pj = INFINITE;
	file_opts->admin = ACCT_ADMIN_NOTSET;

	while(options[i]) {
		quote = 0;
		start=i;
		
		while(options[i] && options[i] != ':' && options[i] != '\n') {
			if(options[i] == '"' || options[i] == '\'') {
				if(quote) {
					if(options[i] == quote_c)
						quote = 0;
				} else {
					quote = 1;
					quote_c = options[i];
				}
			}
			i++;
		}
		if(quote) {
			while(options[i] && options[i] != quote_c) 
				i++;
			if(!options[i])
				fatal("There is a problem with option "
				      "%s with quotes.", option);
			i++;
		}

		if(i-start <= 0)
			goto next_col;

		sub = xstrndup(options+start, i-start);
		end = parse_option_end(sub);
		
		option = strip_quotes(sub+end, NULL);
		if(!end) {
			if(file_opts->name) {
				exit_code=1;
				fprintf(stderr, " Bad format on %s: "
				       "End your option with "
				       "an '=' sign\n", sub);
				_destroy_sacctmgr_file_opts(file_opts);
				break;
			}
			file_opts->name = xstrdup(option);
		} else if (!strncasecmp (sub, "AdminLevel", 2)) {
			file_opts->admin = str_2_acct_admin_level(option);
		} else if (!strncasecmp (sub, "Coordinator", 2)) {
			if(!file_opts->coord_list)
				file_opts->coord_list =
					list_create(slurm_destroy_char);
			slurm_addto_char_list(file_opts->coord_list, option);
		} else if (!strncasecmp (sub, "DefaultAccount", 3)) {
			file_opts->def_acct = xstrdup(option);
		} else if (!strncasecmp (sub, "Description", 3)) {
			file_opts->desc = xstrdup(option);
		} else if (!strncasecmp (sub, "FairShare", 1)) {
			if (get_uint(option, &file_opts->fairshare, 
			    "FairShare") != SLURM_SUCCESS) {
				exit_code=1;
				fprintf(stderr, 
					" Bad FairShare value: %s\n", option);
				_destroy_sacctmgr_file_opts(file_opts);
				break;
			}
		} else if (!strncasecmp (sub, "MaxCPUMin", 4)
			   || !strncasecmp (sub, "MaxProcSec", 4)) {
			if (get_uint(option, &file_opts->max_cpu_mins_pj,
			    "MaxCPUMin") != SLURM_SUCCESS) {
				exit_code=1;
				fprintf(stderr, 
					" Bad MaxCPUMin value: %s\n", option);
				_destroy_sacctmgr_file_opts(file_opts);
				break;
			}
		} else if (!strncasecmp (sub, "MaxJobs", 4)) {
			if (get_uint(option, &file_opts->max_jobs,
			    "MaxJobs") != SLURM_SUCCESS) {
				exit_code=1;
				fprintf(stderr, 
					" Bad MaxJobs value: %s\n", option);
				_destroy_sacctmgr_file_opts(file_opts);
				break;
			}
		} else if (!strncasecmp (sub, "MaxNodes", 4)) {
			if (get_uint(option, &file_opts->max_nodes_pj,
			    "MaxNodes") != SLURM_SUCCESS) {
				exit_code=1;
				fprintf(stderr, 
					" Bad MaxNodes value: %s\n", option);
				_destroy_sacctmgr_file_opts(file_opts);
				break;
			}
		} else if (!strncasecmp (sub, "MaxWall", 4)) {
			mins = time_str2mins(option);
			if (mins >= 0) {
				file_opts->max_wall_pj 
					= (uint32_t) mins;
			} else if (strcmp(option, "-1")) {
				file_opts->max_wall_pj = INFINITE;
			} else {
				exit_code=1;
				fprintf(stderr, 
					" Bad MaxWall time format: %s\n", 
					option);
				_destroy_sacctmgr_file_opts(file_opts);
				break;
			}
		} else if (!strncasecmp (sub, "Organization", 1)) {
			file_opts->org = xstrdup(option);
		} else if (!strncasecmp (sub, "QosLevel", 1)
			   || !strncasecmp (sub, "Expedite", 1)) {
			int option2 = 0;
			if(!file_opts->qos_list) {
				file_opts->qos_list = 
					list_create(slurm_destroy_char);
			}
			
			if(!qos_list) {
				qos_list = acct_storage_g_get_qos(
					db_conn, my_uid, NULL);
			}
			if(end > 2 && sub[end-1] == '='
			   && (sub[end-2] == '+' 
			       || sub[end-2] == '-'))
				option2 = (int)sub[end-2];

			addto_qos_char_list(file_opts->qos_list, qos_list,
					    option, option2);
		} else {
			exit_code=1;
			fprintf(stderr, " Unknown option: %s\n", sub);
		}

		xfree(sub);
		xfree(option);

	next_col:
		if(options[i] == ':')
			i++;
		else
			break;
	}
	
	xfree(sub);
	xfree(option);

	if(!file_opts->name) {
		exit_code=1;
		fprintf(stderr, " No name given\n");
		_destroy_sacctmgr_file_opts(file_opts);
		file_opts = NULL;
	} else if(exit_code) {
		_destroy_sacctmgr_file_opts(file_opts);
		file_opts = NULL;
	}

	return file_opts;
}

static List _set_up_print_fields(List format_list)
{
	ListIterator itr = NULL;
	List print_fields_list = NULL;
	print_field_t *field = NULL;
	char *object = NULL;

	print_fields_list = list_create(destroy_print_field);
	itr = list_iterator_create(format_list);
	while((object = list_next(itr))) {
		field = xmalloc(sizeof(print_field_t));
		if(!strncasecmp("Account", object, 2)) {
			field->type = PRINT_ACCOUNT;
			field->name = xstrdup("Account");
			field->len = 10;
			field->print_routine = print_fields_str;
		} else if(!strncasecmp("AdminLevel", object, 2)) {
			field->type = PRINT_ADMIN;
			field->name = xstrdup("Admin");
			field->len = 9;
			field->print_routine = print_fields_str;
		} else if(!strncasecmp("Cluster", object, 2)) {
			field->type = PRINT_CLUSTER;
			field->name = xstrdup("Cluster");
			field->len = 10;
			field->print_routine = print_fields_str;
		} else if(!strncasecmp("Coordinators", object, 2)) {
			field->type = PRINT_COORDS;
			field->name = xstrdup("Coord Accounts");
			field->len = 20;
			field->print_routine = sacctmgr_print_coord_list;
		} else if(!strncasecmp("Default", object, 3)) {
			field->type = PRINT_DACCT;
			field->name = xstrdup("Def Acct");
			field->len = 10;
			field->print_routine = print_fields_str;
		} else if(!strncasecmp("Description", object, 3)) {
			field->type = PRINT_DESC;
			field->name = xstrdup("Descr");
			field->len = 20;
			field->print_routine = print_fields_str;
		} else if(!strncasecmp("FairShare", object, 1)) {
			field->type = PRINT_FAIRSHARE;
			field->name = xstrdup("FairShare");
			field->len = 9;
			field->print_routine = print_fields_uint;
		} else if(!strncasecmp("ID", object, 1)) {
			field->type = PRINT_ID;
			field->name = xstrdup("ID");
			field->len = 6;
			field->print_routine = print_fields_uint;
		} else if(!strncasecmp("MaxCPUMins", object, 4)) {
			field->type = PRINT_MAXC;
			field->name = xstrdup("MaxCPUMins");
			field->len = 11;
			field->print_routine = print_fields_uint;
		} else if(!strncasecmp("MaxJobs", object, 4)) {
			field->type = PRINT_MAXJ;
			field->name = xstrdup("MaxJobs");
			field->len = 7;
			field->print_routine = print_fields_uint;
		} else if(!strncasecmp("MaxNodes", object, 4)) {
			field->type = PRINT_MAXN;
			field->name = xstrdup("MaxNodes");
			field->len = 8;
			field->print_routine = print_fields_uint;
		} else if(!strncasecmp("MaxWall", object, 4)) {
			field->type = PRINT_MAXW;
			field->name = xstrdup("MaxWall");
			field->len = 11;
			field->print_routine = print_fields_time;
		} else if(!strncasecmp("Name", object, 1)) {
			field->type = PRINT_NAME;
			field->name = xstrdup("Name");
			field->len = 10;
			field->print_routine = print_fields_str;
		} else if(!strncasecmp("Organization", object, 1)) {
			field->type = PRINT_ORG;
			field->name = xstrdup("Org");
			field->len = 20;
			field->print_routine = print_fields_str;
		} else if(!strncasecmp("QOSRAW", object, 4)) {
			field->type = PRINT_QOS_RAW;
			field->name = xstrdup("QOS_RAW");
			field->len = 7;
			field->print_routine = print_fields_char_list;
		} else if(!strncasecmp("QOS", object, 1)) {
			field->type = PRINT_QOS;
			field->name = xstrdup("QOS");
			field->len = 9;
			field->print_routine = sacctmgr_print_qos_list;
		} else if(!strncasecmp("Parent", object, 4)) {
			field->type = PRINT_PARENT;
			field->name = xstrdup("Parent");
			field->len = 10;
			field->print_routine = print_fields_str;
		} else if(!strncasecmp("Partition", object, 4)) {
			field->type = PRINT_PART;
			field->name = xstrdup("Partition");
			field->len = 10;
			field->print_routine = print_fields_str;
		} else if(!strncasecmp("User", object, 1)) {
			field->type = PRINT_USER;
			field->name = xstrdup("User");
			field->len = 10;
			field->print_routine = print_fields_str;
		} else {
			exit_code=1;
			fprintf(stderr, "Unknown field '%s'\n", object);
			xfree(field);
			continue;
		}
		list_append(print_fields_list, field);		
	}
	list_iterator_destroy(itr);
	return print_fields_list;
}

static int _print_out_assoc(List assoc_list, bool user)
{
	List format_list = NULL;
	List print_fields_list = NULL;
	ListIterator itr, itr2;
	print_field_t *field = NULL;
	acct_association_rec_t *assoc = NULL;
	int rc = SLURM_SUCCESS;

	if(!assoc_list || !list_count(assoc_list))
		return rc;
	
	format_list = list_create(slurm_destroy_char);
	if(user)
		slurm_addto_char_list(format_list,
				"User,Account,F,MaxC,MaxJ,MaxN,MaxW");
	else 
		slurm_addto_char_list(format_list,
				"Account,Parent,F,MaxC,MaxJ,MaxN,MaxW");
	
	print_fields_list = _set_up_print_fields(format_list);
	list_destroy(format_list);
	
	print_fields_header(print_fields_list);
	
	itr = list_iterator_create(assoc_list);
	itr2 = list_iterator_create(print_fields_list);
	while((assoc = list_next(itr))) {
		while((field = list_next(itr2))) {
			switch(field->type) {
			case PRINT_ACCOUNT:
				field->print_routine(field,
						     assoc->acct);
				break;
			case PRINT_FAIRSHARE:
				field->print_routine(field,
						     assoc->fairshare);
				break;
			case PRINT_MAXC:
				field->print_routine(
					field,
					assoc->max_cpu_mins_pj);
				break;
			case PRINT_MAXJ:
				field->print_routine(field, 
						     assoc->max_jobs);
				break;
			case PRINT_MAXN:
				field->print_routine(field,
						     assoc->max_nodes_pj);
				break;
			case PRINT_MAXW:
				field->print_routine(
					field,
					assoc->max_wall_pj);
				break;
			case PRINT_PARENT:
				field->print_routine(field,
						     assoc->parent_acct);
				break;
			case PRINT_PART:
				field->print_routine(field,
						     assoc->partition);
				break;
			case PRINT_USER:
				field->print_routine(field, 
						     assoc->user);
				break;
			default:
				break;
			}
		}
		list_iterator_reset(itr2);
		printf("\n");
	}
	list_iterator_destroy(itr);
	list_iterator_destroy(itr2);
	list_destroy(print_fields_list);
	rc = acct_storage_g_add_associations(db_conn, my_uid, assoc_list);
	printf("---------------------------------------------------\n\n");

	return rc;
}

static int _mod_cluster(sacctmgr_file_opts_t *file_opts,
			acct_cluster_rec_t *cluster)
{
	int changed = 0;
	acct_association_rec_t mod_assoc;
	acct_association_cond_t assoc_cond;
	char *my_info = NULL;

	memset(&mod_assoc, 0, sizeof(acct_association_rec_t));

	mod_assoc.fairshare = NO_VAL;
	mod_assoc.max_cpu_mins_pj = NO_VAL;
	mod_assoc.max_jobs = NO_VAL;
	mod_assoc.max_nodes_pj = NO_VAL;
	mod_assoc.max_wall_pj = NO_VAL;

	memset(&assoc_cond, 0, sizeof(acct_association_cond_t));

	assoc_cond.fairshare = NO_VAL;
	assoc_cond.max_cpu_mins_pj = NO_VAL;
	assoc_cond.max_jobs = NO_VAL;
	assoc_cond.max_nodes_pj = NO_VAL;
	assoc_cond.max_wall_pj = NO_VAL;

	if(cluster->default_fairshare != file_opts->fairshare) {
		mod_assoc.fairshare = file_opts->fairshare;
		changed = 1;
		xstrfmtcat(my_info, 
			   "%-30.30s for %-7.7s %-10.10s %8d -> %d\n",
			   " Changed fairshare", "Cluster",
			   cluster->name,
			   cluster->default_fairshare,
			   file_opts->fairshare); 
	}
	if(cluster->default_max_cpu_mins_pj != 
	   file_opts->max_cpu_mins_pj) {
		mod_assoc.max_cpu_mins_pj = 
			file_opts->max_cpu_mins_pj;
		changed = 1;
		xstrfmtcat(my_info, 
			   "%-30.30s for %-7.7s %-10.10s %8d -> %d\n",
			   " Changed MaxCPUMinsPerJob", "Cluster",
			   cluster->name,
			   cluster->default_max_cpu_mins_pj,
			   file_opts->max_cpu_mins_pj);
	}
	if(cluster->default_max_jobs != file_opts->max_jobs) {
		mod_assoc.max_jobs = file_opts->max_jobs;
		changed = 1;
		xstrfmtcat(my_info, 
			   "%-30.30s for %-7.7s %-10.10s %8d -> %d\n",
			   " Changed MaxJobs", "Cluster",
			   cluster->name,
			   cluster->default_max_jobs,
			   file_opts->max_jobs);
	}
	if(cluster->default_max_nodes_pj != file_opts->max_nodes_pj) {
		mod_assoc.max_nodes_pj = file_opts->max_nodes_pj;
		changed = 1;
		xstrfmtcat(my_info, 
			   "%-30.30s for %-7.7s %-10.10s %8d -> %d\n",
			   " Changed MaxNodesPerJob", "Cluster",
			   cluster->name,
			   cluster->default_max_nodes_pj, 
			   file_opts->max_nodes_pj);
	}
	if(cluster->default_max_wall_pj !=
	   file_opts->max_wall_pj) {
		mod_assoc.max_wall_pj =
			file_opts->max_wall_pj;
		changed = 1;
		xstrfmtcat(my_info, 
			   "%-30.30s for %-7.7s %-10.10s %8d -> %d\n",
			   " Changed MaxWallDurationPerJob", "Cluster",
			   cluster->name,
			   cluster->default_max_wall_pj,
			   file_opts->max_wall_pj);
	}

	if(changed) {
		List ret_list = NULL;
					
		assoc_cond.cluster_list = list_create(NULL); 
		assoc_cond.acct_list = list_create(NULL); 
					
		list_push(assoc_cond.cluster_list, cluster->name);
		list_push(assoc_cond.acct_list, "root");
					
		notice_thread_init();
		ret_list = acct_storage_g_modify_associations(
			db_conn, my_uid,
			&assoc_cond, 
			&mod_assoc);
		notice_thread_fini();
					
		list_destroy(assoc_cond.cluster_list);
		list_destroy(assoc_cond.acct_list);

/* 		if(ret_list && list_count(ret_list)) { */
/* 			char *object = NULL; */
/* 			ListIterator itr = list_iterator_create(ret_list); */
/* 			printf(" Modified cluster defaults for " */
/* 			       "associations...\n"); */
/* 			while((object = list_next(itr)))  */
/* 				printf("  %s\n", object); */
/* 			list_iterator_destroy(itr); */
/* 		} */
 
		if(ret_list) {
			printf("%s", my_info);
			list_destroy(ret_list);
		} else
			changed = 0;
		xfree(my_info);
	}

	return changed;
}

static int _mod_acct(sacctmgr_file_opts_t *file_opts,
		     acct_account_rec_t *acct, char *parent)
{
	int changed = 0;
	char *desc = NULL, *org = NULL, *my_info = NULL;
	acct_account_rec_t mod_acct;
	acct_account_cond_t acct_cond;
	acct_association_cond_t assoc_cond;
	
	memset(&mod_acct, 0, sizeof(acct_account_rec_t));
	memset(&acct_cond, 0, sizeof(acct_account_cond_t));
	memset(&assoc_cond, 0, sizeof(acct_association_cond_t));

	if(file_opts->desc) 
		desc = xstrdup(file_opts->desc);

	if(desc && strcmp(desc, acct->description)) {
		xstrfmtcat(my_info, 
			   "%-30.30s for %-7.7s %-10.10s %8s -> %s\n",
			   " Changed description", "Account",
			   acct->name,
			   acct->description,
			   desc);
		mod_acct.description = desc;
		changed = 1;
	} else 
		xfree(desc);
				
	if(file_opts->org)
		org = xstrdup(file_opts->org);

	if(org && strcmp(org, acct->organization)) {
		xstrfmtcat(my_info, 
			   "%-30.30s for %-7.7s %-10.10s %8s -> %s\n",
			   " Changed organization", "Account",
			   acct->name,
			   acct->organization,
			   org);
		mod_acct.organization = org;
		changed = 1;
	} else
		xfree(org);

	if(acct->qos_list && list_count(acct->qos_list)
	   && file_opts->qos_list && list_count(file_opts->qos_list)) {
		ListIterator now_qos_itr = list_iterator_create(acct->qos_list),
			new_qos_itr = list_iterator_create(file_opts->qos_list);
		char *now_qos = NULL, *new_qos = NULL;

		if(!mod_acct.qos_list)
			mod_acct.qos_list = list_create(slurm_destroy_char);
		while((new_qos = list_next(new_qos_itr))) {
			while((now_qos = list_next(now_qos_itr))) {
				if(!strcmp(new_qos, now_qos))
					break;
			}
			list_iterator_reset(now_qos_itr);
			if(!now_qos) 
				list_append(mod_acct.qos_list,
					    xstrdup(new_qos));
		}
		list_iterator_destroy(new_qos_itr);
		list_iterator_destroy(now_qos_itr);
		if(mod_acct.qos_list && list_count(mod_acct.qos_list))
			new_qos = get_qos_complete_str(qos_list,
						       mod_acct.qos_list);
		if(new_qos) {
			xstrfmtcat(my_info, 
				   " Adding QOS for account '%s' '%s'\n",
				   acct->name,
				   new_qos);
			xfree(new_qos);
			changed = 1;
		} else {
			list_destroy(mod_acct.qos_list);
			mod_acct.qos_list = NULL;
		}
	} else if(file_opts->qos_list && list_count(file_opts->qos_list)) {
		char *new_qos = get_qos_complete_str(qos_list,
						     file_opts->qos_list);
		
		if(new_qos) {
			xstrfmtcat(my_info, 
				   " Adding QOS for account '%s' '%s'\n",
				   acct->name,
				   new_qos);
			xfree(new_qos);
			mod_acct.qos_list = file_opts->qos_list;
			file_opts->qos_list = NULL;
			changed = 1;
		}
	}
									
	if(changed) {
		List ret_list = NULL;
					
		assoc_cond.acct_list = list_create(NULL);
		list_append(assoc_cond.acct_list, acct->name);
		acct_cond.assoc_cond = &assoc_cond;

		notice_thread_init();
		ret_list = acct_storage_g_modify_accounts(db_conn, my_uid,
							  &acct_cond, 
							  &mod_acct);
		notice_thread_fini();
	
		list_destroy(assoc_cond.acct_list);

		if(mod_acct.qos_list)
			list_destroy(mod_acct.qos_list);

/* 		if(ret_list && list_count(ret_list)) { */
/* 			char *object = NULL; */
/* 			ListIterator itr = list_iterator_create(ret_list); */
/* 			printf(" Modified account defaults for " */
/* 			       "associations...\n"); */
/* 			while((object = list_next(itr)))  */
/* 				printf("  %s\n", object); */
/* 			list_iterator_destroy(itr); */
/* 		} */
 
		if(ret_list) {
			printf("%s", my_info);
			list_destroy(ret_list);
		} else
			changed = 0;
		xfree(my_info);
	}
	xfree(desc);
	xfree(org);
	return changed;
}

static int _mod_user(sacctmgr_file_opts_t *file_opts,
		     acct_user_rec_t *user, char *parent)
{
	int rc;
	int set = 0;
	int changed = 0;
	char *def_acct = NULL, *my_info = NULL;
	acct_user_rec_t mod_user;
	acct_user_cond_t user_cond;
	List ret_list = NULL;
	acct_association_cond_t assoc_cond;

	if(!user || !user->name) {
		fatal(" We need a user name in _mod_user");
	}

	memset(&mod_user, 0, sizeof(acct_user_rec_t));
	memset(&user_cond, 0, sizeof(acct_user_cond_t));
	memset(&assoc_cond, 0, sizeof(acct_association_cond_t));
				
	assoc_cond.user_list = list_create(NULL);
	list_append(assoc_cond.user_list, user->name);
	user_cond.assoc_cond = &assoc_cond;

	if(file_opts->def_acct)
		def_acct = xstrdup(file_opts->def_acct);

	if(def_acct && 
	   (!user->default_acct || strcmp(def_acct, user->default_acct))) {
		xstrfmtcat(my_info, 
			   "%-30.30s for %-7.7s %-10.10s %8s -> %s\n",
			   " Changed Default Account", "User",
			   user->name,
			   user->default_acct,
			   def_acct);
		mod_user.default_acct = def_acct;
		changed = 1;
	} else
		xfree(def_acct);
				
	if(user->qos_list && list_count(user->qos_list)
	   && file_opts->qos_list && list_count(file_opts->qos_list)) {
		ListIterator now_qos_itr = list_iterator_create(user->qos_list),
			new_qos_itr = list_iterator_create(file_opts->qos_list);
		char *now_qos = NULL, *new_qos = NULL;

		if(!mod_user.qos_list)
			mod_user.qos_list = list_create(slurm_destroy_char);
		while((new_qos = list_next(new_qos_itr))) {
			while((now_qos = list_next(now_qos_itr))) {
				if(!strcmp(new_qos, now_qos))
					break;
			}
			list_iterator_reset(now_qos_itr);
			if(!now_qos) 
				list_append(mod_user.qos_list,
					    xstrdup(new_qos));
		}
		list_iterator_destroy(new_qos_itr);
		list_iterator_destroy(now_qos_itr);
		if(mod_user.qos_list && list_count(mod_user.qos_list))
			new_qos = get_qos_complete_str(qos_list,
						       mod_user.qos_list);
		if(new_qos) {
			xstrfmtcat(my_info, 
				   " Adding QOS for user '%s' '%s'\n",
				   user->name,
				   new_qos);
			xfree(new_qos);
			changed = 1;
		} else {
			list_destroy(mod_user.qos_list);
			mod_user.qos_list = NULL;
		}

	} else if(file_opts->qos_list && list_count(file_opts->qos_list)) {
		char *new_qos = get_qos_complete_str(qos_list,
						     file_opts->qos_list);
		
		if(new_qos) {
			xstrfmtcat(my_info, 
				   " Adding QOS for user '%s' '%s'\n",
				   user->name,
				   new_qos);
			xfree(new_qos);
			mod_user.qos_list = file_opts->qos_list;
			file_opts->qos_list = NULL;
			changed = 1;
		}
	}
									
	if(user->admin_level != ACCT_ADMIN_NOTSET
	   && file_opts->admin != ACCT_ADMIN_NOTSET
	   && user->admin_level != file_opts->admin) {
		xstrfmtcat(my_info, 
			   "%-30.30s for %-7.7s %-10.10s %8s -> %s\n",
			   " Changed Admin Level", "User",
			   user->name,
			   acct_admin_level_str(
				   user->admin_level),
			   acct_admin_level_str(
				   file_opts->admin));
		mod_user.admin_level = file_opts->admin;
		changed = 1;
	}
									
	if(changed) {
		notice_thread_init();
		ret_list = acct_storage_g_modify_users(
			db_conn, my_uid,
			&user_cond, 
			&mod_user);
		notice_thread_fini();

		if(mod_user.qos_list)
			list_destroy(mod_user.qos_list);
					
/* 		if(ret_list && list_count(ret_list)) { */
/* 			char *object = NULL; */
/* 			ListIterator itr = list_iterator_create(ret_list); */
/* 			printf(" Modified user defaults for " */
/* 			       "associations...\n"); */
/* 			while((object = list_next(itr)))  */
/* 				printf("  %s\n", object); */
/* 			list_iterator_destroy(itr); */
/* 		} */
 
		if(ret_list) {
			printf("%s", my_info);
			list_destroy(ret_list);
			set = 1;
		} 
		xfree(my_info);
	}
	xfree(def_acct);

	if((!user->coord_accts || !list_count(user->coord_accts))
		  && (file_opts->coord_list 
		      && list_count(file_opts->coord_list))) {
		ListIterator coord_itr = NULL;
		char *temp_char = NULL;
		acct_coord_rec_t *coord = NULL;
		int first = 1;
		notice_thread_init();
		rc = acct_storage_g_add_coord(db_conn, my_uid, 
					      file_opts->coord_list,
					      &user_cond);
		notice_thread_fini();

		user->coord_accts = list_create(destroy_acct_coord_rec);
		coord_itr = list_iterator_create(file_opts->coord_list);
		printf(" Making User '%s' coordinator for account(s)",
		       user->name);
		while((temp_char = list_next(coord_itr))) {
			coord = xmalloc(sizeof(acct_coord_rec_t));
			coord->name = xstrdup(temp_char);
			coord->direct = 1;
			list_push(user->coord_accts, coord);

			if(first) {
				printf(" %s", temp_char);
				first = 0;
			} else
				printf(", %s", temp_char);
		}
		list_iterator_destroy(coord_itr);
		printf("\n");
		set = 1;
	} else if((user->coord_accts && list_count(user->coord_accts))
		  && (file_opts->coord_list 
		      && list_count(file_opts->coord_list))) {
		ListIterator coord_itr = NULL;
		ListIterator char_itr = NULL;
		char *temp_char = NULL;
		acct_coord_rec_t *coord = NULL;
		List add_list = list_create(NULL);

		coord_itr = list_iterator_create(user->coord_accts);
		char_itr = list_iterator_create(file_opts->coord_list);

		while((temp_char = list_next(char_itr))) {
			while((coord = list_next(coord_itr))) {
				if(!coord->direct)
					continue;
				if(!strcmp(coord->name, temp_char)) {
					break;
				}
			}
			if(!coord) {
				printf(" Making User '%s' coordinator of "
				       "account '%s'\n",
				       user->name,
				       temp_char);
					
				list_append(add_list, temp_char);
			}
			list_iterator_reset(coord_itr);
		}

		list_iterator_destroy(char_itr);
		list_iterator_destroy(coord_itr);

		if(list_count(add_list)) {
			notice_thread_init();
			rc = acct_storage_g_add_coord(db_conn, my_uid, 
						      add_list,
						      &user_cond);
			notice_thread_fini();
			set = 1;
		}
		list_destroy(add_list);
	}
	list_destroy(assoc_cond.user_list);

	return set;
}

static int _mod_assoc(sacctmgr_file_opts_t *file_opts,
		      acct_association_rec_t *assoc,
		      sacctmgr_mod_type_t mod_type)
{
	int changed = 0;
	acct_association_rec_t mod_assoc;
	acct_association_cond_t assoc_cond;
	char *type = NULL;
	char *name = NULL;
	char *my_info = NULL;

	switch(mod_type) {
	case MOD_CLUSTER:
		type = "Cluster";
		name = assoc->cluster;
		break;
	case MOD_ACCT:
		type = "Account";
		name = assoc->acct;
		break;
	case MOD_USER:
		type = "User";
		name = assoc->user;
		break;
	default:
		return 0;
		break;
	}

	memset(&mod_assoc, 0, sizeof(acct_association_rec_t));

	mod_assoc.fairshare = NO_VAL;
	mod_assoc.max_cpu_mins_pj = NO_VAL;
	mod_assoc.max_jobs = NO_VAL;
	mod_assoc.max_nodes_pj = NO_VAL;
	mod_assoc.max_wall_pj = NO_VAL;

	memset(&assoc_cond, 0, sizeof(acct_association_cond_t));

	assoc_cond.fairshare = NO_VAL;
	assoc_cond.max_cpu_mins_pj = NO_VAL;
	assoc_cond.max_jobs = NO_VAL;
	assoc_cond.max_nodes_pj = NO_VAL;
	assoc_cond.max_wall_pj = NO_VAL;

	if(assoc->fairshare != file_opts->fairshare) {
		mod_assoc.fairshare = file_opts->fairshare;
		changed = 1;
		xstrfmtcat(my_info, 
			   "%-30.30s for %-7.7s %-10.10s %8d -> %d\n",
			   " Changed fairshare",
			   type, name,
			   assoc->fairshare,
			   file_opts->fairshare);
	}
	if(assoc->max_cpu_mins_pj != file_opts->max_cpu_mins_pj) {
		mod_assoc.max_cpu_mins_pj =
			file_opts->max_cpu_mins_pj;
		changed = 1;
		xstrfmtcat(my_info, 
			   "%-30.30s for %-7.7s %-10.10s %8d -> %d\n",
			   " Changed MaxCPUMinsPerJob",
			   type, name,
			   assoc->max_cpu_mins_pj,
			   file_opts->max_cpu_mins_pj);
	}
	if(assoc->max_jobs != file_opts->max_jobs) {
		mod_assoc.max_jobs = file_opts->max_jobs;
		changed = 1;
		xstrfmtcat(my_info, 
			   "%-30.30s for %-7.7s %-10.10s %8d -> %d\n",
			   " Changed MaxJobs",
			   type, name,
			   assoc->max_jobs,
			   file_opts->max_jobs);
	}
	if(assoc->max_nodes_pj != file_opts->max_nodes_pj) {
		mod_assoc.max_nodes_pj = file_opts->max_nodes_pj;
		changed = 1;
		xstrfmtcat(my_info, 
			   "%-30.30s for %-7.7s %-10.10s %8d -> %d\n",
			   " Changed MaxNodesPerJob",
			   type, name,
			   assoc->max_nodes_pj, 
			   file_opts->max_nodes_pj);
	}
	if(assoc->max_wall_pj !=
	   file_opts->max_wall_pj) {
		mod_assoc.max_wall_pj =
			file_opts->max_wall_pj;
		changed = 1;
		xstrfmtcat(my_info, 
			   "%-30.30s for %-7.7s %-10.10s %8d -> %d\n",
			   " Changed MaxWallDurationPerJob",
			   type, name,
			   assoc->max_wall_pj,
			   file_opts->max_wall_pj);
	}

	if(changed) {
		List ret_list = NULL;
					
		assoc_cond.cluster_list = list_create(NULL); 
		list_push(assoc_cond.cluster_list, assoc->cluster);

		if(mod_type >= MOD_ACCT) {
			assoc_cond.acct_list = list_create(NULL); 
			list_push(assoc_cond.acct_list, assoc->acct);
		}

		if(mod_type == MOD_USER) {
			assoc_cond.user_list = list_create(NULL); 
			list_push(assoc_cond.user_list, assoc->user);
			if(assoc->partition) {
				assoc_cond.partition_list = list_create(NULL); 
				list_push(assoc_cond.partition_list,
					  assoc->partition);
			}
		}
			
		notice_thread_init();
		ret_list = acct_storage_g_modify_associations(
			db_conn, my_uid,
			&assoc_cond, 
			&mod_assoc);
		notice_thread_fini();
					
		list_destroy(assoc_cond.cluster_list);
		list_destroy(assoc_cond.acct_list);
		if(assoc_cond.user_list)
			list_destroy(assoc_cond.user_list);
		if(assoc_cond.partition_list)
			list_destroy(assoc_cond.partition_list);

/* 		if(ret_list && list_count(ret_list)) { */
/* 			char *object = NULL; */
/* 			ListIterator itr = list_iterator_create(ret_list); */
/* 			printf(" Modified account defaults for " */
/* 			       "associations...\n"); */
/* 			while((object = list_next(itr)))  */
/* 				printf("  %s\n", object); */
/* 			list_iterator_destroy(itr); */
/* 		} */
 
		if(ret_list) {
			printf("%s", my_info);
			list_destroy(ret_list);
		} else
			changed = 0;
		xfree(my_info);
	}

	return changed;
}

static acct_user_rec_t *_set_user_up(sacctmgr_file_opts_t *file_opts,
				     char *parent)
{
	acct_user_rec_t *user = xmalloc(sizeof(acct_user_rec_t));

	user->assoc_list = NULL;
	user->name = xstrdup(file_opts->name);
	
	if(file_opts->def_acct)
		user->default_acct = xstrdup(file_opts->def_acct);
	else
		user->default_acct = xstrdup(parent);
	
	user->qos_list = file_opts->qos_list;
	file_opts->qos_list = NULL;
	user->admin_level = file_opts->admin;
	
	if(file_opts->coord_list) {
		acct_user_cond_t user_cond;
		acct_association_cond_t assoc_cond;
		ListIterator coord_itr = NULL;
		char *temp_char = NULL;
		acct_coord_rec_t *coord = NULL;
		
		memset(&user_cond, 0, sizeof(acct_user_cond_t));
		memset(&assoc_cond, 0, sizeof(acct_association_cond_t));
		assoc_cond.user_list = list_create(NULL);
		list_append(assoc_cond.user_list, user->name);
		user_cond.assoc_cond = &assoc_cond;
		
		notice_thread_init();
		acct_storage_g_add_coord(db_conn, my_uid, 
					 file_opts->coord_list,
					 &user_cond);
		notice_thread_fini();
		list_destroy(assoc_cond.user_list);
		user->coord_accts = list_create(destroy_acct_coord_rec);
		coord_itr = list_iterator_create(file_opts->coord_list);
		while((temp_char = list_next(coord_itr))) {
			coord = xmalloc(sizeof(acct_coord_rec_t));
			coord->name = xstrdup(temp_char);
			coord->direct = 1;
			list_push(user->coord_accts, coord);
		}
		list_iterator_destroy(coord_itr);
	}
	return user;
}


static acct_account_rec_t *_set_acct_up(sacctmgr_file_opts_t *file_opts,
					char *parent)
{
	acct_account_rec_t *acct = xmalloc(sizeof(acct_account_rec_t));
	acct->assoc_list = NULL;	
	acct->name = xstrdup(file_opts->name);
	if(file_opts->desc) 
		acct->description = xstrdup(file_opts->desc);
	else
		acct->description = xstrdup(file_opts->name);
	if(file_opts->org)
		acct->organization = xstrdup(file_opts->org);
	else if(strcmp(parent, "root"))
		acct->organization = xstrdup(parent);
	else
		acct->organization = xstrdup(file_opts->name);
	/* info("adding acct %s (%s) (%s)", */
/* 	        acct->name, acct->description, */
/* 		acct->organization); */
	acct->qos_list = file_opts->qos_list;
	file_opts->qos_list = NULL;

	return acct;
}

static int _print_file_sacctmgr_assoc_childern(FILE *fd, 
					       List sacctmgr_assoc_list,
					       List user_list,
					       List acct_list)
{
	ListIterator itr = NULL;
	sacctmgr_assoc_t *sacctmgr_assoc = NULL;
	char *line = NULL;
	acct_user_rec_t *user_rec = NULL;
	acct_account_rec_t *acct_rec = NULL;

	itr = list_iterator_create(sacctmgr_assoc_list);
	while((sacctmgr_assoc = list_next(itr))) {
		if(sacctmgr_assoc->assoc->user) {
			user_rec = sacctmgr_find_user_from_list(
				user_list, sacctmgr_assoc->assoc->user);
			line = xstrdup_printf(
				"User - %s", sacctmgr_assoc->sort_name);
			if(user_rec) {
				xstrfmtcat(line, ":DefaultAccount='%s'",
					   user_rec->default_acct);
				if(user_rec->admin_level > ACCT_ADMIN_NONE)
					xstrfmtcat(line, ":AdminLevel='%s'",
						   acct_admin_level_str(
							   user_rec->
							   admin_level));
				if(user_rec->qos_list 
				   && list_count(user_rec->qos_list)) {
					char *temp_char = NULL;
					if(!qos_list) {
						qos_list = 
							acct_storage_g_get_qos(
								db_conn, my_uid,
								NULL);
					}
					temp_char = get_qos_complete_str(
						qos_list, user_rec->qos_list);
					xstrfmtcat(line, ":QOS='%s'",
						   temp_char);
					xfree(temp_char);
				}

				if(user_rec->coord_accts
				   && list_count(user_rec->coord_accts)) {
					ListIterator itr2 = NULL;
					acct_coord_rec_t *coord = NULL;
					int first_coord = 1;
					list_sort(user_rec->coord_accts,
						  (ListCmpF)sort_coord_list);
					itr2 = list_iterator_create(
						user_rec->coord_accts);
					while((coord = list_next(itr2))) {
						/* We only care about
						 * the direct accounts here
						 */
						if(!coord->direct) 
							continue;
						if(first_coord) {
							xstrfmtcat(
								line,
								":Coordinator"
								"='%s",
								coord->name);
							first_coord = 0;
						} else {
							xstrfmtcat(line, ",%s",
								   coord->name);
						}				
					}
					if(!first_coord)
						xstrcat(line, "'");
					list_iterator_destroy(itr2);
				}
			}
		} else {
			acct_rec = sacctmgr_find_account_from_list(
				acct_list, sacctmgr_assoc->assoc->acct);
			line = xstrdup_printf(
				"Account - %s", sacctmgr_assoc->sort_name);
			if(acct_rec) {
				xstrfmtcat(line, ":Description='%s'",
					   acct_rec->description);
				xstrfmtcat(line, ":Organization='%s'",
					   acct_rec->organization);
				if(acct_rec->qos_list) {
					char *temp_char = get_qos_complete_str(
						qos_list, acct_rec->qos_list);
					if(temp_char) {			
						xstrfmtcat(line, ":QOS='%s'",
							   temp_char);
						xfree(temp_char);
					}
				}
			}
		}
		if(sacctmgr_assoc->assoc->partition) 
			xstrfmtcat(line, ":Partition='%s'", 
				   sacctmgr_assoc->assoc->partition);
			
		if(sacctmgr_assoc->assoc->fairshare != INFINITE)
			xstrfmtcat(line, ":Fairshare=%u", 
				   sacctmgr_assoc->assoc->fairshare);
		
		if(sacctmgr_assoc->assoc->max_cpu_mins_pj != INFINITE)
			xstrfmtcat(line, ":MaxCPUMins=%u",
				   sacctmgr_assoc->assoc->max_cpu_mins_pj);
		
		if(sacctmgr_assoc->assoc->max_jobs != INFINITE) 
			xstrfmtcat(line, ":MaxJobs=%u",
				   sacctmgr_assoc->assoc->max_jobs);
		
		if(sacctmgr_assoc->assoc->max_nodes_pj != INFINITE)
			xstrfmtcat(line, ":MaxNodes=%u",
				   sacctmgr_assoc->assoc->max_nodes_pj);
		
		if(sacctmgr_assoc->assoc->max_wall_pj 
		   != INFINITE)
 			xstrfmtcat(line, ":MaxWallDurationPerJob=%u",
				   sacctmgr_assoc->assoc->
				   max_wall_pj);


		if(fprintf(fd, "%s\n", line) < 0) {
			exit_code=1;
			fprintf(stderr, " Can't write to file");
			return SLURM_ERROR;
		}
		info("%s", line);
	}
	list_iterator_destroy(itr);
	print_file_sacctmgr_assoc_list(fd, sacctmgr_assoc_list,
				       user_list, acct_list);

	return SLURM_SUCCESS;
}

extern int print_file_sacctmgr_assoc_list(FILE *fd, 
					  List sacctmgr_assoc_list,
					  List user_list,
					  List acct_list)
{
	ListIterator itr = NULL;
	sacctmgr_assoc_t *sacctmgr_assoc = NULL;

	itr = list_iterator_create(sacctmgr_assoc_list);
	while((sacctmgr_assoc = list_next(itr))) {
/* 		info("got here %d with %d from %s %s",  */
/* 		     depth, list_count(sacctmgr_assoc->childern), */
/* 		     sacctmgr_assoc->assoc->acct, sacctmgr_assoc->assoc->user); */
		if(!list_count(sacctmgr_assoc->childern))
			continue;
		if(fprintf(fd, "Parent - %s\n",
			   sacctmgr_assoc->assoc->acct) < 0) {
			error("Can't write to file");
			return SLURM_ERROR;
		}
		info("%s - %s", "Parent",
		       sacctmgr_assoc->assoc->acct);
/* 		info("sending %d from %s", */
/* 		     list_count(sacctmgr_assoc->childern), */
/* 		     sacctmgr_assoc->assoc->acct); */
		_print_file_sacctmgr_assoc_childern(
			fd, sacctmgr_assoc->childern, user_list, acct_list);
	}	
	list_iterator_destroy(itr);

	return SLURM_SUCCESS;
}

extern void load_sacctmgr_cfg_file (int argc, char *argv[])
{
	DEF_TIMERS;
	char line[BUFFER_SIZE];
	FILE *fd = NULL;
	char *parent = NULL;
	char *file_name = NULL;
	char *cluster_name = NULL;
	char *user_name = NULL;
	char object[25];
	int start = 0, len = 0, i = 0;
	int lc=0, num_lines=0;
	int start_clean=0;
	int cluster_name_set=0;
	int rc = SLURM_SUCCESS;

	sacctmgr_file_opts_t *file_opts = NULL;
	acct_association_rec_t *assoc = NULL, *assoc2 = NULL;
	acct_account_rec_t *acct = NULL, *acct2 = NULL;
	acct_cluster_rec_t *cluster = NULL;
	acct_user_rec_t *user = NULL, *user2 = NULL;
	acct_user_cond_t user_cond;

	List curr_assoc_list = NULL;
	List curr_acct_list = NULL;
	List curr_cluster_list = NULL;
	List curr_user_list = NULL;

	/* This will be freed in their local counter parts */
	List mod_acct_list = NULL;
	List acct_list = NULL;
	List acct_assoc_list = NULL;
	List mod_user_list = NULL;
	List user_list = NULL;
	List user_assoc_list = NULL;
	List mod_assoc_list = NULL;

	ListIterator itr;
	ListIterator itr2;

	List print_fields_list;
	List format_list = NULL;
	print_field_t *field = NULL;
	
	int set = 0;
	
	if(readonly_flag) {
		exit_code = 1;
		fprintf(stderr, "Can't run this command in readonly mode.\n");
		return;		
	}

	/* reset the connection to get the most recent stuff */
	acct_storage_g_commit(db_conn, 0);

	memset(&user_cond, 0, sizeof(acct_user_cond_t));
	user_cond.with_coords = 1;
	curr_user_list = acct_storage_g_get_users(db_conn, my_uid, &user_cond);

	/* make sure this person running is an admin */
	user_name = uid_to_string(my_uid);
	if(!(user = sacctmgr_find_user_from_list(curr_user_list, user_name))) {
		exit_code=1;
		fprintf(stderr, " Your uid (%u) is not in the "
			"accounting system, can't load file.\n", my_uid);
		if(curr_user_list)
			list_destroy(curr_user_list);
		xfree(user_name);
		return;
		
	} else {
		if(my_uid != slurm_get_slurm_user_id() && my_uid != 0
		   && user->admin_level < ACCT_ADMIN_SUPER_USER) {
			exit_code=1;
			fprintf(stderr, " Your user does not have sufficient "
				"privileges to load files.\n");
			if(curr_user_list)
				list_destroy(curr_user_list);
			xfree(user_name);
			return;
		}
	}
	xfree(user_name);

	for (i=0; i<argc; i++) {
		int end = parse_option_end(argv[i]);

		if(!end && !strncasecmp(argv[i], "clean", 3)) {
			start_clean = 1;
		} else if(!end || !strncasecmp (argv[i], "File", 1)) {
			if(file_name) {
				exit_code=1;
				fprintf(stderr, 
					" File name already set to %s\n",
					file_name);
				continue;
			}		
			file_name = xstrdup(argv[i]+end);
		} else if (!strncasecmp (argv[i], "Cluster", 3)) {
			if(cluster_name) {
				exit_code=1;
				fprintf(stderr, 
					" Can only do one cluster at a time.  "
					"Already doing %s\n", cluster_name);
				continue;
			}
			cluster_name = xstrdup(argv[i]+end);
			cluster_name_set = 1;
		} else {
			exit_code=1;
			fprintf(stderr, " Unknown option: %s\n", argv[i]);
		}		
	}

	if(!file_name) {
		exit_code=1;
		xfree(cluster_name);
		fprintf(stderr, 
			" No filename given, specify one with file=''\n");
		return;
		
	}

	fd = fopen(file_name, "r");
	xfree(file_name);
	if (fd == NULL) {
		exit_code=1;
		fprintf(stderr, " Unable to read \"%s\": %m\n", argv[0]);
		xfree(cluster_name);
		return;
	}

	curr_acct_list = acct_storage_g_get_accounts(db_conn, my_uid, NULL);

	/* These are new info so they need to be freed here */
	acct_list = list_create(destroy_acct_account_rec);
	acct_assoc_list = list_create(destroy_acct_association_rec);
	user_list = list_create(destroy_acct_user_rec);
	user_assoc_list = list_create(destroy_acct_association_rec);
	
	mod_acct_list = list_create(destroy_acct_account_rec);
	mod_user_list = list_create(destroy_acct_user_rec);
	mod_assoc_list = list_create(destroy_acct_association_rec);

	format_list = list_create(slurm_destroy_char);	

	while((num_lines = _get_next_line(line, BUFFER_SIZE, fd)) > 0) {
		lc += num_lines;
		/* skip empty lines */
		if (line[0] == '\0') {
			continue;
		}
		len = strlen(line);

		memset(object, 0, sizeof(object));

		/* first find the object */
		start=0;
		for(i=0; i<len; i++) {
			if(line[i] == '-') {
				start = i;
				if(line[i-1] == ' ') 
					i--;
				if(i<sizeof(object))
					strncpy(object, line, i);
				break;
			} 
		}
		if(!object[0]) 
			continue;
		
		while(line[start] != ' ' && start<len)
			start++;
		if(start>=len) {
			exit_code=1;
			fprintf(stderr, " Nothing after object "
				"name '%s'. line(%d)\n",
				object, lc);
			rc = SLURM_ERROR;
			break;
		}
		start++;
		
		if(!strcasecmp("Machine", object) 
		   || !strcasecmp("Cluster", object)) {
			acct_association_cond_t assoc_cond;

			if(cluster_name && !cluster_name_set) {
				exit_code=1;
				fprintf(stderr, " You can only add one cluster "
				       "at a time.\n");
				rc = SLURM_ERROR;
				break;
			}

			file_opts = _parse_options(line+start);
			
			if(!file_opts) {
				exit_code=1;
				fprintf(stderr, 
					" error: Problem with line(%d)\n", lc);
				rc = SLURM_ERROR;
				break;
			}

			if(!cluster_name_set)
				cluster_name = xstrdup(file_opts->name);
			if(start_clean) {
				acct_cluster_cond_t cluster_cond;
				List ret_list = NULL;

				if(!commit_check("You requested to flush "
						 "the cluster before "
						 "adding it again.\n"
						 "Are you sure you want "
						 "to continue?")) {
					printf("Aborted\n");
					break;
				}		

				memset(&cluster_cond, 0, 
				       sizeof(acct_cluster_cond_t));
				cluster_cond.cluster_list = list_create(NULL);
				list_append(cluster_cond.cluster_list,
					    cluster_name);

				notice_thread_init();
				ret_list = acct_storage_g_remove_clusters(
					db_conn, my_uid, &cluster_cond);
				notice_thread_fini();
				list_destroy(cluster_cond.cluster_list);

				if(!ret_list) {
					exit_code=1;
					fprintf(stderr, " There was a problem "
						"removing the cluster.\n");
					rc = SLURM_ERROR;
					break;
				}
			}
			curr_cluster_list = acct_storage_g_get_clusters(
				db_conn, my_uid, NULL);

			if(cluster_name)
				info("For cluster %s", cluster_name);

			if(!(cluster = sacctmgr_find_cluster_from_list(
				     curr_cluster_list, cluster_name))) {
				List cluster_list =
					list_create(destroy_acct_cluster_rec);
				cluster = xmalloc(sizeof(acct_cluster_rec_t));
				list_append(cluster_list, cluster);
				cluster->name = xstrdup(cluster_name);
				cluster->default_fairshare =
					file_opts->fairshare;		
				cluster->default_max_cpu_mins_pj = 
					file_opts->max_cpu_mins_pj;
				cluster->default_max_jobs = file_opts->max_jobs;
				cluster->default_max_nodes_pj = 
					file_opts->max_nodes_pj;
				cluster->default_max_wall_pj = 
					file_opts->max_wall_pj;
				notice_thread_init();
				rc = acct_storage_g_add_clusters(
					db_conn, my_uid, cluster_list);
				notice_thread_fini();
				list_destroy(cluster_list);

				if(rc != SLURM_SUCCESS) {
					exit_code=1;
					fprintf(stderr, 
						" Problem adding cluster\n");
					rc = SLURM_ERROR;
					_destroy_sacctmgr_file_opts(file_opts);
					break;
				}
				set = 1;
			} else {
				set = _mod_cluster(file_opts, cluster);
			}
				     
			_destroy_sacctmgr_file_opts(file_opts);
			
			memset(&assoc_cond, 0, sizeof(acct_association_cond_t));
			assoc_cond.cluster_list = list_create(NULL);
			assoc_cond.without_parent_limits = 1;
			list_append(assoc_cond.cluster_list, cluster_name);
			curr_assoc_list = acct_storage_g_get_associations(
				db_conn, my_uid, &assoc_cond);
			list_destroy(assoc_cond.cluster_list);

			if(!curr_assoc_list) {
				exit_code=1;
				fprintf(stderr, " Problem getting associations "
					"for this cluster\n");
				rc = SLURM_ERROR;
				break;
			}
			//info("got %d assocs", list_count(curr_assoc_list));
			continue;
		} else if(!cluster_name) {
			exit_code=1;
			fprintf(stderr, " You need to specify a cluster name "
				"first with 'Cluster - $NAME' in your file\n");
			break;
		}
		
		if(!strcasecmp("Parent", object)) {
			xfree(parent);
			
			i = start;
			while(line[i] != '\n' && i<len)
				i++;
			
			if(i >= len) {
				exit_code=1;
				fprintf(stderr, " No parent name "
					"given line(%d)\n",
				       lc);
				rc = SLURM_ERROR;
				break;
			}
			parent = xstrndup(line+start, i-start);
			//info("got parent %s", parent);
			if(!sacctmgr_find_account_base_assoc_from_list(
				   curr_assoc_list, parent, cluster_name)
			   && !sacctmgr_find_account_base_assoc_from_list(
				   acct_assoc_list, parent, cluster_name)) {
				exit_code=1;
				fprintf(stderr, " line(%d) You need to add "
				       "this parent (%s) as a child before "
				       "you can add childern to it.\n",
				       lc, parent);
				break;
			}
			continue;
		} else if(!parent) {
			parent = xstrdup("root");
			printf(" No parent given creating off root, "
			       "If incorrect specify 'Parent - name' "
			       "before any childern in your file\n");
		} 
	
		if(!strcasecmp("Project", object)
		   || !strcasecmp("Account", object)) {
			file_opts = _parse_options(line+start);
			
			if(!file_opts) {
				exit_code=1;
				fprintf(stderr, " Problem with line(%d)\n", lc);
				rc = SLURM_ERROR;
				break;
			}
			
			//info("got a project %s of %s", file_opts->name, parent);
			if(!(acct = sacctmgr_find_account_from_list(
				     curr_acct_list, file_opts->name))
			   && !sacctmgr_find_account_from_list(
				   acct_list, file_opts->name)) {
				acct = _set_acct_up(file_opts, parent);
				list_append(acct_list, acct);
				/* don't add anything to the
				   curr_acct_list */

				assoc = xmalloc(sizeof(acct_association_rec_t));
				assoc->acct = xstrdup(file_opts->name);
				assoc->cluster = xstrdup(cluster_name);
				assoc->parent_acct = xstrdup(parent);
				assoc->fairshare = file_opts->fairshare;
				assoc->max_jobs = file_opts->max_jobs;
				assoc->max_nodes_pj =
					file_opts->max_nodes_pj;
				assoc->max_wall_pj =
					file_opts->max_wall_pj;
				assoc->max_cpu_mins_pj = 
					file_opts->max_cpu_mins_pj;
				list_append(acct_assoc_list, assoc);
				/* don't add anything to the
				   curr_assoc_list */
			} else if(!(assoc =
				    sacctmgr_find_account_base_assoc_from_list(
					    curr_assoc_list, file_opts->name,
					    cluster_name)) &&
				  !sacctmgr_find_account_base_assoc_from_list(
					  acct_assoc_list, file_opts->name,
					  cluster_name)) {

				acct2 = sacctmgr_find_account_from_list(
					mod_acct_list, file_opts->name);

				if(!acct2) {
					acct2 = xmalloc(
						sizeof(acct_account_rec_t));
					list_append(mod_acct_list, acct2);
					acct2->name = xstrdup(file_opts->name);
					if(_mod_acct(file_opts, acct, parent))
						set = 1;
				} else {
					debug2("already modified this account");
				}
				assoc = xmalloc(sizeof(acct_association_rec_t));
				assoc->acct = xstrdup(file_opts->name);
				assoc->cluster = xstrdup(cluster_name);
				assoc->parent_acct = xstrdup(parent);
				assoc->fairshare = file_opts->fairshare;
				assoc->max_jobs = file_opts->max_jobs;
				assoc->max_nodes_pj =
					file_opts->max_nodes_pj;
				assoc->max_wall_pj =
					file_opts->max_wall_pj;
				assoc->max_cpu_mins_pj = 
					file_opts->max_cpu_mins_pj;
				list_append(acct_assoc_list, assoc);
				/* don't add anything to the
				   curr_assoc_list */
			} else if(assoc) {
				acct2 = sacctmgr_find_account_from_list(
					mod_acct_list, file_opts->name);

				if(!acct2) {
					acct2 = xmalloc(
						sizeof(acct_account_rec_t));
					list_append(mod_acct_list, acct2);
					acct2->name = xstrdup(file_opts->name);
					if(_mod_acct(file_opts, acct, parent))
						set = 1;
				} else {
					debug2("already modified this account");
				}
				assoc2 = sacctmgr_find_association_from_list(
					mod_assoc_list,
					NULL, file_opts->name,
					cluster_name,
					NULL);

				if(!assoc2) {
					assoc2 = xmalloc(
						sizeof(acct_association_rec_t));
					list_append(mod_assoc_list, assoc2);
					assoc2->cluster = xstrdup(cluster_name);
					assoc2->acct = xstrdup(file_opts->name);
					if(_mod_assoc(file_opts, 
						      assoc, MOD_ACCT))
						set = 1;
				} else {
					debug2("already modified this assoc");
				}
			}
			_destroy_sacctmgr_file_opts(file_opts);
			continue;
		} else if(!strcasecmp("User", object)) {
			file_opts = _parse_options(line+start);
			
			if(!file_opts) {
				exit_code=1;
				fprintf(stderr, " Problem with line(%d)\n", lc);
				rc = SLURM_ERROR;
				break;
			}

			if(!(user = sacctmgr_find_user_from_list(
				     curr_user_list, file_opts->name))
			   && !sacctmgr_find_user_from_list(
				   user_list, file_opts->name)) {

				user = _set_user_up(file_opts, parent);
				list_append(user_list, user);
				/* don't add anything to the
				   curr_user_list */

				assoc = xmalloc(sizeof(acct_association_rec_t));
				assoc->acct = xstrdup(parent);
				assoc->cluster = xstrdup(cluster_name);
				assoc->fairshare = file_opts->fairshare;
				assoc->max_jobs = file_opts->max_jobs;
				assoc->max_nodes_pj =
					file_opts->max_nodes_pj;
				assoc->max_wall_pj =
					file_opts->max_wall_pj;
				assoc->max_cpu_mins_pj = 
					file_opts->max_cpu_mins_pj;
				assoc->partition = xstrdup(file_opts->part);
				assoc->user = xstrdup(file_opts->name);
				
				list_append(user_assoc_list, assoc);
				/* don't add anything to the
				   curr_assoc_list */
			} else if(!(assoc = sacctmgr_find_association_from_list(
					    curr_assoc_list,
					    file_opts->name, parent,
					    cluster_name, file_opts->part)) 
				  && !sacctmgr_find_association_from_list(
					  user_assoc_list,
					  file_opts->name, parent,
					  cluster_name,
					  file_opts->part)) {

				/* This means the user was added
				 * during this round but this is a new
				 * association we are adding
				 */
				if(!user) 
					goto new_association;

				/* This means there could be a change
				 * on the user.
				 */
				user2 = sacctmgr_find_user_from_list(
					mod_user_list, file_opts->name);
				if(!user2) {
					user2 = xmalloc(
						sizeof(acct_user_rec_t));
					list_append(mod_user_list, user2);
					user2->name = xstrdup(file_opts->name);
					if(_mod_user(file_opts, user, parent))
						set = 1;
				} else {
					debug2("already modified this user");
				}
			new_association:
				assoc = xmalloc(sizeof(acct_association_rec_t));
				assoc->acct = xstrdup(parent);
				assoc->cluster = xstrdup(cluster_name);
				assoc->fairshare = file_opts->fairshare;
				assoc->max_jobs = file_opts->max_jobs;
				assoc->max_nodes_pj =
					file_opts->max_nodes_pj;
				assoc->max_wall_pj =
					file_opts->max_wall_pj;
				assoc->max_cpu_mins_pj = 
					file_opts->max_cpu_mins_pj;
				assoc->partition = xstrdup(file_opts->part);
				assoc->user = xstrdup(file_opts->name);
				
				list_append(user_assoc_list, assoc);
				/* don't add anything to the
				   curr_assoc_list */
			} else if(assoc) {
				user2 = sacctmgr_find_user_from_list(
					mod_user_list, file_opts->name);
				if(!user2) {
					user2 = xmalloc(
						sizeof(acct_user_rec_t));
					list_append(mod_user_list, user2);
					user2->name = xstrdup(file_opts->name);
					if(_mod_user(file_opts, user, parent))
						set = 1;
				} else {
					debug2("already modified this user");
				}

				assoc2 = sacctmgr_find_association_from_list(
					mod_assoc_list,
					file_opts->name, parent,
					cluster_name,
					file_opts->part);

				if(!assoc2) {
					assoc2 = xmalloc(
						sizeof(acct_association_rec_t));
					list_append(mod_assoc_list, assoc2);
					assoc2->cluster = xstrdup(cluster_name);
					assoc2->acct = xstrdup(parent);
					assoc2->user = xstrdup(file_opts->name);
					assoc2->partition =
						xstrdup(file_opts->part);
					if(_mod_assoc(file_opts, 
						      assoc, MOD_USER))
						set = 1;
				} else {
					debug2("already modified this assoc");
				}
			}
			//info("got a user %s", file_opts->name);
			_destroy_sacctmgr_file_opts(file_opts);
			continue;
		} else {
			exit_code=1;
			fprintf(stderr, 
				" Misformatted line(%d): %s\n", lc, line);
			rc = SLURM_ERROR;
			break;
		}
	}
	fclose(fd);
	xfree(cluster_name);
	xfree(parent);
		
	START_TIMER;
	if(rc == SLURM_SUCCESS && list_count(acct_list)) {
		printf("Accounts\n");
		slurm_addto_char_list(format_list,
				"Name,Description,Organization,QOS");

		print_fields_list = _set_up_print_fields(format_list);
		list_flush(format_list);
		
		print_fields_header(print_fields_list);

		itr = list_iterator_create(acct_list);
		itr2 = list_iterator_create(print_fields_list);
		while((acct = list_next(itr))) {
			while((field = list_next(itr2))) {
				switch(field->type) {
				case PRINT_DESC:
					field->print_routine(
						field, acct->description);
					break;
				case PRINT_NAME:
					field->print_routine(
						field, acct->name);
					break;
				case PRINT_ORG:
					field->print_routine(
						field, acct->organization);
					break;
				case PRINT_QOS:
					field->print_routine(
						field,
						qos_list,
						acct->qos_list);
					break;
				default:
					break;
				}
			}
			list_iterator_reset(itr2);
			printf("\n");
		}
		list_iterator_destroy(itr);
		list_iterator_destroy(itr2);
		list_destroy(print_fields_list);
		rc = acct_storage_g_add_accounts(db_conn, my_uid, acct_list);
		printf("-----------------------------"
		       "----------------------\n\n");
		set = 1;
	}
	
	if(rc == SLURM_SUCCESS && list_count(acct_assoc_list)) {
		printf("Account Associations\n");
		_print_out_assoc(acct_assoc_list, 0);
		set = 1;
	}
	if(rc == SLURM_SUCCESS && list_count(user_list)) {
		printf("Users\n");

		slurm_addto_char_list(format_list, 
				      "Name,Default,QOS,Admin,Coord");

		print_fields_list = _set_up_print_fields(format_list);
		list_flush(format_list);
		print_fields_header(print_fields_list);

		itr = list_iterator_create(user_list);
		itr2 = list_iterator_create(print_fields_list);
		while((user = list_next(itr))) {
			while((field = list_next(itr2))) {
				switch(field->type) {
				case PRINT_ADMIN:
					field->print_routine(
						field,
						acct_admin_level_str(
							user->admin_level));
					break;
				case PRINT_COORDS:
					field->print_routine(
						field,
						user->coord_accts);
					break;
				case PRINT_DACCT:
					field->print_routine(
						field,
						user->default_acct);
					break;
				case PRINT_NAME:
					field->print_routine(
						field, user->name);
					break;
				case PRINT_QOS:
					field->print_routine(
						field,
						qos_list,
						user->qos_list);
					break;
				default:
					break;
				}
			}
			list_iterator_reset(itr2);
			printf("\n");
		}
		list_iterator_destroy(itr);
		list_iterator_destroy(itr2);
		list_destroy(print_fields_list);
		
		rc = acct_storage_g_add_users(db_conn, my_uid, user_list);
		printf("---------------------------"
		       "------------------------\n\n");
		set = 1;
	}
	
	if(rc == SLURM_SUCCESS && list_count(user_assoc_list)) {
		printf("User Associations\n");
		_print_out_assoc(user_assoc_list, 1);
		set = 1;
	}
	END_TIMER2("add cluster");

	if(set)
		info("Done adding cluster in %s", TIME_STR);
		
	if(rc == SLURM_SUCCESS) {
		if(set) {
			if(commit_check("Would you like to commit changes?")) {
				acct_storage_g_commit(db_conn, 1);
			} else {
				printf(" Changes Discarded\n");
				acct_storage_g_commit(db_conn, 0);
			}
		} else {
			printf(" Nothing new added.\n");
		}
	} else {
		exit_code=1;
		fprintf(stderr, " Problem with requests.\n");
	}

	list_destroy(format_list);
	list_destroy(mod_acct_list);
	list_destroy(acct_list);
	list_destroy(acct_assoc_list);
	list_destroy(mod_user_list);
	list_destroy(user_list);
	list_destroy(user_assoc_list);
	list_destroy(mod_assoc_list);
	if(curr_acct_list)
		list_destroy(curr_acct_list);
	if(curr_assoc_list)
		list_destroy(curr_assoc_list);
	if(curr_cluster_list)
		list_destroy(curr_cluster_list);
	if(curr_user_list)
		list_destroy(curr_user_list);
}
