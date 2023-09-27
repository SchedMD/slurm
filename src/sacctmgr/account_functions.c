/*****************************************************************************\
 *  account_functions.c - functions dealing with accounts in the
 *                        accounting system.
 *****************************************************************************
 *  Copyright (C) 2002-2007 The Regents of the University of California.
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

#include "src/sacctmgr/sacctmgr.h"
#include "src/common/assoc_mgr.h"
#include "src/interfaces/data_parser.h"

static int _set_cond(int *start, int argc, char **argv,
		     slurmdb_account_cond_t *acct_cond,
		     List format_list)
{
	int i;
	int cond_set = 0;
	int end = 0;
	slurmdb_assoc_cond_t *assoc_cond = NULL;
	int command_len = 0;
	int option = 0;

	if (!acct_cond) {
		exit_code=1;
		fprintf(stderr, "No acct_cond given");
		return -1;
	}

	if (!acct_cond->assoc_cond) {
		acct_cond->assoc_cond =
			xmalloc(sizeof(slurmdb_assoc_cond_t));
	}

	assoc_cond = acct_cond->assoc_cond;

	for (i=(*start); i<argc; i++) {
		end = parse_option_end(argv[i]);
		if (!end)
			command_len=strlen(argv[i]);
		else {
			command_len=end-1;
			if (argv[i][end] == '=') {
				option = (int)argv[i][end-1];
				end++;
			}
		}

		if (!xstrncasecmp(argv[i], "Set", MAX(command_len, 3))) {
			i--;
			break;
		} else if (!end &&
			   !xstrncasecmp(argv[i], "WithAssoc",
					 MAX(command_len, 5))) {
			acct_cond->with_assocs = 1;
		} else if (!end &&
			   !xstrncasecmp(argv[i], "WithCoordinators",
					 MAX(command_len, 5))) {
			acct_cond->with_coords = 1;
		} else if (!end &&
			   !xstrncasecmp(argv[i], "WithDeleted",
					 MAX(command_len, 5))) {
			acct_cond->with_deleted = 1;
			assoc_cond->with_deleted = 1;
		} else if (!end &&
			   !xstrncasecmp(argv[i], "WithRawQOSLevel",
					 MAX(command_len, 5))) {
			assoc_cond->with_raw_qos = 1;
		} else if (!end && !xstrncasecmp(argv[i], "WOPLimits",
						 MAX(command_len, 4))) {
			assoc_cond->without_parent_limits = 1;
		} else if (!end && !xstrncasecmp(argv[i], "where",
					       MAX(command_len, 5))) {
			continue;
		} else if (!end
			  || !xstrncasecmp(argv[i], "Names",
					   MAX(command_len, 1))
			  || !xstrncasecmp(argv[i], "Accounts",
					   MAX(command_len, 1))
			  || !xstrncasecmp(argv[i], "Acct",
					   MAX(command_len, 4))) {
			if (!assoc_cond->acct_list) {
				assoc_cond->acct_list = list_create(xfree_ptr);
			}
			if (slurm_addto_char_list(
				   assoc_cond->acct_list,
				   argv[i]+end))
				cond_set |= SA_SET_USER;
		} else if (!xstrncasecmp(argv[i], "Descriptions",
					 MAX(command_len, 1))) {
			if (!acct_cond->description_list) {
				acct_cond->description_list =
					list_create(xfree_ptr);
			}
			if (slurm_addto_char_list(acct_cond->description_list,
						 argv[i]+end))
				cond_set |= SA_SET_USER;
		} else if (!xstrncasecmp(argv[i], "Format",
					 MAX(command_len, 1))) {
			if (format_list)
				slurm_addto_char_list(format_list, argv[i]+end);
		} else if (!xstrncasecmp(argv[i], "Organizations",
					 MAX(command_len, 1))) {
			if (!acct_cond->organization_list) {
				acct_cond->organization_list =
					list_create(xfree_ptr);
			}
			if (slurm_addto_char_list(acct_cond->organization_list,
						 argv[i]+end))
				cond_set |= SA_SET_USER;
		} else if (sacctmgr_set_assoc_cond(
				    assoc_cond, argv[i], argv[i]+end,
				    command_len, option)) {
			cond_set |= SA_SET_ASSOC;
		} else {
			exit_code=1;
			fprintf(stderr, " Unknown condition: %s\n"
				" Use keyword 'set' to modify value\n",
				argv[i]);
		}
	}

	(*start) = i;

	return cond_set;
}

static int _set_rec(int *start, int argc, char **argv,
		    List acct_list,
		    List cluster_list,
		    slurmdb_account_rec_t *acct,
		    slurmdb_assoc_rec_t *assoc)
{
	int i;
	int rec_set = 0;
	int end = 0;
	int command_len = 0;
	int option = 0;

	xassert(acct);
	xassert(assoc);

	for (i=(*start); i<argc; i++) {
		end = parse_option_end(argv[i]);
		if (!end)
			command_len=strlen(argv[i]);
		else {
			command_len=end-1;
			if (argv[i][end] == '=') {
				option = (int)argv[i][end-1];
				end++;
			}
		}

		if (!xstrncasecmp(argv[i], "Where", MAX(command_len, 5))) {
			i--;
			break;
		} else if (!end && !xstrncasecmp(argv[i], "set",
						 MAX(command_len, 3))) {
			continue;
		} else if (!end
			  || !xstrncasecmp(argv[i], "Accounts",
					   MAX(command_len, 1))
			  || !xstrncasecmp(argv[i], "Names",
					   MAX(command_len, 1))
			  || !xstrncasecmp(argv[i], "Acct",
					   MAX(command_len, 4))) {
			if (acct_list)
				slurm_addto_char_list(acct_list, argv[i]+end);
			else {
				exit_code=1;
				fprintf(stderr,
					" Can't modify the name "
					"of an account\n");
			}
		} else if (!xstrncasecmp(argv[i], "Clusters",
					 MAX(command_len, 1))) {
			if (cluster_list)
				slurm_addto_char_list(cluster_list,
						      argv[i]+end);
			else {
				exit_code=1;
				fprintf(stderr,
					" Can't modify the cluster "
					"of an account\n");
			}
		} else if (!xstrncasecmp(argv[i], "Description",
					 MAX(command_len, 1))) {
			acct->description =  strip_quotes(argv[i]+end, NULL, 1);
			rec_set |= SA_SET_USER;

		} else if (!xstrncasecmp(argv[i], "Organization",
					 MAX(command_len, 1))) {
			acct->organization = strip_quotes(argv[i]+end, NULL, 1);
			rec_set |= SA_SET_USER;

		} else if (!xstrncasecmp(argv[i], "RawUsage",
					 MAX(command_len, 7))) {
			uint32_t usage;
			if (!assoc)
				continue;
			assoc->usage = xmalloc(sizeof(slurmdb_assoc_usage_t));
			if (get_uint(argv[i]+end, &usage,
				     "RawUsage") == SLURM_SUCCESS) {
				assoc->usage->usage_raw = usage;
				rec_set |= SA_SET_ASSOC;
			}
		} else if (sacctmgr_set_assoc_rec(
					      assoc, argv[i], argv[i]+end,
					      command_len, option)) {
			rec_set |= SA_SET_ASSOC;
		} else {
			exit_code=1;
			fprintf(stderr, " Unknown option: %s\n"
				" Use keyword 'where' to modify condition\n",
				argv[i]);
		}
	}

	(*start) = i;
	return rec_set;
}

extern int sacctmgr_add_account(int argc, char **argv)
{
	int rc = SLURM_SUCCESS;
	int i=0;
	char *ret_str = NULL;
	int limit_set = 0;
	slurmdb_add_assoc_cond_t add_assoc;
	slurmdb_account_rec_t *start_acct = xmalloc(sizeof(*start_acct));
	slurmdb_assoc_rec_t *start_assoc;

	slurmdb_init_add_assoc_cond(&add_assoc, 0);
	start_assoc = &add_assoc.assoc;
	add_assoc.acct_list = list_create(xfree_ptr);
	add_assoc.cluster_list = list_create(xfree_ptr);

	for (i = 0; i < argc; i++) {
		int command_len = strlen(argv[i]);
		if (!xstrncasecmp(argv[i], "Where", MAX(command_len, 5))
		    || !xstrncasecmp(argv[i], "Set", MAX(command_len, 3)))
			i++;
		limit_set += _set_rec(&i, argc, argv,
				      add_assoc.acct_list,
				      add_assoc.cluster_list,
				      start_acct, start_assoc);
	}
	if (exit_code) {
		slurmdb_destroy_account_rec(start_acct);
		return SLURM_ERROR;
	}

	if (!list_count(add_assoc.acct_list)) {
		FREE_NULL_LIST(add_assoc.acct_list);
		FREE_NULL_LIST(add_assoc.cluster_list);
		slurmdb_destroy_account_rec(start_acct);
		exit_code = 1;
		fprintf(stderr, " Need name of account to add.\n");
		return SLURM_SUCCESS;
	}

	notice_thread_init();
	ret_str = slurmdb_accounts_add_cond(db_conn, &add_assoc, start_acct);
	rc = errno;
	notice_thread_fini();

	if (rc == SLURM_SUCCESS) {
		if (ret_str) {
			printf("%s", ret_str);
			xfree(ret_str);
			if (limit_set) {
				printf(" Settings\n");
				sacctmgr_print_assoc_limits(start_assoc);
			}
		}
		if (commit_check("Would you like to commit changes?")) {
			slurmdb_connection_commit(db_conn, 1);
		} else {
			printf(" Changes Discarded\n");
			slurmdb_connection_commit(db_conn, 0);
		}
	} else if (rc == SLURM_NO_CHANGE_IN_DATA) {
		printf(" %s", ret_str ? ret_str : slurm_strerror(rc));
	} else {
		exit_code=1;
		if (ret_str)
			fprintf(stderr, " error: %s\n", ret_str);
		else
			fprintf(stderr,
				" error: Problem adding account associations: %s\n",
				slurm_strerror(rc));
		rc = SLURM_ERROR;
	}

	xfree(ret_str);
	FREE_NULL_LIST(add_assoc.acct_list);
	FREE_NULL_LIST(add_assoc.cluster_list);
	slurmdb_destroy_account_rec(start_acct);

	return rc;
}

extern int sacctmgr_list_account(int argc, char **argv)
{
	int rc = SLURM_SUCCESS;
	slurmdb_account_cond_t *acct_cond =
		xmalloc(sizeof(slurmdb_account_cond_t));
 	List acct_list;
	int i=0, cond_set=0, prev_set=0;
	ListIterator itr = NULL;
	ListIterator itr2 = NULL;
	slurmdb_account_rec_t *acct = NULL;
	slurmdb_assoc_rec_t *assoc = NULL;
	char *tmp_char = NULL;
	uint32_t tmp_uint32;

	int field_count = 0;

	print_field_t *field = NULL;

	List format_list = list_create(xfree_ptr);
	List print_fields_list; /* types are of print_field_t */

	acct_cond->with_assocs = with_assoc_flag;

	for (i=0; i<argc; i++) {
		int command_len = strlen(argv[i]);
		if (!xstrncasecmp(argv[i], "Where", MAX(command_len, 5))
		    || !xstrncasecmp(argv[i], "Set", MAX(command_len, 3)))
			i++;
		prev_set = _set_cond(&i, argc, argv, acct_cond, format_list);
		cond_set |=  prev_set;
	}

	if (exit_code) {
		slurmdb_destroy_account_cond(acct_cond);
		FREE_NULL_LIST(format_list);
		return SLURM_ERROR;
	} else if (!list_count(format_list)) {
		slurm_addto_char_list(format_list, "Acc,Des,O");
		if (acct_cond->with_assocs)
			slurm_addto_char_list(format_list,
					      "Cl,ParentN,U,Share,Priority,"
					      "GrpJ,GrpN,"
					      "GrpCPUs,GrpMEM,GrpS,GrpWall,GrpCPUMins,"
					      "MaxJ,MaxN,MaxCPUs,MaxS,MaxW,"
					      "MaxCPUMins,QOS,DefaultQOS");

		if (acct_cond->with_coords)
			slurm_addto_char_list(format_list, "Coord");

	}

	if (!acct_cond->with_assocs && (cond_set & SA_SET_ASSOC)) {
		if (!commit_check("You requested options that are only valid "
				 "when querying with the withassoc option.\n"
				 "Are you sure you want to continue?")) {
			printf("Aborted\n");
			FREE_NULL_LIST(format_list);
			slurmdb_destroy_account_cond(acct_cond);
			return SLURM_SUCCESS;
		}
	}

	print_fields_list = sacctmgr_process_format_list(format_list);
	FREE_NULL_LIST(format_list);

	if (exit_code) {
		slurmdb_destroy_account_cond(acct_cond);
		FREE_NULL_LIST(print_fields_list);
		return SLURM_ERROR;
	}

	acct_list = slurmdb_accounts_get(db_conn, acct_cond);
	slurmdb_destroy_account_cond(acct_cond);

	if (mime_type) {
		DATA_DUMP_CLI_SINGLE(OPENAPI_ACCOUNTS_RESP, acct_list, argc,
				     argv, db_conn, mime_type, data_parser, rc);
		FREE_NULL_LIST(print_fields_list);
		FREE_NULL_LIST(acct_list);
		return rc;
	}

	if (!acct_list) {
		exit_code=1;
		fprintf(stderr, " Problem with query.\n");
		FREE_NULL_LIST(print_fields_list);
		return SLURM_ERROR;
	}

	itr = list_iterator_create(acct_list);
	itr2 = list_iterator_create(print_fields_list);
	print_fields_header(print_fields_list);

	field_count = list_count(print_fields_list);

	while((acct = list_next(itr))) {
		if (acct->assoc_list) {
			ListIterator itr3 =
				list_iterator_create(acct->assoc_list);
			while((assoc = list_next(itr3))) {
				int curr_inx = 1;
				while((field = list_next(itr2))) {
					switch(field->type) {
					case PRINT_ACCT:
						field->print_routine(
							field, acct->name,
							(curr_inx ==
							 field_count));
						break;
					case PRINT_COORDS:
						field->print_routine(
							field,
							&acct->coordinators,
							(curr_inx ==
							 field_count));
						break;
					case PRINT_DESC:
						field->print_routine(
							field,
							acct->description,
							(curr_inx ==
							 field_count));
						break;
					case PRINT_ORG:
						field->print_routine(
							field,
							acct->organization,
							(curr_inx ==
							 field_count));
						break;
					default:
						sacctmgr_print_assoc_rec(
							assoc, field, NULL,
							(curr_inx ==
							 field_count));
						break;
					}
					curr_inx++;
				}
				list_iterator_reset(itr2);
				printf("\n");
			}
			list_iterator_destroy(itr3);
		} else {
			int curr_inx = 1;
			while((field = list_next(itr2))) {
				switch(field->type) {
				case PRINT_QOS:
					tmp_char = get_qos_complete_str(NULL,
									NULL);
					field->print_routine(
						field,
						tmp_char,
						(curr_inx == field_count));
					xfree(tmp_char);
					break;
				case PRINT_ACCT:
					field->print_routine(
						field, acct->name,
						(curr_inx ==
						 field_count));
					break;
				case PRINT_COORDS:
					field->print_routine(
						field,
						&acct->coordinators,
						(curr_inx ==
						 field_count));
					break;
				case PRINT_DESC:
					field->print_routine(
						field, acct->description,
						(curr_inx ==
						 field_count));
					break;
				case PRINT_ORG:
					field->print_routine(
						field, acct->organization,
						(curr_inx ==
						 field_count));
					break;
				case PRINT_PRIO:
					tmp_uint32 = INFINITE;
					field->print_routine(
						field,
						&tmp_uint32,
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
	}

	list_iterator_destroy(itr2);
	list_iterator_destroy(itr);
	FREE_NULL_LIST(acct_list);
	FREE_NULL_LIST(print_fields_list);

	return rc;
}

extern int sacctmgr_modify_account(int argc, char **argv)
{
	int rc = SLURM_SUCCESS;
	slurmdb_account_cond_t *acct_cond =
		xmalloc(sizeof(slurmdb_account_cond_t));
	slurmdb_account_rec_t *acct = xmalloc(sizeof(slurmdb_account_rec_t));
	slurmdb_assoc_rec_t *assoc =
		xmalloc(sizeof(slurmdb_assoc_rec_t));

	int i=0;
	int cond_set = 0, prev_set = 0, rec_set = 0, set = 0;
	List ret_list = NULL;

	slurmdb_init_assoc_rec(assoc, 0);

	for (i=0; i<argc; i++) {
		int command_len = strlen(argv[i]);
		if (!xstrncasecmp(argv[i], "Where", MAX(command_len, 5))) {
			i++;
			prev_set = _set_cond(&i, argc, argv, acct_cond, NULL);
			cond_set |= prev_set;
		} else if (!xstrncasecmp(argv[i], "Set", MAX(command_len, 3))) {
			i++;
			prev_set = _set_rec(&i, argc, argv, NULL, NULL,
					    acct, assoc);
			rec_set |= prev_set;
		} else {
			prev_set = _set_cond(&i, argc, argv, acct_cond, NULL);
			cond_set |= prev_set;
		}
	}

	if (exit_code) {
		slurmdb_destroy_account_cond(acct_cond);
		slurmdb_destroy_account_rec(acct);
		slurmdb_destroy_assoc_rec(assoc);
		return SLURM_ERROR;
	} else if (!rec_set) {
		exit_code=1;
		fprintf(stderr, " You didn't give me anything to set\n");
		slurmdb_destroy_account_cond(acct_cond);
		slurmdb_destroy_account_rec(acct);
		slurmdb_destroy_assoc_rec(assoc);
		return SLURM_ERROR;
	} else if (!cond_set) {
		if (!commit_check("You didn't set any conditions with 'WHERE'.\n"
				 "Are you sure you want to continue?")) {
			printf("Aborted\n");
			slurmdb_destroy_account_cond(acct_cond);
			slurmdb_destroy_account_rec(acct);
			slurmdb_destroy_assoc_rec(assoc);
			return SLURM_SUCCESS;
		}
	}

	// Special case:  reset raw usage only
	if (assoc->usage) {
		rc = SLURM_ERROR;
		if (assoc->usage->usage_raw == 0.0)
			rc = sacctmgr_remove_assoc_usage(acct_cond->assoc_cond);
		else
			error("Raw usage can only be set to 0 (zero)");

		slurmdb_destroy_account_cond(acct_cond);
		slurmdb_destroy_account_rec(acct);
		slurmdb_destroy_assoc_rec(assoc);
		return rc;
	}

	notice_thread_init();
	if (rec_set & SA_SET_USER) { // process the account changes
		if (cond_set == SA_SET_ASSOC) {
			exit_code=1;
			fprintf(stderr,
				" There was a problem with your "
				"'where' options.\n");
			rc = SLURM_ERROR;
			goto assoc_start;
		}
		ret_list = slurmdb_accounts_modify(
			db_conn, acct_cond, acct);
		if (ret_list && list_count(ret_list)) {
			char *object = NULL;
			ListIterator itr = list_iterator_create(ret_list);
			printf(" Modified accounts...\n");
			while((object = list_next(itr))) {
				printf("  %s\n", object);
			}
			list_iterator_destroy(itr);
			set = 1;
		} else if (ret_list) {
			printf(" Nothing modified\n");
			rc = SLURM_ERROR;
		} else {
			exit_code=1;
			fprintf(stderr, " Error with request: %s\n",
				slurm_strerror(errno));

			rc = SLURM_ERROR;
		}

		FREE_NULL_LIST(ret_list);
	}

assoc_start:
	if (rec_set & SA_SET_ASSOC) { // process the association changes
		if ((cond_set == SA_SET_USER) &&
		    !acct_cond->assoc_cond->acct_list) {
			rc = SLURM_ERROR;
			exit_code=1;
			fprintf(stderr,
				" There was a problem with your "
				"'where' options.\n");
			goto assoc_end;
		}

		ret_list = slurmdb_associations_modify(
			db_conn, acct_cond->assoc_cond, assoc);

		if (ret_list && list_count(ret_list)) {
			char *object = NULL;
			ListIterator itr = list_iterator_create(ret_list);
			printf(" Modified account associations...\n");
			while ((object = list_next(itr))) {
				printf("  %s\n", object);
			}
			list_iterator_destroy(itr);
			set = 1;
		} else if (ret_list) {
			printf(" Nothing modified\n");
			rc = SLURM_ERROR;
		} else if ((errno == ESLURM_INVALID_PARENT_ACCOUNT) &&
			   assoc->parent_acct) {
			slurmdb_account_rec_t *acct_rec =
				sacctmgr_find_account(assoc->parent_acct);
			if (!acct_rec) {
				exit_code=1;
				fprintf(stderr,
					" Parent Account %s doesn't exist.\n",
					assoc->parent_acct);
				rc = SLURM_ERROR;
				goto assoc_end;
			}
		} else {
			exit_code=1;
			fprintf(stderr, " Error with request: %s\n",
				slurm_strerror(errno));

			rc = SLURM_ERROR;
		}

		FREE_NULL_LIST(ret_list);
	}

assoc_end:

	notice_thread_fini();
	if (set) {
		if (commit_check("Would you like to commit changes?"))
			slurmdb_connection_commit(db_conn, 1);
		else {
			printf(" Changes Discarded\n");
			slurmdb_connection_commit(db_conn, 0);
		}
	}
	slurmdb_destroy_account_cond(acct_cond);
	slurmdb_destroy_account_rec(acct);
	slurmdb_destroy_assoc_rec(assoc);

	return rc;
}

extern int sacctmgr_delete_account(int argc, char **argv)
{
	int rc = SLURM_SUCCESS;
	slurmdb_account_cond_t *acct_cond =
		xmalloc(sizeof(slurmdb_account_cond_t));
	int i = 0;
	List ret_list = NULL;
	ListIterator itr = NULL;
	int cond_set = 0, prev_set = 0;

	for (i = 0; i < argc; i++) {
		int command_len = strlen(argv[i]);
		if (!xstrncasecmp(argv[i], "Where", MAX(command_len, 5))
		    || !xstrncasecmp(argv[i], "Set", MAX(command_len, 3)))
			i++;
		prev_set = _set_cond(&i, argc, argv, acct_cond, NULL);
		cond_set |= prev_set;
	}

	if (!cond_set) {
		exit_code = 1;
		fprintf(stderr,
			" No conditions given to remove, not executing.\n");
		slurmdb_destroy_account_cond(acct_cond);
		return SLURM_ERROR;
	}

	if (exit_code) {
		slurmdb_destroy_account_cond(acct_cond);
		return SLURM_ERROR;
	}

	if (!acct_cond->assoc_cond) {
		error("%s: Association condition is NULL", __func__);
		slurmdb_destroy_account_cond(acct_cond);
		return SLURM_ERROR;
	}

	/* check to see if person is trying to remove root account.  This is
	 * bad, and should not be allowed outside of deleting a cluster.
	 */
	if (acct_cond->assoc_cond
	   && acct_cond->assoc_cond->acct_list
	   && list_count(acct_cond->assoc_cond->acct_list)) {
		char *tmp_char = NULL;
		itr = list_iterator_create(acct_cond->assoc_cond->acct_list);
		while ((tmp_char = list_next(itr))) {
			if (!xstrcasecmp(tmp_char, "root"))
				break;
		}
		list_iterator_destroy(itr);
		if (tmp_char) {
			exit_code=1;
			fprintf(stderr, " You are not allowed to remove "
				"the root account.\n"
				" Use remove cluster instead.\n");
			slurmdb_destroy_account_cond(acct_cond);
			return SLURM_ERROR;
		}
	}

	acct_cond->assoc_cond->only_defs = 0;

	notice_thread_init();
	if (cond_set == SA_SET_USER) {
		ret_list = slurmdb_accounts_remove(
			db_conn, acct_cond);
	} else if (cond_set & SA_SET_ASSOC) {
		ret_list = slurmdb_associations_remove(
			db_conn, acct_cond->assoc_cond);
	}
	rc = errno;
	notice_thread_fini();
	slurmdb_destroy_account_cond(acct_cond);

	if (ret_list && list_count(ret_list)) {
		char *object = NULL;
		ListIterator itr = NULL;
		itr = list_iterator_create(ret_list);

		/* Check to see if person is trying to remove a default
		 * account of a user.  _isdefault only works with the
		 * output from slurmdb_accounts_remove, and
		 * with a previously got assoc_list.
		 */
		if (rc == ESLURM_NO_REMOVE_DEFAULT_ACCOUNT){
			fprintf(stderr, " Error with request: %s\n",
				slurm_strerror(rc));
			while((object = list_next(itr))) {
				fprintf(stderr,"  %s\n", object);
			}
			fprintf(stderr, " Please either remove the "
				"accounts listed "
				"above from list and resubmit,\n"
				" or change these users' default accounts to "
				"remove the account(s).\n"
				" Changes Discarded\n");
			slurmdb_connection_commit(db_conn, 0);
			goto end_it;
		}

		/* If there were jobs running with an association to
		   be deleted, don't.
		*/
		if (rc == ESLURM_JOBS_RUNNING_ON_ASSOC) {
			fprintf(stderr, " Error with request: %s\n",
				slurm_strerror(rc));
			while((object = list_next(itr))) {
				fprintf(stderr,"  %s\n", object);
			}
			slurmdb_connection_commit(db_conn, 0);
			goto end_it;
		}

		if (cond_set == SA_SET_USER) {
			printf(" Deleting accounts...\n");
		} else if (cond_set & SA_SET_ASSOC) {
			printf(" Deleting account associations...\n");
		}
		while((object = list_next(itr))) {
			printf("  %s\n", object);
		}
		list_iterator_destroy(itr);
		if (commit_check("Would you like to commit changes?")) {
			slurmdb_connection_commit(db_conn, 1);
		} else {
			printf(" Changes Discarded\n");
			slurmdb_connection_commit(db_conn, 0);
		}
	} else if (ret_list) {
		printf(" Nothing deleted\n");
		rc = SLURM_ERROR;
	} else {
		exit_code=1;
		fprintf(stderr, " Error with request: %s\n",
			slurm_strerror(errno));

		rc = SLURM_ERROR;
	}

end_it:

	FREE_NULL_LIST(ret_list);

	return rc;
}
