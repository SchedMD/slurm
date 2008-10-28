/*****************************************************************************\
 *  process.c -  process the return from get_share_info. 
 *****************************************************************************
 *  Copyright (C) 2008 Lawrence Livermore National Security.
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

#include "src/sshare/sshare.h"

extern void _sshare_print_time(print_field_t *field,
			       uint64_t value, int last)
{
	/* (value == unset)  || (value == cleared) */
	if((value == NO_VAL) || (value == INFINITE)) {
		if(print_fields_parsable_print 
		   == PRINT_FIELDS_PARSABLE_NO_ENDING
		   && last)
			;
		else if(print_fields_parsable_print)
			printf("|");	
		else				
			printf("%-*s ", field->len, " ");
	} else {
		char *output = NULL;
		double temp_d = (double)value;
		
		switch(time_format) {
		case SSHARE_TIME_SECS:
			output = xstrdup_printf("%llu", value);
			break;
		case SSHARE_TIME_MINS:
			temp_d /= 60;
			output = xstrdup_printf("%.0lf", temp_d);
			break;
		case SSHARE_TIME_HOURS:
			temp_d /= 3600;
			output = xstrdup_printf("%.0lf", temp_d);
			break;
		default:
			temp_d /= 60;
			output = xstrdup_printf("%.0lf", temp_d);
			break;
		}
		
		if(print_fields_parsable_print 
		   == PRINT_FIELDS_PARSABLE_NO_ENDING
		   && last)
			printf("%s", output);
		else if(print_fields_parsable_print)
			printf("%s|", output);	
		else
			printf("%*.*s ", field->len, field->len, output);
		xfree(output);
	}
}


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
		PRINT_FAIRSHARE,
		PRINT_ID,
		PRINT_NORME,
		PRINT_NORMS,
		PRINT_NORMU,
		PRINT_USED,
		PRINT_USER,
	};

	if(!resp)
		return SLURM_ERROR;

	format_list = list_create(slurm_destroy_char);
	slurm_addto_char_list(format_list,
			      "A,User,Id,Fair,NormShares,"
			      "Used,NormUsed,EUsed,NormEUsed");

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
		} else if(!strncasecmp("EUsed", object, 1)) {
			field->type = PRINT_EUSED;
			field->name = xstrdup("Effective Used");
			field->len = 19;
			field->print_routine = _sshare_print_time;
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
		}else if(!strncasecmp("NormEUsage", object, 5)) {
			field->type = PRINT_NORME;
			field->name = xstrdup("Norm EUsage");
			field->len = 11;
			field->print_routine = print_fields_double;
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
		} else if(!strncasecmp("Used", object, 4)) {
			field->type = PRINT_USED;
			field->name = xstrdup("Used");
			field->len = 19;
			field->print_routine = _sshare_print_time;
		} else if(!strncasecmp("User", object, 4)) {
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
			if(newlen > 0) 
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
		double tmp_double = 0.0;

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
						     assoc->eused_shares,
						     (curr_inx == field_count));
				break;
			case PRINT_FAIRSHARE:
				field->print_routine(
					field,
					assoc->fairshare,
					(curr_inx == field_count));
				break;
			case PRINT_ID:
				field->print_routine(field, 
						     assoc->assoc_id,
						     (curr_inx == field_count));
				break;
			case PRINT_NORME:
				tmp_double = ((double)assoc->eused_shares
					      / (double)resp->tot_shares);
				field->print_routine(field, 
						     tmp_double,
						     (curr_inx == field_count));
				break;
			case PRINT_NORMS:
				field->print_routine(field, 
						     assoc->norm_shares,
						     (curr_inx == field_count));
				break;
			case PRINT_NORMU:
				tmp_double = ((double)assoc->used_shares
					      / (double)resp->tot_shares);
/* 				info("norm used is %llu / %llu = %f", */
/* 				     assoc->used_shares, resp->tot_shares, */
/* 				     tmp_double); */
				field->print_routine(field, 
						     tmp_double,
						     (curr_inx == field_count));
				break;
			case PRINT_USED:
				field->print_routine(field, 
						     assoc->used_shares,
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
