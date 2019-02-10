/*****************************************************************************\
 *  file_functions.c - functions dealing with files that are generated in the
 *                        accounting system.
 *****************************************************************************
 *  Copyright (C) 2008 Lawrence Livermore National Security.
 *  Copyright (C) 2002-2007 The Regents of the University of California.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Danny Auble <da@llnl.gov>
 *  CODE-OCEC-09-009. All rights reserved.
 *
 *  This file is part of Slurm, a resource management program.
 *  For details, see <https://slurm.schedmd.com/>.
 *  Please also read the included file: DISCLAIMER.
 *
 *  Slurm is free software; you can redistribute it and/or modify it under
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
 *  Slurm is distributed in the hope that it will be useful, but WITHOUT ANY
 *  WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 *  FOR A PARTICULAR PURPOSE.  See the GNU General Public License for more
 *  details.
 *
 *  You should have received a copy of the GNU General Public License along
 *  with Slurm; if not, write to the Free Software Foundation, Inc.,
 *  51 Franklin Street, Fifth Floor, Boston, MA 02110-1301  USA.
\*****************************************************************************/

#include "src/sacctmgr/sacctmgr.h"
#include "src/common/strlcpy.h"
#include "src/common/uid.h"

#define BUFFER_SIZE 4096

typedef struct {
	slurmdb_admin_level_t admin;
	slurmdb_assoc_rec_t assoc_rec;
	uint16_t classification;
	List coord_list; /* char *list */
	char *def_acct;
	char *def_wckey;
	char *desc;

	char *name;
	char *org;
	List wckey_list;
} sacctmgr_file_opts_t;

typedef enum {
	MOD_CLUSTER,
	MOD_ACCT,
	MOD_USER
} sacctmgr_mod_type_t;

static int _init_sacctmgr_file_opts(sacctmgr_file_opts_t *file_opts)
{
	if (!file_opts)
		return SLURM_ERROR;

	memset(file_opts, 0, sizeof(sacctmgr_file_opts_t));
	slurmdb_init_assoc_rec(&file_opts->assoc_rec, 0);
	file_opts->admin = SLURMDB_ADMIN_NOTSET;

	return SLURM_SUCCESS;
}

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
 * Concatenates together lines that are continued on
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

	if (file_opts) {
		slurmdb_free_assoc_rec_members(&file_opts->assoc_rec);
		FREE_NULL_LIST(file_opts->coord_list);
		xfree(file_opts->def_acct);
		xfree(file_opts->def_wckey);
		xfree(file_opts->desc);
		xfree(file_opts->name);
		xfree(file_opts->org);
		FREE_NULL_LIST(file_opts->wckey_list);
		xfree(file_opts);
	}
}

static sacctmgr_file_opts_t *_parse_options(char *options)
{
	int start=0, i=0, end=0, quote = 0;
 	char *sub = NULL;
	sacctmgr_file_opts_t *file_opts = xmalloc(sizeof(sacctmgr_file_opts_t));
	char *option = NULL;
	char quote_c = '\0';
	int command_len = 0;
	int option2 = 0;

	_init_sacctmgr_file_opts(file_opts);

	while (options[i]) {
		quote = 0;
		start=i;

		while (options[i] && options[i] != ':' && options[i] != '\n') {
			if (options[i] == '"' || options[i] == '\'') {
				if (quote) {
					if (options[i] == quote_c)
						quote = 0;
				} else {
					quote = 1;
					quote_c = options[i];
				}
			}
			i++;
		}
		if (quote) {
			while (options[i] && options[i] != quote_c)
				i++;
			if (!options[i])
				fatal("There is a problem with option "
				      "%s with quotes.", option);
			i++;
		}

		if (i-start <= 0)
			goto next_col;

		sub = xstrndup(options+start, i-start);
		end = parse_option_end(sub);
		command_len = end - 1;
		if (sub[end] == '=') {
			option2 = (int)sub[end-1];
			end++;
		}

		option = strip_quotes(sub+end, NULL, 1);

		if (!end) {
			if (file_opts->name) {
				exit_code=1;
				fprintf(stderr, " Bad format on %s: "
					"End your option with "
					"an '=' sign\n", sub);
				break;
			}
			file_opts->name = xstrdup(option);
		} else if (end && !strlen(option)) {
			debug("blank field given for %s discarding", sub);
		} else if (!xstrncasecmp(sub, "AdminLevel",
					 MAX(command_len, 2))) {
			file_opts->admin = str_2_slurmdb_admin_level(option);
		} else if (!xstrncasecmp(sub, "Coordinator",
					 MAX(command_len, 2))) {
			if (!file_opts->coord_list)
				file_opts->coord_list =
					list_create(slurm_destroy_char);
			slurm_addto_char_list(file_opts->coord_list, option);
		} else if (!xstrncasecmp(sub, "Classification",
					 MAX(command_len, 2))) {
			file_opts->classification =
				str_2_classification(option);
		} else if (!xstrncasecmp(sub, "DefaultAccount",
					 MAX(command_len, 8))) {
			file_opts->def_acct = xstrdup(option);
		} else if (!xstrncasecmp(sub, "DefaultWCKey",
					 MAX(command_len, 8))) {
			file_opts->def_wckey = xstrdup(option);
			if (!file_opts->wckey_list)
				file_opts->wckey_list =
					list_create(slurm_destroy_char);
			slurm_addto_char_list(file_opts->wckey_list, option);
		} else if (!xstrncasecmp(sub, "Description",
					 MAX(command_len, 3))) {
			file_opts->desc = xstrdup(option);
		} else if (!xstrncasecmp(sub, "Organization",
					 MAX(command_len, 1))) {
			file_opts->org = xstrdup(option);
		} else if (!xstrncasecmp(sub, "Partition",
					 MAX(command_len, 1))) {
			file_opts->assoc_rec.partition = xstrdup(option);
		} else if (!xstrncasecmp(sub, "WCKeys",
					 MAX(command_len, 2))) {
			if (!file_opts->wckey_list)
				file_opts->wckey_list =
					list_create(slurm_destroy_char);
			slurm_addto_char_list(file_opts->wckey_list, option);
		} else if (!sacctmgr_set_assoc_rec(
				   &file_opts->assoc_rec, sub, option,
				   command_len, option2)) {
			exit_code=1;
			fprintf(stderr, " Unknown option: %s\n", sub);
			break;
		}

		xfree(sub);
		xfree(option);

	next_col:
		if (options[i] == ':')
			i++;
		else
			break;
	}

	xfree(sub);
	xfree(option);

	if (!file_opts->name) {
		exit_code = 1;
		fprintf(stderr, " No name given\n");
	}

	if (exit_code) {
		_destroy_sacctmgr_file_opts(file_opts);
		file_opts = NULL;
	}

	return file_opts;
}

static int _print_out_assoc(List assoc_list, bool user, bool add)
{
	List format_list = NULL;
	List print_fields_list = NULL;
	ListIterator itr, itr2;
	print_field_t *field = NULL;
	slurmdb_assoc_rec_t *assoc = NULL;
	int rc = SLURM_SUCCESS;

	if (!assoc_list || !list_count(assoc_list))
		return rc;

	format_list = list_create(slurm_destroy_char);
	if (user)
		slurm_addto_char_list(format_list,
				      "User,Account");
	else
		slurm_addto_char_list(format_list,
				      "Account,ParentName");
	slurm_addto_char_list(format_list,
			      "Share,GrpTRESM,GrpTRESR,GrpTRES,GrpJ,GrpJobsA,"
			      "GrpMEM,GrpN,GrpS,GrpW,MaxTRESM,MaxTRES,"
			      "MaxTRESPerN,MaxJ,MaxS,MaxN,MaxW,QOS,DefaultQOS");

	print_fields_list = sacctmgr_process_format_list(format_list);
	FREE_NULL_LIST(format_list);

	print_fields_header(print_fields_list);

	itr = list_iterator_create(assoc_list);
	itr2 = list_iterator_create(print_fields_list);
	while ((assoc = list_next(itr))) {
		while ((field = list_next(itr2))) {
			sacctmgr_print_assoc_rec(assoc, field, NULL, 0);
		}
		list_iterator_reset(itr2);
		printf("\n");
	}
	list_iterator_destroy(itr);
	list_iterator_destroy(itr2);
	FREE_NULL_LIST(print_fields_list);
	if (add)
		rc = slurmdb_associations_add(db_conn, assoc_list);
	printf("--------------------------------------------------------------\n\n");

	return rc;
}

