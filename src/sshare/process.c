/*****************************************************************************\
 *  process.c -  process the return from get_share_info.
 *****************************************************************************
 *  Copyright (C) 2008-2010 Lawrence Livermore National Security.
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

#include "src/common/slurm_priority.h"
#include "src/sshare/sshare.h"
#include <math.h>

static void _print_tres(print_field_t *field, uint64_t *tres_cnts,
			int last);

int long_flag;		/* exceeds 80 character limit with more info */
char **tres_names = NULL;
uint32_t tres_cnt = 0;
char *opt_field_list = NULL;

print_field_t fields[] = {
	{-20, "Account", print_fields_str, PRINT_ACCOUNT},
	{10, "Cluster", print_fields_str, PRINT_CLUSTER},
	{13, "EffectvUsage", print_fields_double, PRINT_EUSED},
	{10, "FairShare", print_fields_double, PRINT_FSFACTOR},
	{30, "GrpTRESMins", _print_tres, PRINT_TRESMINS},
	{30, "GrpTRESRaw", _print_tres, PRINT_GRPTRESRAW},
	{6,  "ID", print_fields_uint, PRINT_ID},
	{10, "LevelFS", print_fields_double, PRINT_LEVELFS},
	{11, "NormShares", print_fields_double, PRINT_NORMS},
	{11, "NormUsage", print_fields_double, PRINT_NORMU},
	{12, "Partition", print_fields_str, PRINT_PART},
	{10, "RawShares", print_fields_uint32, PRINT_RAWS},
	{11, "RawUsage", print_fields_uint64, PRINT_RAWU},
	{30, "TRESRunMins", _print_tres, PRINT_RUNMINS},
	{10, "User", print_fields_str, PRINT_USER},
	{0,  NULL, NULL, 0}
};

static void _print_tres(print_field_t *field, uint64_t *tres_cnts,
			int last)
{
	int abs_len = abs(field->len);
	char *print_this;

	print_this = slurmdb_make_tres_string_from_arrays(
		tres_names, tres_cnts, tres_cnt, TRES_STR_FLAG_REMOVE);

	if (!print_this)
		print_this = xstrdup("");

	if (print_fields_parsable_print == PRINT_FIELDS_PARSABLE_NO_ENDING
	    && last)
		printf("%s", print_this);
	else if (print_fields_parsable_print)
		printf("%s|", print_this);
	else {
		if (strlen(print_this) > abs_len)
			print_this[abs_len-1] = '+';

		if (field->len == abs_len)
			printf("%*.*s ", abs_len, abs_len, print_this);
		else
			printf("%-*.*s ", abs_len, abs_len, print_this);
	}
	xfree(print_this);
}

extern int process(shares_response_msg_t *resp, uint16_t options)
{
	uint32_t flags = slurmctld_conf.priority_flags;
	int rc = SLURM_SUCCESS, i;
	assoc_shares_object_t *share = NULL;
	ListIterator itr = NULL;
	ListIterator itr2 = NULL;
	char *object = NULL;
	char *print_acct = NULL;
	List tree_list = NULL;
	char *tmp_char = NULL;

	int field_count = 0;

	List format_list;
	List print_fields_list; /* types are of print_field_t */

	if (!resp)
		return SLURM_ERROR;

	tres_names = resp->tres_names;
	tres_cnt = resp->tres_cnt;

	format_list = list_create(slurm_destroy_char);
	if (opt_field_list) {
		slurm_addto_char_list(format_list, opt_field_list);
	} else if (flags & PRIORITY_FLAGS_FAIR_TREE) {
		if (long_flag) {
			if (options & PRINT_PARTITIONS)
				slurm_addto_char_list(
					format_list,
					"A,User,P,RawShares,NormShares,"
					"RawUsage,NormUsage,Eff,"
					"Fairshare,LevelFS,GrpTRESMins,"
					"TRESRunMins");
			else
				slurm_addto_char_list(
					format_list,
					"A,User,RawShares,NormShares,"
					"RawUsage,NormUsage,Eff,"
					"Fairshare,LevelFS,GrpTRESMins,"
					"TRESRunMins");

		} else {
			if (options & PRINT_PARTITIONS)
				slurm_addto_char_list(
					format_list,
					"A,User,P,RawShares,NormShares,"
					"RawUsage,Eff,Fairshare");
			else
				slurm_addto_char_list(
					format_list,
					"A,User,RawShares,NormShares,"
					"RawUsage,Eff,Fairshare");
		}
	} else {
		if (long_flag) {
			if (options & PRINT_PARTITIONS)
				slurm_addto_char_list(
					format_list,
					"A,User,P,RawShares,NormShares,"
					"RawUsage,NormUsage,Eff,"
					"Fairshare,GrpTRESMins,TRESRunMins");
			else
				slurm_addto_char_list(
					format_list,
					"A,User,RawShares,NormShares,"
					"RawUsage,NormUsage,Eff,"
					"Fairshare,GrpTRESMins,TRESRunMins");
		} else {
			if (options & PRINT_PARTITIONS)
				slurm_addto_char_list(
					format_list,
					"A,User,P,RawShares,NormShares,"
					"RawUsage,Eff,Fairshare");
			else
				slurm_addto_char_list(
					format_list,
					"A,User,RawShares,NormShares,"
					"RawUsage,Eff,Fairshare");
		}
	}


	print_fields_list = list_create(NULL);
	itr = list_iterator_create(format_list);
	while ((object = list_next(itr))) {
		int newlen = 0;
		if ((tmp_char = strstr(object, "\%"))) {
			tmp_char[0] = '\0';
			newlen = atoi(tmp_char+1);
		}
		for (i = 0; fields[i].name; i++) {
			if (!xstrncasecmp(fields[i].name, object,
					  strlen(object))) {
				if (newlen)
					fields[i].len = newlen;

				list_append(print_fields_list, &fields[i]);
				break;
			}
		}

		if (!fields[i].name) {
			error("Invalid field requested: \"%s\"", object);
			exit(1);
		}
	}
	list_iterator_destroy(itr);
	FREE_NULL_LIST(format_list);

	if (exit_code) {
		FREE_NULL_LIST(print_fields_list);
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
		print_field_t *field = NULL;
		uint64_t tres_raw[tres_cnt];

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
			case PRINT_PART:
				field->print_routine(field,
						     share->partition,
						     (curr_inx == field_count));
				break;
			case PRINT_TRESMINS:
				field->print_routine(field,
						     share->tres_grp_mins,
						     (curr_inx == field_count));
				break;
			case PRINT_GRPTRESRAW:
				/* convert to ints and minutes */
				for (i=0; i<tres_cnt; i++)
					tres_raw[i] = (uint64_t)
						(share->usage_tres_raw[i] / 60);
				field->print_routine(field,
						     tres_raw,
						     (curr_inx == field_count));
				break;
			case PRINT_RUNMINS:
				/* convert to minutes */
				for (i=0; i<tres_cnt; i++)
					share->tres_run_secs[i] /= 60;
				field->print_routine(field,
						     share->tres_run_secs,
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

	FREE_NULL_LIST(tree_list);
	list_iterator_destroy(itr2);
	list_iterator_destroy(itr);
	FREE_NULL_LIST(print_fields_list);
	return rc;
}
