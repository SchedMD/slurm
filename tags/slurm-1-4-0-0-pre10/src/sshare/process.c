/*****************************************************************************\
 *  process.c -  process the return from get_share_info. 
 *****************************************************************************
 *  Copyright (C) 2008 Lawrence Livermore National Security.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Danny Auble <da@llnl.gov>
 *  CODE-OCEC-09-009. All rights reserved.
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
\*****************************************************************************/

#include "src/sshare/sshare.h"

extern int long_flag;

extern int process(shares_response_msg_t *resp)
{
	int rc = SLURM_SUCCESS;
	association_shares_object_t *assoc = NULL;
	ListIterator itr = NULL;
	ListIterator itr2 = NULL;
	char *object = NULL;
	char *print_acct = NULL;
	List tree_list = NULL;

	int field_count = 0;

	print_field_t *field = NULL;

	List format_list = list_create(slurm_destroy_char);
	List print_fields_list; /* types are of print_field_t */

	enum {
		PRINT_ACCOUNT,
		PRINT_CLUSTER,
		PRINT_EUSED,
		PRINT_FSFACTOR,
		PRINT_ID,
		PRINT_NORMS,
		PRINT_NORMU,
		PRINT_RAWS,
		PRINT_RAWU,
		PRINT_USER,
	};

	if(!resp)
		return SLURM_ERROR;

	format_list = list_create(slurm_destroy_char);
	if (long_flag) {
		slurm_addto_char_list(format_list,
				      "A,User,RawShares,NormShares,"
				      "RawUsage,NormUsage,EffUsage,"
				      "FSFctr");
	} else {
		slurm_addto_char_list(format_list,
				      "A,User,RawShares,NormShares,"
				      "RawUsage,EffUsage,FSFctr");
	}

	print_fields_list = list_create(destroy_print_field);

	itr = list_iterator_create(format_list);
	while((object = list_next(itr))) {
		char *tmp_char = NULL;
		field = xmalloc(sizeof(print_field_t));
		if(!strncasecmp("Account", object, 1)) {
			field->type = PRINT_ACCOUNT;
			field->name = xstrdup("Account");
			field->len = 20;
			field->print_routine = print_fields_str;
		} else if(!strncasecmp("Cluster", object, 1)) {
			field->type = PRINT_CLUSTER;
			field->name = xstrdup("Cluster");
			field->len = 10;
			field->print_routine = print_fields_str;
		} else if(!strncasecmp("EffUsage", object, 1)) {
			field->type = PRINT_EUSED;
			field->name = xstrdup("Effectv Usage");
			field->len = 13;
			field->print_routine = print_fields_double;
		} else if(!strncasecmp("FSFctr", object, 1)) {
			field->type = PRINT_FSFACTOR;
			field->name = xstrdup("Fair-share");
			field->len = 10;
			field->print_routine = print_fields_double;
		} else if(!strncasecmp("ID", object, 1)) {
			field->type = PRINT_ID;
			field->name = xstrdup("ID");
			field->len = 6;
			field->print_routine = print_fields_uint;
		} else if(!strncasecmp("NormShares", object, 5)) {
			field->type = PRINT_NORMS;
			field->name = xstrdup("Norm Shares");
			field->len = 11;
			field->print_routine = print_fields_double;
		} else if(!strncasecmp("NormUsage", object, 5)) {
			field->type = PRINT_NORMU;
			field->name = xstrdup("Norm Usage");
			field->len = 11;
			field->print_routine = print_fields_double;
		} else if(!strncasecmp("RawShares", object, 4)) {
			field->type = PRINT_RAWS;
			field->name = xstrdup("Raw Shares");
			field->len = 10;
			field->print_routine = print_fields_uint32;
		} else if(!strncasecmp("RawUsage", object, 4)) {
			field->type = PRINT_RAWU;
			field->name = xstrdup("Raw Usage");
			field->len = 11;
			field->print_routine = print_fields_uint64;
		} else if(!strncasecmp("User", object, 1)) {
			field->type = PRINT_USER;
			field->name = xstrdup("User");
			field->len = 10;
			field->print_routine = print_fields_str;
		} else {
			exit_code=1;
			fprintf(stderr, "Unknown field '%s'\n", object);
			exit(1);
			xfree(field);
			continue;
		}
		if((tmp_char = strstr(object, "\%"))) {
			int newlen = atoi(tmp_char+1);
			if(newlen) 
				field->len = newlen;
		}
		list_append(print_fields_list, field);
	}
	list_iterator_destroy(itr);
	list_destroy(format_list);

	if(exit_code) {
		list_destroy(print_fields_list);
		return SLURM_ERROR;
	}

	itr2 = list_iterator_create(print_fields_list);
	print_fields_header(print_fields_list);

	field_count = list_count(print_fields_list);

	if(!resp->assoc_shares_list || !list_count(resp->assoc_shares_list))
		return SLURM_SUCCESS;
	tree_list = list_create(destroy_acct_print_tree);
	itr = list_iterator_create(resp->assoc_shares_list);
	while((assoc = list_next(itr))) {
		int curr_inx = 1;
		char *tmp_char = NULL;
		char *local_acct = NULL;

		while((field = list_next(itr2))) {
			switch(field->type) {
			case PRINT_ACCOUNT:
				if(assoc->user) 
					local_acct = xstrdup_printf(
						"|%s", assoc->name);
				else 
					local_acct = xstrdup(assoc->name);
				
				print_acct = get_tree_acct_name(
					local_acct,
					assoc->parent, tree_list);
				xfree(local_acct);
				field->print_routine(
					field, 
					print_acct,
					(curr_inx == field_count));
				break;
			case PRINT_CLUSTER:
				field->print_routine(
					field,
					assoc->cluster,
					(curr_inx == field_count));
				break;
			case PRINT_EUSED:
				field->print_routine(field, 
						     assoc->usage_efctv,
						     (curr_inx == field_count));
				break;
			case PRINT_FSFACTOR:
				field->print_routine(field,
						     (assoc->shares_norm -
						     (double)assoc->usage_efctv
						      + 1.0) / 2.0,
						     (curr_inx == field_count));
				break;
			case PRINT_ID:
				field->print_routine(field, 
						     assoc->assoc_id,
						     (curr_inx == field_count));
				break;
			case PRINT_NORMS:
				field->print_routine(field, 
						     assoc->shares_norm,
						     (curr_inx == field_count));
				break;
			case PRINT_NORMU:
				field->print_routine(field,
						     assoc->usage_norm,
						     (curr_inx == field_count));
				break;
			case PRINT_RAWS:
				field->print_routine(field,
						     assoc->shares_raw,
						     (curr_inx == field_count));
				break;
			case PRINT_RAWU:
				field->print_routine(field, 
						     assoc->usage_raw,
						     (curr_inx == field_count));
				break;
			case PRINT_USER:
				if(assoc->user)
					tmp_char = assoc->name;
				field->print_routine(field, 
						     tmp_char,
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

	if(tree_list) 
		list_destroy(tree_list);
			
	list_iterator_destroy(itr2);
	list_iterator_destroy(itr);
	list_destroy(print_fields_list);
	return rc;
}