static int _mod_assoc(sacctmgr_file_opts_t *file_opts,
		      slurmdb_assoc_rec_t *assoc,
		      sacctmgr_mod_type_t mod_type,
		      char *parent)
{
	int changed = 0;
	slurmdb_assoc_rec_t mod_assoc;
	slurmdb_assoc_cond_t assoc_cond;
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
	slurmdb_init_assoc_rec(&mod_assoc, 0);
	memset(&assoc_cond, 0, sizeof(slurmdb_assoc_cond_t));

	if ((file_opts->assoc_rec.shares_raw != NO_VAL)
	    && (assoc->shares_raw != file_opts->assoc_rec.shares_raw)) {
		mod_assoc.shares_raw = file_opts->assoc_rec.shares_raw;
		changed = 1;
		xstrfmtcat(my_info,
			   "%-30.30s for %-7.7s %-10.10s %8d -> %d\n",
			   " Changed fairshare",
			   type, name,
			   assoc->shares_raw,
			   file_opts->assoc_rec.shares_raw);
	}

	if (file_opts->assoc_rec.grp_tres_mins
	    && xstrcmp(assoc->grp_tres_mins,
		       file_opts->assoc_rec.grp_tres_mins)) {
		mod_assoc.grp_tres_mins = file_opts->assoc_rec.grp_tres_mins;
		changed = 1;
		xstrfmtcat(my_info,
			   "%-30.30s for %-7.7s %-10.10s "
			   "%8s -> %s\n",
			   " Changed GrpTRESMins",
			   type, name,
			   assoc->grp_tres_mins,
			   file_opts->assoc_rec.grp_tres_mins);
	}

	if (file_opts->assoc_rec.grp_tres_run_mins
	    && xstrcmp(assoc->grp_tres_run_mins,
		file_opts->assoc_rec.grp_tres_run_mins)) {
		mod_assoc.grp_tres_run_mins =
			file_opts->assoc_rec.grp_tres_run_mins;
		changed = 1;
		xstrfmtcat(my_info,
			   "%-30.30s for %-7.7s %-10.10s "
			   "%8s -> %s\n",
			   " Changed GrpTRESRunMins",
			   type, name,
			   assoc->grp_tres_run_mins,
			   file_opts->assoc_rec.grp_tres_run_mins);
	}

	if (file_opts->assoc_rec.grp_tres
	    && xstrcmp(assoc->grp_tres, file_opts->assoc_rec.grp_tres)) {
		mod_assoc.grp_tres = file_opts->assoc_rec.grp_tres;
		changed = 1;
		xstrfmtcat(my_info,
			   "%-30.30s for %-7.7s %-10.10s %8s -> %s\n",
			   " Changed GrpTRES",
			   type, name,
			   assoc->grp_tres,
			   file_opts->assoc_rec.grp_tres);
	}

	if ((file_opts->assoc_rec.grp_jobs != NO_VAL)
	    && (assoc->grp_jobs != file_opts->assoc_rec.grp_jobs)) {
		mod_assoc.grp_jobs = file_opts->assoc_rec.grp_jobs;
		changed = 1;
		xstrfmtcat(my_info,
			   "%-30.30s for %-7.7s %-10.10s %8d -> %d\n",
			   " Changed GrpJobs",
			   type, name,
			   assoc->grp_jobs,
			   file_opts->assoc_rec.grp_jobs);
	}

	if ((file_opts->assoc_rec.grp_jobs_accrue != NO_VAL)
	    && (assoc->grp_jobs_accrue !=
		file_opts->assoc_rec.grp_jobs_accrue)) {
		mod_assoc.grp_jobs_accrue =
			file_opts->assoc_rec.grp_jobs_accrue;
		changed = 1;
		xstrfmtcat(my_info,
			   "%-30.30s for %-7.7s %-10.10s %8d -> %d\n",
			   " Changed GrpJobsAccrue",
			   type, name,
			   assoc->grp_jobs_accrue,
			   file_opts->assoc_rec.grp_jobs_accrue);
	}

	if ((file_opts->assoc_rec.grp_submit_jobs != NO_VAL)
	    && (assoc->grp_submit_jobs !=
		file_opts->assoc_rec.grp_submit_jobs)) {
		mod_assoc.grp_submit_jobs =
			file_opts->assoc_rec.grp_submit_jobs;
		changed = 1;
		xstrfmtcat(my_info,
			   "%-30.30s for %-7.7s %-10.10s %8d -> %d\n",
			   " Changed GrpSubmitJobs",
			   type, name,
			   assoc->grp_submit_jobs,
			   file_opts->assoc_rec.grp_submit_jobs);
	}

	if ((file_opts->assoc_rec.grp_wall != NO_VAL)
	    && (assoc->grp_wall != file_opts->assoc_rec.grp_wall)) {
		mod_assoc.grp_wall = file_opts->assoc_rec.grp_wall;
		changed = 1;
		xstrfmtcat(my_info,
			   "%-30.30s for %-7.7s %-10.10s %8d -> %d\n",
			   " Changed GrpWallDuration",
			   type, name,
			   assoc->grp_wall,
			   file_opts->assoc_rec.grp_wall);
	}

	if (file_opts->assoc_rec.max_tres_mins_pj
	    && xstrcmp(assoc->max_tres_mins_pj,
		       file_opts->assoc_rec.max_tres_mins_pj)) {
		mod_assoc.max_tres_mins_pj =
			file_opts->assoc_rec.max_tres_mins_pj;
		changed = 1;
		xstrfmtcat(my_info,
			   "%-30.30s for %-7.7s %-10.10s "
			   "%8s -> %s\n",
			   " Changed MaxTRESMinsPerJob",
			   type, name,
			   assoc->max_tres_mins_pj,
			   file_opts->assoc_rec.max_tres_mins_pj);
	}

	if (file_opts->assoc_rec.max_tres_run_mins
	    && xstrcmp(assoc->max_tres_run_mins,
		       file_opts->assoc_rec.max_tres_run_mins)) {
		mod_assoc.max_tres_run_mins =
			file_opts->assoc_rec.max_tres_run_mins;
		changed = 1;
		xstrfmtcat(my_info,
			   "%-30.30s for %-7.7s %-10.10s "
			   "%8s -> %s\n",
			   " Changed MaxTRESRunMins",
			   type, name,
			   assoc->max_tres_run_mins,
			   file_opts->assoc_rec.max_tres_run_mins);
	}

	if (file_opts->assoc_rec.max_tres_pj
	    && xstrcmp(assoc->max_tres_pj, file_opts->assoc_rec.max_tres_pj)) {
		mod_assoc.max_tres_pj = file_opts->assoc_rec.max_tres_pj;
		changed = 1;
		xstrfmtcat(my_info,
			   "%-30.30s for %-7.7s %-10.10s %8s -> %s\n",
			   " Changed MaxTRESPerJob",
			   type, name,
			   assoc->max_tres_pj,
			   file_opts->assoc_rec.max_tres_pj);
	}

	if (file_opts->assoc_rec.max_tres_pn
	    && xstrcmp(assoc->max_tres_pn, file_opts->assoc_rec.max_tres_pn)) {
		mod_assoc.max_tres_pn = file_opts->assoc_rec.max_tres_pn;
		changed = 1;
		xstrfmtcat(my_info,
			   "%-30.30s for %-7.7s %-10.10s %8s -> %s\n",
			   " Changed MaxTRESPerNode",
			   type, name,
			   assoc->max_tres_pn,
			   file_opts->assoc_rec.max_tres_pn);
	}

	if ((file_opts->assoc_rec.max_jobs != NO_VAL)
	    && (assoc->max_jobs != file_opts->assoc_rec.max_jobs)) {
		mod_assoc.max_jobs = file_opts->assoc_rec.max_jobs;
		changed = 1;
		xstrfmtcat(my_info,
			   "%-30.30s for %-7.7s %-10.10s %8d -> %d\n",
			   " Changed MaxJobs",
			   type, name,
			   assoc->max_jobs,
			   file_opts->assoc_rec.max_jobs);
	}

	if ((file_opts->assoc_rec.max_jobs_accrue != NO_VAL)
	    && (assoc->max_jobs_accrue !=
		file_opts->assoc_rec.max_jobs_accrue)) {
		mod_assoc.max_jobs_accrue =
			file_opts->assoc_rec.max_jobs_accrue;
		changed = 1;
		xstrfmtcat(my_info,
			   "%-30.30s for %-7.7s %-10.10s %8d -> %d\n",
			   " Changed MaxJobsAccrue",
			   type, name,
			   assoc->max_jobs_accrue,
			   file_opts->assoc_rec.max_jobs_accrue);
	}

	if ((file_opts->assoc_rec.max_submit_jobs != NO_VAL)
	    && (assoc->max_submit_jobs !=
		file_opts->assoc_rec.max_submit_jobs)) {
		mod_assoc.max_submit_jobs =
			file_opts->assoc_rec.max_submit_jobs;
		changed = 1;
		xstrfmtcat(my_info,
			   "%-30.30s for %-7.7s %-10.10s %8d -> %d\n",
			   " Changed MaxSubmitJobs",
			   type, name,
			   assoc->max_submit_jobs,
			   file_opts->assoc_rec.max_submit_jobs);
	}

	if ((file_opts->assoc_rec.max_wall_pj != NO_VAL)
	    && (assoc->max_wall_pj != file_opts->assoc_rec.max_wall_pj)) {
		mod_assoc.max_wall_pj =	file_opts->assoc_rec.max_wall_pj;
		changed = 1;
		xstrfmtcat(my_info,
			   "%-30.30s for %-7.7s %-10.10s %8d -> %d\n",
			   " Changed MaxWallDurationPerJob",
			   type, name,
			   assoc->max_wall_pj,
			   file_opts->assoc_rec.max_wall_pj);
	}
	if (assoc->parent_acct && parent
	    && xstrcmp(assoc->parent_acct, parent)) {
		mod_assoc.parent_acct = parent;
		changed = 1;
		xstrfmtcat(my_info,
			   "%-30.30s for %-7.7s %-10.10s %8s -> %s\n",
			   " Changed Parent",
			   type, name,
			   assoc->parent_acct,
			   parent);
	}

	if ((file_opts->assoc_rec.priority != NO_VAL)
	    && (assoc->priority != file_opts->assoc_rec.priority)) {
		mod_assoc.priority = file_opts->assoc_rec.priority;
		changed = 1;
		xstrfmtcat(my_info,
			   "%-30.30s for %-7.7s %-10.10s %8d -> %d\n",
			   " Changed Priority",
			   type, name,
			   assoc->priority,
			   file_opts->assoc_rec.priority);
	}

	if (assoc->qos_list && list_count(assoc->qos_list) &&
	    file_opts->assoc_rec.qos_list &&
	    list_count(file_opts->assoc_rec.qos_list)) {
		ListIterator now_qos_itr =
			list_iterator_create(assoc->qos_list),
			new_qos_itr =
			list_iterator_create(file_opts->assoc_rec.qos_list);
		char *now_qos = NULL, *new_qos = NULL;

		if (!mod_assoc.qos_list)
			mod_assoc.qos_list = list_create(slurm_destroy_char);
		while ((new_qos = list_next(new_qos_itr))) {
			while ((now_qos = list_next(now_qos_itr))) {
				if (!xstrcmp(new_qos, now_qos))
					break;
			}
			list_iterator_reset(now_qos_itr);
			if (!now_qos)
				list_append(mod_assoc.qos_list,
					    xstrdup(new_qos));
		}
		list_iterator_destroy(new_qos_itr);
		list_iterator_destroy(now_qos_itr);
		if (mod_assoc.qos_list && list_count(mod_assoc.qos_list))
			new_qos = get_qos_complete_str(g_qos_list,
						       mod_assoc.qos_list);
		if (new_qos) {
			xstrfmtcat(my_info,
				   "%-30.30s for %-7.7s %-10.10s %8s\n",
				   " Added QOS",
				   type, name,
				   new_qos);
			xfree(new_qos);
			changed = 1;
		} else {
			FREE_NULL_LIST(mod_assoc.qos_list);
		}
	} else if (file_opts->assoc_rec.qos_list &&
		   list_count(file_opts->assoc_rec.qos_list)) {
		char *new_qos = get_qos_complete_str(
			g_qos_list, file_opts->assoc_rec.qos_list);

		if (new_qos) {
			xstrfmtcat(my_info,
				   "%-30.30s for %-7.7s %-10.10s %8s\n",
				   " Added QOS",
				   type, name,
				   new_qos);
			xfree(new_qos);
			mod_assoc.qos_list = file_opts->assoc_rec.qos_list;
			file_opts->assoc_rec.qos_list = NULL;
			changed = 1;
		}
	}

	if (changed) {
		List ret_list = NULL;

		assoc_cond.cluster_list = list_create(NULL);
		list_push(assoc_cond.cluster_list, assoc->cluster);

		assoc_cond.acct_list = list_create(NULL);
		list_push(assoc_cond.acct_list, assoc->acct);

		if (mod_type == MOD_USER) {
			assoc_cond.user_list = list_create(NULL);
			list_push(assoc_cond.user_list, assoc->user);
			if (assoc->partition) {
				assoc_cond.partition_list = list_create(NULL);
				list_push(assoc_cond.partition_list,
					  assoc->partition);
			}
		}

		notice_thread_init();
		ret_list = slurmdb_associations_modify(
			db_conn,
			&assoc_cond,
			&mod_assoc);
		notice_thread_fini();

		FREE_NULL_LIST(mod_assoc.qos_list);

		FREE_NULL_LIST(assoc_cond.cluster_list);
		FREE_NULL_LIST(assoc_cond.acct_list);
		FREE_NULL_LIST(assoc_cond.user_list);
		FREE_NULL_LIST(assoc_cond.partition_list);

/* 		if (ret_list && list_count(ret_list)) { */
/* 			char *object = NULL; */
/* 			ListIterator itr = list_iterator_create(ret_list); */
/* 			printf(" Modified account defaults for " */
/* 			       "associations...\n"); */
/* 			while ((object = list_next(itr)))  */
/* 				printf("  %s\n", object); */
/* 			list_iterator_destroy(itr); */
/* 		} */

		if (ret_list) {
			printf("%s", my_info);
			FREE_NULL_LIST(ret_list);
		} else
			changed = 0;
		xfree(my_info);
	}

	return changed;
}

