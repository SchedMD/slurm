/*****************************************************************************\
 *  process.c -  process the return from get_share_info.
 *****************************************************************************
 *  Copyright (C) 2008-2010 Lawrence Livermore National Security.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Danny Auble <da@llnl.gov>
 *  CODE-OCEC-09-009. All rights reserved.
 *
 *  This file is part of SLURM, a resource management program.
 *  For details, see <http://slurm.schedmd.com/>.
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
\*****************************************************************************/

#include "src/common/slurm_priority.h"
#include "src/sshare/sshare.h"
#include <math.h>

extern int long_flag;

extern int process(shares_response_msg_t *resp, uint16_t options)
{
	uint32_t flags = slurmctld_conf.priority_flags;
	int rc = SLURM_SUCCESS;
	association_shares_object_t *share = NULL;
	ListIterator itr = NULL;
	ListIterator itr2 = NULL;
	char *object = NULL;
	char *print_acct = NULL;
	List tree_list = NULL;

	int field_count = 0;

	print_field_t *field = NULL;

	List format_list;
	List print_fields_list; /* types are of print_field_t */

	enum {
		PRINT_ACCOUNT,
		PRINT_CLUSTER,
		PRINT_CPUMINS,
		PRINT_EUSED,
		PRINT_FSFACTOR,
		PRINT_ID,
		PRINT_NORMS,
		PRINT_NORMU,
		PRINT_RAWS,
		PRINT_RAWU,
		PRINT_RUNMINS,
		PRINT_USER,
		PRINT_LEVELFS
	};

	if (!resp)
		return SLURM_ERROR;

	format_list = list_create(slurm_destroy_char);
	if (flags & PRIORITY_FLAGS_FAIR_TREE) {
		if (long_flag) {
			slurm_addto_char_list(format_list,
					      "A,User,RawShares,NormShares,"
					      "RawUsage,NormUsage,EffUsage,"
					      "FSFctr,LevelFS,GrpCPUMins,"
					      "CPURunMins");
		} else {
			slurm_addto_char_list(format_list,
					      "A,User,RawShares,NormShares,"
					      "RawUsage,EffUsage,FSFctr");
		}
	} else {
		if (long_flag) {
			slurm_addto_char_list(format_list,
					      "A,User,RawShares,NormShares,"
					      "RawUsage,NormUsage,EffUsage,"
					      "FSFctr,GrpCPUMins,CPURunMins");
		} else {
			slurm_addto_char_list(format_list,
					      "A,User,RawShares,NormShares,"
					      "RawUsage,EffUsage,FSFctr");
		}
	}


	print_fields_list = list_create(destroy_print_field);
	itr = list_iterator_create(format_list);
	while ((object = list_next(itr))) {
		char *tmp_char = NULL;
		field = xmalloc(sizeof(print_field_t));
		if (!strncasecmp("Account", object, 1)) {
			field->type = PRINT_ACCOUNT;
			field->name = xstrdup("Account");
			field->len = -20;
			field->print_routine = print_fields_str;
		} else if (!strncasecmp("Cluster", object, 2)) {
			field->type = PRINT_CLUSTER;
			field->name = xstrdup("Cluster");
			field->len = 10;
			field->print_routine = print_fields_str;
		} else if (!strncasecmp("EffUsage", object, 1)) {
			field->type = PRINT_EUSED;
			field->name = xstrdup("Effectv Usage");
			field->len = 13;
			field->print_routine = print_fields_double;
		} else if (!strncasecmp("FSFctr", object, 4)) {
			field->type = PRINT_FSFACTOR;
			field->name = xstrdup("FairShare");
			field->len = 10;
			field->print_routine = print_fields_double;
		} else if (!strncasecmp("LevelFS", object, 1)) {
			field->type = PRINT_LEVELFS;
			field->name = xstrdup("Level FS");
			field->len = 10;
			field->print_routine = print_fields_double;
		} else if (!strncasecmp("ID", object, 1)) {
			field->type = PRINT_ID;
			field->name = xstrdup("ID");
			field->len = 6;
			field->print_routine = print_fields_uint;
		} else if (!strncasecmp("NormShares", object, 5)) {
			field->type = PRINT_NORMS;
			field->name = xstrdup("Norm Shares");
			field->len = 11;
			field->print_routine = print_fields_double;
		} else if (!strncasecmp("NormUsage", object, 5)) {
			field->type = PRINT_NORMU;
			field->name = xstrdup("Norm Usage");
			field->len = 11;
			field->print_routine = print_fields_double;
		} else if (!strncasecmp("RawShares", object, 4)) {
			field->type = PRINT_RAWS;
			field->name = xstrdup("Raw Shares");
			field->len = 10;
			field->print_routine = print_fields_uint32;
		} else if (!strncasecmp("RawUsage", object, 4)) {
			field->type = PRINT_RAWU;
			field->name = xstrdup("Raw Usage");
			field->len = 11;
			field->print_routine = print_fields_uint64;
		} else if (!strncasecmp("User", object, 1)) {
			field->type = PRINT_USER;
			field->name = xstrdup("User");
			field->len = 10;
			field->print_routine = print_fields_str;
		} else if (!strncasecmp("GrpCPUMins", object, 1)) {
			field->type = PRINT_CPUMINS;
			field->name = xstrdup("GrpCPUMins");
			field->len = 11;
			field->print_routine = print_fields_uint64;
		} else if (!strncasecmp("CPURunMins", object, 2)) {
			field->type = PRINT_RUNMINS;
			field->name = xstrdup("CPURunMins");
			field->len = 15;
			field->print_routine = print_fields_uint64;
		} else {
			exit_code=1;
			fprintf(stderr, "Unknown field '%s'\n", object);
			exit(1);
			xfree(field);
			continue;
		}
		if ((tmp_char = strstr(object, "\%"))) {
			int newlen = atoi(tmp_char+1);
			if (newlen)
				field->len = newlen;
		}
		list_append(print_fields_list, field);
	}
	list_iterator_destroy(itr);
	list_destroy(format_list);

	if (exit_code) {
		list_destroy(print_fields_list);
		return SLURM_ERROR;
	}

	itr2 = list_iterator_create(print_fields_list);
	print_fields_header(print_fields_list);

	field_count = list_count(print_fields_list);

	if (!resp->assoc_shares_list || !list_count(resp->assoc_shares_list))
		return SLURM_SUCCESS;

	tree_list = list_create(slurmdb_destroy_print_tree);
	itr = list_iterator_create(resp->assoc_shares_list);
	while ((share = list_next(itr))) {
		int curr_inx = 1;
		char *tmp_char = NULL;
		char *local_acct = NULL;

		if ((options & PRINT_USERS_ONLY) && share->user == 0)
			continue;

		while ((field = list_next(itr2))) {
			switch(field->type) {
			case PRINT_ACCOUNT:
				if (share->user) {
					local_acct = xstrdup_printf(
						"|%s", share->name);
				} else
					local_acct = xstrdup(share->name);

				print_acct = slurmdb_tree_name_get(
					local_acct,
					share->parent, tree_list);
				xfree(local_acct);
				field->print_routine(
					field,
					print_acct,
					(curr_inx == field_count));
				break;
			case PRINT_CLUSTER:
				field->print_routine(
					field,
					share->cluster,
					(curr_inx == field_count));
				break;
			case PRINT_EUSED:
				field->print_routine(field,
						     share->usage_efctv,
						     (curr_inx == field_count));
				break;
			case PRINT_FSFACTOR:
				if (flags & PRIORITY_FLAGS_FAIR_TREE) {
					if(share->user)
						field->print_routine(
						field,
						share->fs_factor,
						(curr_inx == field_count));
					else
						print_fields_str(
							field,
							NULL,
							(curr_inx ==
							 field_count)
						);
				}
				else
					field->print_routine(field,
						     priority_g_calc_fs_factor(
							     (long double)
							     share->usage_efctv,
							     (long double)
							     share->
							     shares_norm),
						     (curr_inx == field_count));
				break;
			case PRINT_LEVELFS:
				if (share->shares_raw == SLURMDB_FS_USE_PARENT)
					print_fields_str(field, NULL,
							 (curr_inx ==
							  field_count));
				else
					field->print_routine(field,
						     (double) share->level_fs,
						     (curr_inx == field_count));
				break;
			case PRINT_ID:
				field->print_routine(field,
						     share->assoc_id,
						     (curr_inx == field_count));
				break;
			case PRINT_NORMS:
				field->print_routine(field,
						     share->shares_norm,
						     (curr_inx == field_count));
				break;
			case PRINT_NORMU:
				field->print_routine(field,
						     share->usage_norm,
						     (curr_inx == field_count));
				break;
			case PRINT_RAWS:
				if (share->shares_raw == SLURMDB_FS_USE_PARENT)
					print_fields_str(field, "parent",
							 (curr_inx ==
							  field_count));
				else
					field->print_routine(field,
							     share->shares_raw,
							     (curr_inx ==
							      field_count));
				break;
			case PRINT_RAWU:
				field->print_routine(field,
						     share->usage_raw,
						     (curr_inx == field_count));
				break;
			case PRINT_USER:
				if (share->user)
					tmp_char = share->name;
				field->print_routine(field,
						     tmp_char,
						     (curr_inx == field_count));
				break;
			case PRINT_CPUMINS:
				field->print_routine(field,
						     share->grp_cpu_mins,
						     (curr_inx == field_count));
				break;
			case PRINT_RUNMINS:
				field->print_routine(field,
						     share->cpu_run_mins,
						     (curr_inx == field_count));
				break;
			default:
				field->print_routine(
					field, NULL,
					(curr_inx == field_count));
				break;
			}
			curr_inx++;
		}
		list_iterator_reset(itr2);
		printf("\n");
	}

	if (tree_list)
		list_destroy(tree_list);

	list_iterator_destroy(itr2);
	list_iterator_destroy(itr);
	list_destroy(print_fields_list);
	return rc;
}