static int _mod_cluster(sacctmgr_file_opts_t *file_opts,
			slurmdb_cluster_rec_t *cluster, char *parent)
{
	int changed = 0;
	char *my_info = NULL;
	slurmdb_cluster_rec_t mod_cluster;
	slurmdb_cluster_cond_t cluster_cond;

	slurmdb_init_cluster_rec(&mod_cluster, 0);
	slurmdb_init_cluster_cond(&cluster_cond, 0);

	if (file_opts->classification
	    && (file_opts->classification != cluster->classification)) {
		xstrfmtcat(my_info,
			   "%-30.30s for %-7.7s %-10.10s %8s -> %s\n",
			   " Changed Classification", "Cluster",
			   cluster->name,
			   get_classification_str(cluster->classification),
			   get_classification_str(file_opts->classification));
		mod_cluster.classification = file_opts->classification;
		changed = 1;
	}

	if (changed) {
		List ret_list = NULL;

		cluster_cond.cluster_list = list_create(NULL);

		list_append(cluster_cond.cluster_list, cluster->name);

		notice_thread_init();
		ret_list = slurmdb_clusters_modify(db_conn,
						   &cluster_cond,
						   &mod_cluster);
		notice_thread_fini();

		FREE_NULL_LIST(cluster_cond.cluster_list);

/* 		if (ret_list && list_count(ret_list)) { */
/* 			char *object = NULL; */
/* 			ListIterator itr = list_iterator_create(ret_list); */
/* 			printf(" Modified account defaults for " */
/* 			       "associations...\n"); */
/* 			while ((object = list_next(itr)))  */
/* 				printf("  %s\n", object); */
/* 			list_iterator_destroy(itr); */
/* 		} */

		if (ret_list) {
			printf("%s", my_info);
			FREE_NULL_LIST(ret_list);
		} else
			changed = 0;
		xfree(my_info);
	}
	if (!cluster->root_assoc || !cluster->root_assoc->cluster) {
		error("Cluster %s doesn't appear to have a root association.  "
		      "Try removing this cluster and then re-run load.",
		      cluster->name);
		exit(1);
	}

	changed += _mod_assoc(file_opts, cluster->root_assoc,
			      MOD_CLUSTER, parent);

	return changed;
}

static int _mod_acct(sacctmgr_file_opts_t *file_opts,
		     slurmdb_account_rec_t *acct, char *parent)
{
	int changed = 0;
	char *desc = NULL, *org = NULL, *my_info = NULL;
	slurmdb_account_rec_t mod_acct;
	slurmdb_account_cond_t acct_cond;
	slurmdb_assoc_cond_t assoc_cond;

	memset(&mod_acct, 0, sizeof(slurmdb_account_rec_t));
	memset(&acct_cond, 0, sizeof(slurmdb_account_cond_t));
	memset(&assoc_cond, 0, sizeof(slurmdb_assoc_cond_t));

	if (file_opts->desc)
		desc = xstrdup(file_opts->desc);

	if (desc && xstrcmp(desc, acct->description)) {
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

	if (file_opts->org)
		org = xstrdup(file_opts->org);

	if (org && xstrcmp(org, acct->organization)) {
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

	if (changed) {
		List ret_list = NULL;

		assoc_cond.acct_list = list_create(NULL);
		list_append(assoc_cond.acct_list, acct->name);
		acct_cond.assoc_cond = &assoc_cond;

		notice_thread_init();
		ret_list = slurmdb_accounts_modify(db_conn,
						   &acct_cond,
						   &mod_acct);
		notice_thread_fini();

		FREE_NULL_LIST(assoc_cond.acct_list);

/* 		if (ret_list && list_count(ret_list)) { */
/* 			char *object = NULL; */
/* 			ListIterator itr = list_iterator_create(ret_list); */
/* 			printf(" Modified account defaults for " */
/* 			       "associations...\n"); */
/* 			while ((object = list_next(itr)))  */
/* 				printf("  %s\n", object); */
/* 			list_iterator_destroy(itr); */
/* 		} */

		if (ret_list) {
			printf("%s", my_info);
			FREE_NULL_LIST(ret_list);
		} else
			changed = 0;
		xfree(my_info);
	}
	xfree(desc);
	xfree(org);
	return changed;
}

static int _mod_user(sacctmgr_file_opts_t *file_opts,
		     slurmdb_user_rec_t *user, char *cluster, char *parent)
{
	int set = 0;
	int changed = 0;
	char *def_acct = NULL, *def_wckey = NULL, *my_info = NULL;
	slurmdb_user_rec_t mod_user;
	slurmdb_user_cond_t user_cond;
	List ret_list = NULL;
	slurmdb_assoc_cond_t assoc_cond;

	if (!user || !user->name) {
		fatal(" We need a user name in _mod_user");
	}

	memset(&mod_user, 0, sizeof(slurmdb_user_rec_t));
	memset(&user_cond, 0, sizeof(slurmdb_user_cond_t));
	memset(&assoc_cond, 0, sizeof(slurmdb_assoc_cond_t));

	assoc_cond.user_list = list_create(NULL);
	list_append(assoc_cond.user_list, user->name);
	user_cond.assoc_cond = &assoc_cond;

	if (file_opts->def_acct)
		def_acct = xstrdup(file_opts->def_acct);

	if (def_acct &&
	    (!user->default_acct || xstrcmp(def_acct, user->default_acct))) {
		xstrfmtcat(my_info,
			   "%-30.30s for %-7.7s %-10.10s %8s -> %s\n",
			   " Changed Default Account", "User",
			   user->name,
			   user->default_acct,
			   def_acct);
		mod_user.default_acct = def_acct;
		changed = 1;
	}

	if (file_opts->def_wckey)
		def_wckey = xstrdup(file_opts->def_wckey);

	if (def_wckey &&
	    (!user->default_wckey || xstrcmp(def_wckey, user->default_wckey))) {
		xstrfmtcat(my_info,
			   "%-30.30s for %-7.7s %-10.10s %8s -> %s\n",
			   " Changed Default WCKey", "User",
			   user->name,
			   user->default_wckey,
			   def_wckey);
		mod_user.default_wckey = def_wckey;
		changed = 1;
	}

	if (user->admin_level != SLURMDB_ADMIN_NOTSET
	    && file_opts->admin != SLURMDB_ADMIN_NOTSET
	    && user->admin_level != file_opts->admin) {
		xstrfmtcat(my_info,
			   "%-30.30s for %-7.7s %-10.10s %8s -> %s\n",
			   " Changed Admin Level", "User",
			   user->name,
			   slurmdb_admin_level_str(
				   user->admin_level),
			   slurmdb_admin_level_str(
				   file_opts->admin));
		mod_user.admin_level = file_opts->admin;
		changed = 1;
	}

	if (changed) {
		notice_thread_init();
		ret_list = slurmdb_users_modify(
			db_conn,
			&user_cond,
			&mod_user);
		notice_thread_fini();

/* 		if (ret_list && list_count(ret_list)) { */
/* 			char *object = NULL; */
/* 			ListIterator itr = list_iterator_create(ret_list); */
/* 			printf(" Modified user defaults for " */
/* 			       "associations...\n"); */
/* 			while ((object = list_next(itr)))  */
/* 				printf("  %s\n", object); */
/* 			list_iterator_destroy(itr); */
/* 		} */

		if (ret_list) {
			printf("%s", my_info);
			FREE_NULL_LIST(ret_list);
			set = 1;
		}
		xfree(my_info);
	}
	xfree(def_acct);
	xfree(def_wckey);

	if ((!user->coord_accts || !list_count(user->coord_accts))
	    && (file_opts->coord_list
		&& list_count(file_opts->coord_list))) {
		ListIterator coord_itr = NULL;
		char *temp_char = NULL;
		slurmdb_coord_rec_t *coord = NULL;
		int first = 1;
		notice_thread_init();
		(void) slurmdb_coord_add(db_conn,
					 file_opts->coord_list,
					 &user_cond);
		notice_thread_fini();

		user->coord_accts = list_create(slurmdb_destroy_coord_rec);
		coord_itr = list_iterator_create(file_opts->coord_list);
		printf(" Making User '%s' coordinator for account(s)",
		       user->name);
		while ((temp_char = list_next(coord_itr))) {
			coord = xmalloc(sizeof(slurmdb_coord_rec_t));
			coord->name = xstrdup(temp_char);
			coord->direct = 1;
			list_push(user->coord_accts, coord);

			if (first) {
				printf(" %s", temp_char);
				first = 0;
			} else
				printf(", %s", temp_char);
		}
		list_iterator_destroy(coord_itr);
		printf("\n");
		set = 1;
	} else if ((user->coord_accts && list_count(user->coord_accts))
		   && (file_opts->coord_list
		       && list_count(file_opts->coord_list))) {
		ListIterator coord_itr = NULL;
		ListIterator char_itr = NULL;
		char *temp_char = NULL;
		slurmdb_coord_rec_t *coord = NULL;
		List add_list = list_create(NULL);

		coord_itr = list_iterator_create(user->coord_accts);
		char_itr = list_iterator_create(file_opts->coord_list);

		while ((temp_char = list_next(char_itr))) {
			while ((coord = list_next(coord_itr))) {
				if (!coord->direct)
					continue;
				if (!xstrcmp(coord->name, temp_char)) {
					break;
				}
			}
			if (!coord) {
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

		if (list_count(add_list)) {
			notice_thread_init();
			(void) slurmdb_coord_add(db_conn,
						 add_list, &user_cond);
			notice_thread_fini();
			set = 1;
		}
		FREE_NULL_LIST(add_list);
	}

	if ((!user->wckey_list || !list_count(user->wckey_list))
	    && (file_opts->wckey_list
		&& list_count(file_opts->wckey_list))) {
		ListIterator wckey_itr = NULL;
		char *temp_char = NULL;
		slurmdb_wckey_rec_t *wckey = NULL;
		int first = 1;

		user->wckey_list = list_create(slurmdb_destroy_wckey_rec);
		wckey_itr = list_iterator_create(file_opts->wckey_list);
		printf(" Adding WCKey(s) ");
		while ((temp_char = list_next(wckey_itr))) {
			wckey = xmalloc(sizeof(slurmdb_wckey_rec_t));
			wckey->name = xstrdup(temp_char);
			wckey->cluster = xstrdup(cluster);
			wckey->user = xstrdup(user->name);
			if (!xstrcmp(wckey->name, user->default_wckey))
				wckey->is_def = 1;
			list_push(user->wckey_list, wckey);

			if (first) {
				printf("'%s'", temp_char);
				first = 0;
			} else
				printf(", '%s'", temp_char);
		}
		list_iterator_destroy(wckey_itr);
		printf(" for user '%s'\n", user->name);
		set = 1;
		notice_thread_init();
		slurmdb_wckeys_add(db_conn, user->wckey_list);
		notice_thread_fini();
	} else if ((user->wckey_list && list_count(user->wckey_list))
		   && (file_opts->wckey_list
		       && list_count(file_opts->wckey_list))) {
		ListIterator wckey_itr = NULL;
		ListIterator char_itr = NULL;
		char *temp_char = NULL;
		slurmdb_wckey_rec_t *wckey = NULL;
		List add_list = list_create(slurmdb_destroy_wckey_rec);

		wckey_itr = list_iterator_create(user->wckey_list);
		char_itr = list_iterator_create(file_opts->wckey_list);

		while ((temp_char = list_next(char_itr))) {
			while ((wckey = list_next(wckey_itr))) {
				if (!xstrcmp(wckey->name, temp_char))
					break;
			}
			if (!wckey) {
				printf(" Adding WCKey '%s' to User '%s'\n",
				       temp_char, user->name);
				wckey = xmalloc(sizeof(slurmdb_wckey_rec_t));
				wckey->name = xstrdup(temp_char);
				wckey->cluster = xstrdup(cluster);
				wckey->user = xstrdup(user->name);
				if (!xstrcmp(wckey->name, user->default_wckey))
					wckey->is_def = 1;

				list_append(add_list, wckey);
			}
			list_iterator_reset(wckey_itr);
		}

		list_iterator_destroy(char_itr);
		list_iterator_destroy(wckey_itr);

		if (list_count(add_list)) {
			notice_thread_init();
			slurmdb_wckeys_add(db_conn, add_list);
			notice_thread_fini();
			set = 1;
		}
		list_transfer(user->wckey_list, add_list);
		FREE_NULL_LIST(add_list);
	}

	FREE_NULL_LIST(assoc_cond.user_list);

	return set;
}

static slurmdb_user_rec_t *_set_user_up(sacctmgr_file_opts_t *file_opts,
					char *cluster, char *parent)
{
	slurmdb_user_rec_t *user = xmalloc(sizeof(slurmdb_user_rec_t));

	user->assoc_list = NULL;
	user->name = xstrdup(file_opts->name);

	if (file_opts->def_acct)
		user->default_acct = xstrdup(file_opts->def_acct);
	else
		user->default_acct = xstrdup(parent);

	if (file_opts->def_wckey)
		user->default_wckey = xstrdup(file_opts->def_wckey);
	else
		user->default_wckey = xstrdup("");

	user->admin_level = file_opts->admin;

	if (file_opts->coord_list) {
		slurmdb_user_cond_t user_cond;
		slurmdb_assoc_cond_t assoc_cond;
		ListIterator coord_itr = NULL;
		char *temp_char = NULL;
		slurmdb_coord_rec_t *coord = NULL;

		memset(&user_cond, 0, sizeof(slurmdb_user_cond_t));
		memset(&assoc_cond, 0, sizeof(slurmdb_assoc_cond_t));
		assoc_cond.user_list = list_create(NULL);
		list_append(assoc_cond.user_list, user->name);
		user_cond.assoc_cond = &assoc_cond;

		notice_thread_init();
		slurmdb_coord_add(db_conn,
				  file_opts->coord_list,
				  &user_cond);
		notice_thread_fini();
		FREE_NULL_LIST(assoc_cond.user_list);
		user->coord_accts = list_create(slurmdb_destroy_coord_rec);
		coord_itr = list_iterator_create(file_opts->coord_list);
		while ((temp_char = list_next(coord_itr))) {
			coord = xmalloc(sizeof(slurmdb_coord_rec_t));
			coord->name = xstrdup(temp_char);
			coord->direct = 1;
			list_push(user->coord_accts, coord);
		}
		list_iterator_destroy(coord_itr);
	}

	if (file_opts->wckey_list) {
		ListIterator wckey_itr = NULL;
		char *temp_char = NULL;
		slurmdb_wckey_rec_t *wckey = NULL;

		user->wckey_list = list_create(slurmdb_destroy_wckey_rec);
		wckey_itr = list_iterator_create(file_opts->wckey_list);
		while ((temp_char = list_next(wckey_itr))) {
			wckey = xmalloc(sizeof(slurmdb_wckey_rec_t));
			wckey->name = xstrdup(temp_char);
			wckey->user = xstrdup(user->name);
			wckey->cluster = xstrdup(cluster);
			if (!xstrcmp(wckey->name, user->default_wckey))
				wckey->is_def = 1;
			list_push(user->wckey_list, wckey);
		}
		list_iterator_destroy(wckey_itr);
		notice_thread_init();
		slurmdb_wckeys_add(db_conn, user->wckey_list);
		notice_thread_fini();
	}
	return user;
}


static slurmdb_account_rec_t *_set_acct_up(sacctmgr_file_opts_t *file_opts,
					   char *parent)
{
	slurmdb_account_rec_t *acct = xmalloc(sizeof(slurmdb_account_rec_t));
	acct->assoc_list = NULL;
	acct->name = xstrdup(file_opts->name);
	if (file_opts->desc)
		acct->description = xstrdup(file_opts->desc);
	else
		acct->description = xstrdup(file_opts->name);
	if (file_opts->org)
		acct->organization = xstrdup(file_opts->org);
	else if (xstrcmp(parent, "root"))
		acct->organization = xstrdup(parent);
	else
		acct->organization = xstrdup(file_opts->name);
	/* info("adding account %s (%s) (%s)", */
/* 		acct->name, acct->description, */
/* 		acct->organization); */

	return acct;
}

static slurmdb_assoc_rec_t *_set_assoc_up(sacctmgr_file_opts_t *file_opts,
						sacctmgr_mod_type_t mod_type,
						char *cluster, char *parent)
{
	slurmdb_assoc_rec_t *assoc = NULL;

	if (!cluster) {
		error("No cluster name was given for _set_assoc_up");
		return NULL;
	}

	if (!parent && (mod_type != MOD_CLUSTER)) {
		error("No parent was given for _set_assoc_up");
		return NULL;
	}

	assoc = xmalloc(sizeof(slurmdb_assoc_rec_t));
	slurmdb_init_assoc_rec(assoc, 0);

	switch(mod_type) {
	case MOD_CLUSTER:
		assoc->acct = xstrdup(parent);
		assoc->cluster = xstrdup(cluster);
		break;
	case MOD_ACCT:
		assoc->acct = xstrdup(file_opts->name);
		assoc->cluster = xstrdup(cluster);
		assoc->parent_acct = xstrdup(parent);
		break;
	case MOD_USER:
		assoc->acct = xstrdup(parent);
		assoc->cluster = xstrdup(cluster);
		assoc->partition = xstrdup(file_opts->assoc_rec.partition);
		assoc->user = xstrdup(file_opts->name);
		if (!xstrcmp(assoc->acct, file_opts->def_acct))
			assoc->is_def = 1;
		break;
	default:
		error("Unknown mod type for _set_assoc_up %d", mod_type);
		slurmdb_destroy_assoc_rec(assoc);
		assoc = NULL;
		break;
	}

	assoc->shares_raw = file_opts->assoc_rec.shares_raw;

	assoc->def_qos_id = file_opts->assoc_rec.def_qos_id;

	slurmdb_copy_assoc_rec_limits(assoc, &file_opts->assoc_rec);

	return assoc;
}

static int _print_file_slurmdb_hierarchical_rec_children(
	FILE *fd, List slurmdb_hierarchical_rec_list,
	List user_list, List acct_list)
{
	ListIterator itr = NULL;
	slurmdb_hierarchical_rec_t *slurmdb_hierarchical_rec = NULL;
	char *line = NULL;
	slurmdb_user_rec_t *user_rec = NULL;
	slurmdb_account_rec_t *acct_rec = NULL;

	itr = list_iterator_create(slurmdb_hierarchical_rec_list);
	while ((slurmdb_hierarchical_rec = list_next(itr))) {
		if (slurmdb_hierarchical_rec->assoc->user) {
			user_rec = sacctmgr_find_user_from_list(
				user_list,
				slurmdb_hierarchical_rec->assoc->user);
			line = xstrdup_printf(
				"User - '%s'",
				slurmdb_hierarchical_rec->sort_name);
			if (slurmdb_hierarchical_rec->assoc->partition)
				xstrfmtcat(line, ":Partition='%s'",
					   slurmdb_hierarchical_rec->
					   assoc->partition);
			if (user_rec) {
				xstrfmtcat(line, ":DefaultAccount='%s'",
					   user_rec->default_acct);
				if (user_rec->default_wckey
				    && user_rec->default_wckey[0])
					xstrfmtcat(line, ":DefaultWCKey='%s'",
						   user_rec->default_wckey);

				if (user_rec->admin_level > SLURMDB_ADMIN_NONE)
					xstrfmtcat(line, ":AdminLevel='%s'",
						   slurmdb_admin_level_str(
							   user_rec->
							   admin_level));
				if (user_rec->coord_accts
				    && list_count(user_rec->coord_accts)) {
					ListIterator itr2 = NULL;
					slurmdb_coord_rec_t *coord = NULL;
					int first_coord = 1;
					list_sort(user_rec->coord_accts,
						  (ListCmpF)sort_coord_list);
					itr2 = list_iterator_create(
						user_rec->coord_accts);
					while ((coord = list_next(itr2))) {
						/* We only care about
						 * the direct accounts here
						 */
						if (!coord->direct)
							continue;
						if (first_coord) {
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
					if (!first_coord)
						xstrcat(line, "'");
					list_iterator_destroy(itr2);
				}

				if (user_rec->wckey_list
				    && list_count(user_rec->wckey_list)) {
					ListIterator itr2 = NULL;
					slurmdb_wckey_rec_t *wckey = NULL;
					int first_wckey = 1;
					itr2 = list_iterator_create(
						user_rec->wckey_list);
					while ((wckey = list_next(itr2))) {
						/* Avoid sending
						   non-legitimate wckeys.
						*/
						if (!wckey->name ||
						    !wckey->name[0] ||
						    wckey->name[0] == '*')
							continue;
						if (first_wckey) {
							xstrfmtcat(
								line,
								":WCKeys='%s",
								wckey->name);
							first_wckey = 0;
						} else {
							xstrfmtcat(line, ",%s",
								   wckey->name);
						}
					}
					if (!first_wckey)
						xstrcat(line, "'");
					list_iterator_destroy(itr2);
				}
			}
		} else {
			acct_rec = sacctmgr_find_account_from_list(
				acct_list,
				slurmdb_hierarchical_rec->assoc->acct);
			line = xstrdup_printf(
				"Account - '%s'",
				slurmdb_hierarchical_rec->sort_name);
			if (acct_rec) {
				xstrfmtcat(line, ":Description='%s'",
					   acct_rec->description);
				xstrfmtcat(line, ":Organization='%s'",
					   acct_rec->organization);
			}
		}

		print_file_add_limits_to_line(&line,
					      slurmdb_hierarchical_rec->assoc);

		if (fprintf(fd, "%s\n", line) < 0) {
			exit_code=1;
			fprintf(stderr, " Can't write to file");
			xfree(line);
			return SLURM_ERROR;
		}
		info("%s", line);
		xfree(line);
	}
	list_iterator_destroy(itr);
	print_file_slurmdb_hierarchical_rec_list(fd,
						 slurmdb_hierarchical_rec_list,
						 user_list, acct_list);

	return SLURM_SUCCESS;
}

extern int print_file_add_limits_to_line(char **line,
					 slurmdb_assoc_rec_t *assoc)
{
	char *tmp_char;
	if (!assoc)
		return SLURM_ERROR;

	if (assoc->def_qos_id && (assoc->def_qos_id != NO_VAL)) {
		if (!g_qos_list)
			g_qos_list = slurmdb_qos_get(
				db_conn, NULL);
		if ((tmp_char = slurmdb_qos_str(g_qos_list, assoc->def_qos_id)))
			xstrfmtcat(*line, ":DefaultQOS='%s'", tmp_char);
	}
	if (assoc->shares_raw != INFINITE)
		xstrfmtcat(*line, ":Fairshare=%u", assoc->shares_raw);

	if (assoc->grp_tres_mins) {
		sacctmgr_initialize_g_tres_list();
		tmp_char = slurmdb_make_tres_string_from_simple(
			assoc->grp_tres_mins, g_tres_list, NO_VAL,
			CONVERT_NUM_UNIT_EXACT, 0, NULL);
		xstrfmtcat(*line, ":GrpTRESMins=%s", tmp_char);
		xfree(tmp_char);
	}

	if (assoc->grp_tres_run_mins) {
		sacctmgr_initialize_g_tres_list();
		tmp_char = slurmdb_make_tres_string_from_simple(
			assoc->grp_tres_run_mins, g_tres_list, NO_VAL,
			CONVERT_NUM_UNIT_EXACT, 0, NULL);
		xstrfmtcat(*line, ":GrpTRESRunMins=%s", tmp_char);
		xfree(tmp_char);
	}

	if (assoc->grp_tres) {
		sacctmgr_initialize_g_tres_list();
		tmp_char = slurmdb_make_tres_string_from_simple(
			assoc->grp_tres, g_tres_list, NO_VAL,
			CONVERT_NUM_UNIT_EXACT, 0, NULL);
		xstrfmtcat(*line, ":GrpTRES=%s", tmp_char);
		xfree(tmp_char);
	}

	if (assoc->grp_jobs != INFINITE)
		xstrfmtcat(*line, ":GrpJobs=%u", assoc->grp_jobs);

	if (assoc->grp_jobs_accrue != INFINITE)
		xstrfmtcat(*line, ":GrpJobsAccrue=%u", assoc->grp_jobs_accrue);

	if (assoc->grp_submit_jobs != INFINITE)
		xstrfmtcat(*line, ":GrpSubmitJobs=%u", assoc->grp_submit_jobs);

	if (assoc->grp_wall != INFINITE)
		xstrfmtcat(*line, ":GrpWall=%u", assoc->grp_wall);

	if (assoc->max_tres_mins_pj) {
		sacctmgr_initialize_g_tres_list();
		tmp_char = slurmdb_make_tres_string_from_simple(
			assoc->max_tres_mins_pj, g_tres_list, NO_VAL,
			CONVERT_NUM_UNIT_EXACT, 0, NULL);
		xstrfmtcat(*line, ":MaxTRESMinsPerJob=%s", tmp_char);
		xfree(tmp_char);
	}

	if (assoc->max_tres_run_mins) {
		sacctmgr_initialize_g_tres_list();
		tmp_char = slurmdb_make_tres_string_from_simple(
			assoc->max_tres_run_mins, g_tres_list, NO_VAL,
			CONVERT_NUM_UNIT_EXACT, 0, NULL);
		xstrfmtcat(*line, ":MaxTRESRunMins=%s", tmp_char);
		xfree(tmp_char);
	}

	if (assoc->max_tres_pj) {
		sacctmgr_initialize_g_tres_list();
		tmp_char = slurmdb_make_tres_string_from_simple(
			assoc->max_tres_pj, g_tres_list, NO_VAL,
			CONVERT_NUM_UNIT_EXACT, 0, NULL);
		xstrfmtcat(*line, ":MaxTRESPerJob=%s", tmp_char);
		xfree(tmp_char);
	}

	if (assoc->max_tres_pn) {
		sacctmgr_initialize_g_tres_list();
		tmp_char = slurmdb_make_tres_string_from_simple(
			assoc->max_tres_pn, g_tres_list, NO_VAL,
			CONVERT_NUM_UNIT_EXACT, 0, NULL);
		xstrfmtcat(*line, ":MaxTRESPerNode=%s", tmp_char);
		xfree(tmp_char);
	}

	if (assoc->max_jobs != INFINITE)
		xstrfmtcat(*line, ":MaxJobs=%u", assoc->max_jobs);

	if (assoc->max_jobs_accrue != INFINITE)
		xstrfmtcat(*line, ":MaxJobsAccrue=%u", assoc->max_jobs_accrue);

	if (assoc->max_submit_jobs != INFINITE)
		xstrfmtcat(*line, ":MaxSubmitJobs=%u", assoc->max_submit_jobs);

	if (assoc->max_wall_pj != INFINITE)
		xstrfmtcat(*line, ":MaxWallDurationPerJob=%u",
			   assoc->max_wall_pj);

	if (assoc->priority != INFINITE)
		xstrfmtcat(*line, ":Priority=%u", assoc->priority);

	if (assoc->qos_list && list_count(assoc->qos_list)) {
		char *temp_char = NULL;
		if (!g_qos_list)
			g_qos_list = slurmdb_qos_get(
				db_conn, NULL);

		temp_char = get_qos_complete_str(g_qos_list, assoc->qos_list);
		xstrfmtcat(*line, ":QOS='%s'", temp_char);
		xfree(temp_char);
	}

	return SLURM_SUCCESS;
}


extern int print_file_slurmdb_hierarchical_rec_list(
	FILE *fd,
	List slurmdb_hierarchical_rec_list,
	List user_list,
	List acct_list)
{
	ListIterator itr = NULL;
	slurmdb_hierarchical_rec_t *slurmdb_hierarchical_rec = NULL;

	itr = list_iterator_create(slurmdb_hierarchical_rec_list);
	while ((slurmdb_hierarchical_rec = list_next(itr))) {
/* 		info("got here %d with %d from %s %s",  */
/* 		     depth, list_count(slurmdb_hierarchical_rec->children), */
/* 		     slurmdb_hierarchical_rec->assoc->acct,
		     slurmdb_hierarchical_rec->assoc->user); */
		if (!list_count(slurmdb_hierarchical_rec->children))
			continue;
		if (fprintf(fd, "Parent - '%s'\n",
			    slurmdb_hierarchical_rec->assoc->acct) < 0) {
			error("Can't write to file");
			return SLURM_ERROR;
		}
		info("%s - '%s'", "Parent",
		     slurmdb_hierarchical_rec->assoc->acct);
/* 		info("sending %d from %s", */
/* 		     list_count(slurmdb_hierarchical_rec->children), */
/* 		     slurmdb_hierarchical_rec->assoc->acct); */
		_print_file_slurmdb_hierarchical_rec_children(
			fd, slurmdb_hierarchical_rec->children,
			user_list, acct_list);
	}
	list_iterator_destroy(itr);

	return SLURM_SUCCESS;
}

extern void load_sacctmgr_cfg_file (int argc, char **argv)
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
	slurmdb_assoc_rec_t *assoc = NULL, *assoc2 = NULL;
	slurmdb_account_rec_t *acct = NULL, *acct2 = NULL;
	slurmdb_cluster_rec_t *cluster = NULL;
	slurmdb_user_rec_t *user = NULL, *user2 = NULL;
	slurmdb_user_cond_t user_cond;

	List curr_assoc_list = NULL;
	List curr_acct_list = NULL;
	List curr_cluster_list = NULL;
	List curr_user_list = NULL;

	/* This will be freed in their local counter parts */
	List mod_acct_list = NULL;
	List acct_list = NULL;
	List slurmdb_assoc_list = NULL;
	List mod_user_list = NULL;
	List user_list = NULL;
	List user_assoc_list = NULL;
	List mod_assoc_list = NULL;

	ListIterator itr;
	ListIterator itr2;

	List print_fields_list;
	List format_list = NULL;
	print_field_t *field = NULL;

	int set = 0, command_len = 0;

	if (readonly_flag) {
		exit_code = 1;
		fprintf(stderr, "Can't run this command in readonly mode.\n");
		return;
	}

	/* reset the connection to get the most recent stuff */
	slurmdb_connection_commit(db_conn, 0);

	for (i = 0; i < argc; i++) {
		int end = parse_option_end(argv[i]);
		if (!end)
			command_len=strlen(argv[i]);
		else {
			command_len=end-1;
			if (argv[i][end] == '=') {
				end++;
			}
		}
		if (!end && !xstrncasecmp(argv[i], "clean",
					  MAX(command_len, 3))) {
			start_clean = 1;
		} else if (!end || !xstrncasecmp(argv[i], "File",
						 MAX(command_len, 1))) {
			if (file_name) {
				exit_code=1;
				fprintf(stderr,
					" File name already set to %s\n",
					file_name);
				continue;
			}
			file_name = xstrdup(argv[i]+end);
		} else if (!xstrncasecmp(argv[i], "Cluster",
					 MAX(command_len, 3))) {
			if (cluster_name) {
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

	if (!file_name) {
		exit_code = 1;
		xfree(cluster_name);
		fprintf(stderr,
			" No filename given, specify one with file=''\n");
		return;

	}

	fd = fopen(file_name, "r");
	xfree(file_name);
	if (fd == NULL) {
		exit_code = 1;
		fprintf(stderr, " Unable to read \"%s\": %s\n", argv[0],
			slurm_strerror(errno));
		xfree(cluster_name);
		return;
	}

	curr_acct_list = slurmdb_accounts_get(db_conn, NULL);

	/* These are new info so they need to be freed here */
	acct_list = list_create(slurmdb_destroy_account_rec);
	slurmdb_assoc_list = list_create(slurmdb_destroy_assoc_rec);
	user_list = list_create(slurmdb_destroy_user_rec);
	user_assoc_list = list_create(slurmdb_destroy_assoc_rec);

	mod_acct_list = list_create(slurmdb_destroy_account_rec);
	mod_user_list = list_create(slurmdb_destroy_user_rec);
	mod_assoc_list = list_create(slurmdb_destroy_assoc_rec);

	format_list = list_create(slurm_destroy_char);

	while ((num_lines = _get_next_line(line, BUFFER_SIZE, fd)) > 0) {
		lc += num_lines;
		/* skip empty lines */
		if (line[0] == '\0') {
			continue;
		}
		len = strlen(line);

		memset(object, 0, sizeof(object));

		/* first find the object */
		start = 0;
		for (i = 0; i < len; i++) {
			if (line[i] == '-') {
				start = i;
				if (line[i-1] == ' ')
					i--;
				if (i < sizeof(object)) {
					i++;	/* Append '\0' */
					strlcpy(object, line, i);
				}
				break;
			}
		}
		if (!object[0])
			continue;

		while ((line[start] != ' ') && (start < len))
			start++;
		if (start >= len) {
			exit_code=1;
			fprintf(stderr, " Nothing after object "
				"name '%s'. line(%d)\n",
				object, lc);
			rc = SLURM_ERROR;
			break;
		}
		start++;

		if (!xstrcasecmp("Machine", object)
		    || !xstrcasecmp("Cluster", object)) {
			slurmdb_assoc_cond_t assoc_cond;

			if (cluster_name && !cluster_name_set) {
				exit_code = 1;
				fprintf(stderr, " You can only add one cluster "
					"at a time.\n");
				rc = SLURM_ERROR;
				break;
			}

			file_opts = _parse_options(line+start);

			if (!file_opts) {
				exit_code = 1;
				fprintf(stderr,
					" error: Problem with line(%d)\n", lc);
				rc = SLURM_ERROR;
				break;
			}

			if (!cluster_name_set)
				cluster_name = xstrdup(file_opts->name);

			/* we have to do this here since this is the
			   first place we have the cluster_name
			*/
			memset(&user_cond, 0, sizeof(slurmdb_user_cond_t));
			user_cond.with_coords = 1;
			user_cond.with_assocs = 1;
			user_cond.with_wckeys = 1;

			memset(&assoc_cond, 0,
			       sizeof(slurmdb_assoc_cond_t));
			assoc_cond.cluster_list = list_create(NULL);
			assoc_cond.with_raw_qos = 1;
			assoc_cond.without_parent_limits = 1;
			list_append(assoc_cond.cluster_list, cluster_name);
			user_cond.assoc_cond = &assoc_cond;
			curr_user_list = slurmdb_users_get(db_conn, &user_cond);

			user_cond.assoc_cond = NULL;
			assoc_cond.only_defs = 0;

			/* make sure this person running is an admin */
			user_name = uid_to_string_cached(my_uid);
			if (!(user = sacctmgr_find_user_from_list(
				      curr_user_list, user_name))) {
				exit_code =1;
				fprintf(stderr, " Your uid (%u) is not in the "
					"accounting system, can't load file.\n",
					my_uid);
				FREE_NULL_LIST(curr_user_list);
				fclose(fd);
				_destroy_sacctmgr_file_opts(file_opts);
				xfree(cluster_name);
				xfree(parent);
				return;

			} else {
				if (my_uid != slurm_get_slurm_user_id()
				    && my_uid != 0
				    && (user->admin_level
					< SLURMDB_ADMIN_SUPER_USER)) {
					exit_code = 1;
					fprintf(stderr,
						" Your user does not have "
						"sufficient "
						"privileges to load files.\n");
					FREE_NULL_LIST(curr_user_list);
					fclose(fd);
					_destroy_sacctmgr_file_opts(file_opts);
					xfree(cluster_name);
					xfree(parent);
					return;
				}
			}
			xfree(user_name);

			if (start_clean) {
				slurmdb_cluster_cond_t cluster_cond;
				List ret_list = NULL;

				if (!commit_check("You requested to flush "
						  "the cluster before "
						  "adding it again.\n"
						  "Are you sure you want "
						  "to continue?")) {
					printf("Aborted\n");
					break;
				}

				slurmdb_init_cluster_cond(&cluster_cond, 0);
				cluster_cond.cluster_list = list_create(NULL);
				list_append(cluster_cond.cluster_list,
					    cluster_name);

				notice_thread_init();
				ret_list = slurmdb_clusters_remove(
					db_conn, &cluster_cond);
				notice_thread_fini();
				FREE_NULL_LIST(cluster_cond.cluster_list);

				if (!ret_list) {
					exit_code=1;
					fprintf(stderr, " There was a problem "
						"removing the cluster.\n");
					rc = SLURM_ERROR;
					break;
				}
				/* This needs to be commited or
				   problems may arise */
				slurmdb_connection_commit(db_conn, 1);
			}
			curr_cluster_list = slurmdb_clusters_get(
				db_conn, NULL);

			if (cluster_name)
				printf("For cluster %s\n", cluster_name);

			if (!(cluster = sacctmgr_find_cluster_from_list(
				      curr_cluster_list, cluster_name))) {
				List temp_assoc_list = list_create(NULL);
				List cluster_list = list_create(
					slurmdb_destroy_cluster_rec);

				cluster = xmalloc(
					sizeof(slurmdb_cluster_rec_t));
				slurmdb_init_cluster_rec(cluster, 0);
				list_append(cluster_list, cluster);
				cluster->name = xstrdup(cluster_name);
				if (file_opts->classification) {
					cluster->classification =
						file_opts->classification;
					printf("Classification: %s\n",
					       get_classification_str(
						       cluster->
						       classification));
				}

				cluster->root_assoc = _set_assoc_up(
					file_opts, MOD_CLUSTER,
					cluster_name, "root");
				list_append(temp_assoc_list,
					    cluster->root_assoc);

				(void) _print_out_assoc(temp_assoc_list, 0, 0);
				FREE_NULL_LIST(temp_assoc_list);
				notice_thread_init();

				rc = slurmdb_clusters_add(
					db_conn, cluster_list);
				notice_thread_fini();
				FREE_NULL_LIST(cluster_list);

				if (rc != SLURM_SUCCESS) {
					exit_code = 1;
					fprintf(stderr,
						" Problem adding cluster: %s\n",
						slurm_strerror(rc));
					rc = SLURM_ERROR;
					_destroy_sacctmgr_file_opts(file_opts);
					file_opts = NULL;
					break;
				}
				/* This needs to be commited or
				   problems may arise */
				slurmdb_connection_commit(db_conn, 1);
				set = 1;
			} else {
				set = _mod_cluster(file_opts,
						   cluster, parent);
			}

			_destroy_sacctmgr_file_opts(file_opts);
			file_opts = NULL;

			/* assoc_cond if set up above */
			curr_assoc_list = slurmdb_associations_get(
				db_conn, &assoc_cond);
			FREE_NULL_LIST(assoc_cond.cluster_list);

			if (!curr_assoc_list) {
				exit_code = 1;
				fprintf(stderr, " Problem getting assocs "
					"for this cluster\n");
				rc = SLURM_ERROR;
				break;
			}
			//info("got %d assocs", list_count(curr_assoc_list));
			continue;
		} else if (!cluster_name) {
			exit_code = 1;
			fprintf(stderr, " You need to specify a cluster name "
				"first with 'Cluster - $NAME' in your file\n");
			break;
		}

		if (!xstrcasecmp("Parent", object)) {
			file_opts = _parse_options(line+start);
			xfree(parent);

			if (!file_opts) {
				exit_code = 1;
				fprintf(stderr, " Problem with line(%d)\n", lc);
				rc = SLURM_ERROR;
				break;
			}

			parent = xstrdup(file_opts->name);
			//info("got parent %s", parent);
			if (!sacctmgr_find_account_base_assoc_from_list(
				    curr_assoc_list, parent, cluster_name)
			    && !sacctmgr_find_account_base_assoc_from_list(
				    slurmdb_assoc_list, parent, cluster_name)) {
				exit_code=1;
				fprintf(stderr, " line(%d) You need to add "
					"this parent (%s) as a child before "
					"you can add children to it.\n",
					lc, parent);
				break;
			}
			_destroy_sacctmgr_file_opts(file_opts);
			file_opts = NULL;
			continue;
		} else if (!parent) {
			parent = xstrdup("root");
			printf(" No parent given creating off root, "
			       "If incorrect specify 'Parent - name' "
			       "before any children in your file\n");
		}

		if (!xstrcasecmp("Project", object)
		    || !xstrcasecmp("Account", object)) {
			file_opts = _parse_options(line+start);

			if (!file_opts) {
				exit_code=1;
				fprintf(stderr, " Problem with line(%d)\n", lc);
				rc = SLURM_ERROR;
				break;
			}

			//info("got a project %s of %s", file_opts->name, parent);

			acct = sacctmgr_find_account_from_list(
				curr_acct_list, file_opts->name);
			if (!acct)
				acct = sacctmgr_find_account_from_list(
					acct_list, file_opts->name);

			if (!acct) {
				acct = _set_acct_up(file_opts, parent);
				list_append(acct_list, acct);
				/* don't add anything to the
				   curr_acct_list */

				assoc = _set_assoc_up(file_opts, MOD_ACCT,
						      cluster_name, parent);

				list_append(slurmdb_assoc_list, assoc);
				/* don't add anything to the
				   curr_assoc_list */
			} else if (!(assoc =
				     sacctmgr_find_account_base_assoc_from_list(
					     curr_assoc_list, file_opts->name,
					     cluster_name)) &&
				   !sacctmgr_find_account_base_assoc_from_list(
					   slurmdb_assoc_list, file_opts->name,
					   cluster_name)) {
				acct2 = sacctmgr_find_account_from_list(
					mod_acct_list, file_opts->name);

				if (!acct2) {
					acct2 = xmalloc(
						sizeof(slurmdb_account_rec_t));
					list_append(mod_acct_list, acct2);
					acct2->name = xstrdup(file_opts->name);
					if (_mod_acct(file_opts, acct, parent))
						set = 1;
				} else {
					debug2("already modified this account");
				}

				assoc = _set_assoc_up(file_opts, MOD_ACCT,
						      cluster_name, parent);

				list_append(slurmdb_assoc_list, assoc);
				/* don't add anything to the
				   curr_assoc_list */
			} else if (assoc) {
				acct2 = sacctmgr_find_account_from_list(
					mod_acct_list, file_opts->name);

				if (!acct2) {
					acct2 = xmalloc(
						sizeof(slurmdb_account_rec_t));
					list_append(mod_acct_list, acct2);
					acct2->name = xstrdup(file_opts->name);
					if (_mod_acct(file_opts, acct, parent))
						set = 1;
				} else {
					debug2("already modified this account");
				}

				assoc2 = sacctmgr_find_assoc_from_list(
					mod_assoc_list,
					NULL, file_opts->name,
					cluster_name,
					NULL);

				if (!assoc2) {
					assoc2 = xmalloc(
						sizeof(slurmdb_assoc_rec_t));
					slurmdb_init_assoc_rec(assoc2, 0);
					list_append(mod_assoc_list, assoc2);
					assoc2->cluster = xstrdup(cluster_name);
					assoc2->acct = xstrdup(file_opts->name);
					assoc2->parent_acct =
						xstrdup(assoc->parent_acct);
					if (_mod_assoc(file_opts,
						       assoc, MOD_ACCT, parent))
						set = 1;
				} else {
					debug2("already modified this assoc");
				}
			}
			_destroy_sacctmgr_file_opts(file_opts);
			file_opts = NULL;
			continue;
		} else if (!xstrcasecmp("User", object)) {
			file_opts = _parse_options(line+start);

			if (!file_opts) {
				exit_code=1;
				fprintf(stderr, " Problem with line(%d)\n", lc);
				rc = SLURM_ERROR;
				break;
			}

			if (!(user = sacctmgr_find_user_from_list(
				      curr_user_list, file_opts->name))
			    && !sacctmgr_find_user_from_list(
				    user_list, file_opts->name)) {

				user = _set_user_up(file_opts, cluster_name,
						    parent);
				list_append(user_list, user);
				/* don't add anything to the
				   curr_user_list */

				assoc = _set_assoc_up(file_opts, MOD_USER,
						      cluster_name, parent);

				list_append(user_assoc_list, assoc);
				/* don't add anything to the
				   curr_assoc_list */
			} else if (!(assoc =
				     sacctmgr_find_assoc_from_list(
					     curr_assoc_list,
					     file_opts->name, parent,
					     cluster_name,
					     file_opts->assoc_rec.partition))
				   && !sacctmgr_find_assoc_from_list(
					   user_assoc_list,
					   file_opts->name, parent,
					   cluster_name,
					   file_opts->assoc_rec.partition)) {

				/* This means the user was added
				 * during this round but this is a new
				 * association we are adding
				 */
				if (!user)
					goto new_assoc;

				/* This means there could be a change
				 * on the user.
				 */
				user2 = sacctmgr_find_user_from_list(
					mod_user_list, file_opts->name);
				if (!user2) {
					user2 = xmalloc(
						sizeof(slurmdb_user_rec_t));
					list_append(mod_user_list, user2);
					user2->name = xstrdup(file_opts->name);
					if (_mod_user(file_opts, user,
						      cluster_name, parent))
						set = 1;
				} else {
					debug2("already modified this user");
				}
			new_assoc:
				assoc = _set_assoc_up(file_opts, MOD_USER,
						      cluster_name, parent);

				list_append(user_assoc_list, assoc);
				/* don't add anything to the
				   curr_assoc_list */
			} else if (assoc) {
				user2 = sacctmgr_find_user_from_list(
					mod_user_list, file_opts->name);
				if (!user2) {
					user2 = xmalloc(
						sizeof(slurmdb_user_rec_t));
					list_append(mod_user_list, user2);
					user2->name = xstrdup(file_opts->name);
					if (_mod_user(file_opts, user,
						      cluster_name, parent))
						set = 1;
				} else {
					debug2("already modified this user");
				}

				assoc2 = sacctmgr_find_assoc_from_list(
					mod_assoc_list,
					file_opts->name, parent,
					cluster_name,
					file_opts->assoc_rec.partition);

				if (!assoc2) {
					assoc2 = xmalloc(
						sizeof(slurmdb_assoc_rec_t));
					slurmdb_init_assoc_rec(assoc2, 0);
					list_append(mod_assoc_list, assoc2);
					assoc2->cluster = xstrdup(cluster_name);
					assoc2->acct = xstrdup(parent);
					assoc2->user = xstrdup(file_opts->name);
					assoc2->partition = xstrdup(
						file_opts->assoc_rec.partition);
					if (_mod_assoc(file_opts,
						       assoc, MOD_USER, parent))
						set = 1;
				} else {
					debug2("already modified this assoc");
				}
			}
			//info("got a user %s", file_opts->name);
			_destroy_sacctmgr_file_opts(file_opts);
			file_opts = NULL;
			continue;
		} else {
			exit_code = 1;
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
	if (rc == SLURM_SUCCESS && list_count(acct_list)) {
		printf("Accounts\n");
		slurm_addto_char_list(format_list,
				      "Name,Description,Organization,QOS");

		print_fields_list = sacctmgr_process_format_list(format_list);
		list_flush(format_list);

		print_fields_header(print_fields_list);

		itr = list_iterator_create(acct_list);
		itr2 = list_iterator_create(print_fields_list);
		while ((acct = list_next(itr))) {
			while ((field = list_next(itr2))) {
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
				default:
					field->print_routine(
						field, NULL);
					break;
				}
			}
			list_iterator_reset(itr2);
			printf("\n");
		}
		list_iterator_destroy(itr);
		list_iterator_destroy(itr2);
		FREE_NULL_LIST(print_fields_list);
		rc = slurmdb_accounts_add(db_conn, acct_list);
		printf("-----------------------------"
		       "----------------------\n\n");
		set = 1;
	}

	if (rc == SLURM_SUCCESS && list_count(slurmdb_assoc_list)) {
		printf("Account Associations\n");
		rc = _print_out_assoc(slurmdb_assoc_list, 0, 1);
		set = 1;
	}
	if (rc == SLURM_SUCCESS && list_count(user_list)) {
		printf("Users\n");

		slurm_addto_char_list(format_list,
				      "Name,DefaultA,DefaultW,QOS,Admin,Coord");

		print_fields_list = sacctmgr_process_format_list(format_list);
		list_flush(format_list);
		print_fields_header(print_fields_list);

		itr = list_iterator_create(user_list);
		itr2 = list_iterator_create(print_fields_list);
		while ((user = list_next(itr))) {
			while ((field = list_next(itr2))) {
				switch(field->type) {
				case PRINT_ADMIN:
					field->print_routine(
						field,
						slurmdb_admin_level_str(
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
				case PRINT_DWCKEY:
					field->print_routine(
						field,
						user->default_wckey);
					break;
				case PRINT_NAME:
					field->print_routine(
						field, user->name);
					break;
				case PRINT_WCKEYS:
					field->print_routine(
						field, user->wckey_list);
					break;
				default:
					field->print_routine(
						field, NULL);
					break;
				}
			}
			list_iterator_reset(itr2);
			printf("\n");
		}
		list_iterator_destroy(itr);
		list_iterator_destroy(itr2);
		FREE_NULL_LIST(print_fields_list);

		rc = slurmdb_users_add(db_conn, user_list);
		printf("---------------------------"
		       "------------------------\n\n");
		set = 1;
	}

	if (rc == SLURM_SUCCESS && list_count(user_assoc_list)) {
		printf("User Associations\n");
		rc = _print_out_assoc(user_assoc_list, 1, 1);
		set = 1;
	}
	END_TIMER2("add cluster");

	if (set)
		info("Done adding cluster in %s", TIME_STR);

	if (rc == SLURM_SUCCESS) {
		if (set) {
			if (commit_check("Would you like to commit changes?")) {
				slurmdb_connection_commit(db_conn, 1);
			} else {
				printf(" Changes Discarded\n");
				slurmdb_connection_commit(db_conn, 0);
			}
		} else {
			printf(" Nothing new added.\n");
		}
	} else {
		exit_code=1;
		fprintf(stderr, " Problem with requests: %s\n",
			slurm_strerror(rc));
	}

	FREE_NULL_LIST(format_list);
	FREE_NULL_LIST(mod_acct_list);
	FREE_NULL_LIST(acct_list);
	FREE_NULL_LIST(slurmdb_assoc_list);
	FREE_NULL_LIST(mod_user_list);
	FREE_NULL_LIST(user_list);
	FREE_NULL_LIST(user_assoc_list);
	FREE_NULL_LIST(mod_assoc_list);
	FREE_NULL_LIST(curr_acct_list);
	FREE_NULL_LIST(curr_assoc_list);
	FREE_NULL_LIST(curr_cluster_list);
	FREE_NULL_LIST(curr_user_list);
	_destroy_sacctmgr_file_opts(file_opts);
}
