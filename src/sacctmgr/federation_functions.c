/*****************************************************************************\
 *  federation_functions.c - functions dealing with Federations in the
 *                        accounting system.
 *****************************************************************************
 *  Copyright (C) 2016 SchedMD LLC.
 *  Written by Brian Christiansen <brian@schedmd.com>
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

#include "src/sacctmgr/sacctmgr.h"

static int _set_cond(int *start, int argc, char *argv[],
		     slurmdb_federation_cond_t *federation_cond,
		     List format_list)
{
	int i;
	int c_set = 0;
	int a_set = 0;
	int end = 0;
	int command_len = 0;

	for (i=(*start); i<argc; i++) {
		end = parse_option_end(argv[i]);
		if (!end)
			command_len=strlen(argv[i]);
		else {
			command_len=end-1;
			if (argv[i][end] == '=') {
				end++;
			}
		}

		if (!strncasecmp(argv[i], "Set", MAX(command_len, 3))) {
			i--;
			break;
		} else if (!end && !strncasecmp(argv[i], "where",
					       MAX(command_len, 5))) {
			continue;
		} else if (!end &&
			   !strncasecmp(argv[i], "WithDeleted",
					 MAX(command_len, 5))) {
			federation_cond->with_deleted = 1;
		} else if (!end || !strncasecmp(argv[i], "Names",
						MAX(command_len, 1))
			  || !strncasecmp(argv[i], "Federations",
					   MAX(command_len, 3))) {
			if (!federation_cond->federation_list)
				federation_cond->federation_list =
					list_create(slurm_destroy_char);
			if (slurm_addto_char_list(
					federation_cond->federation_list,
					argv[i]+end))
				a_set = 1;
		} else if (!strncasecmp(argv[i], "Format",
					 MAX(command_len, 2))) {
			if (format_list)
				slurm_addto_char_list(format_list, argv[i]+end);
		} else {
			exit_code=1;
			fprintf(stderr, " Unknown condition: %s\n"
				" Use keyword 'set' to modify value\n",
				argv[i]);
			break;
		}
	}
	(*start) = i;

	if (c_set && a_set)
		return 3;
	else if (a_set) {
		return 2;
	} else if (c_set)
		return 1;
	return 0;
}

static int _set_rec(int *start, int argc, char *argv[],
		    List name_list, slurmdb_federation_rec_t *fed)
{
	int i;
	int set = 0;
	int end = 0;
	int command_len = 0;
	int option = 0;

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

		if (!strncasecmp (argv[i], "Where", MAX(command_len, 5))) {
			i--;
			break;
		} else if (!end && !strncasecmp(argv[i], "set",
					       MAX(command_len, 3))) {
			continue;
		} else if (!end
			  || !strncasecmp (argv[i], "Name",
					   MAX(command_len, 1))) {
			if (name_list)
				slurm_addto_char_list(name_list, argv[i]+end);
		} else if (!fed) {
			continue;
		} else if (!strncasecmp (argv[i], "Flags",
					 MAX(command_len, 2))) {
			fed->flags = str_2_federation_flags(argv[i]+end,
								   option);
			if (fed->flags == FEDERATION_FLAG_NOTSET) {
				char *tmp_char = NULL;
				fed->flags = INFINITE;
				fed->flags &= (~FEDERATION_FLAG_NOTSET &
					       ~FEDERATION_FLAG_ADD &
					       ~FEDERATION_FLAG_REMOVE);
				tmp_char =
					slurmdb_federation_flags_str(
							fed->flags);
				printf(" Unknown federation flag used in:\n"
				       " '%s'\n"
				       " Valid federation flags are\n  '%s'\n",
				       argv[i]+end, tmp_char);
				xfree(tmp_char);
				exit_code = 1;
			} else
				set = 1;
		} else {
			exit_code = 1;
			printf(" Unknown option: %s\n"
			       " Use keyword 'where' to modify condition\n",
			       argv[i]);
		}
	}

	(*start) = i;

	return set;
}

extern int sacctmgr_add_federation(int argc, char *argv[])
{
	int rc = SLURM_SUCCESS;
	int i = 0, limit_set = 0;
	slurmdb_federation_rec_t *fed = NULL;
	slurmdb_federation_rec_t *start_fed =
		xmalloc(sizeof(slurmdb_federation_rec_t));
	List name_list = list_create(slurm_destroy_char);
	List federation_list;

	ListIterator itr = NULL, itr_c = NULL;
	char *name = NULL;

	slurmdb_init_federation_rec(start_fed, 0);

	for (i=0; i<argc; i++) {
		int command_len = strlen(argv[i]);
		if (!strncasecmp(argv[i], "Where", MAX(command_len, 5))
		    || !strncasecmp(argv[i], "Set", MAX(command_len, 3)))
			i++;
		limit_set += _set_rec(&i, argc, argv, name_list, start_fed);
	}
	if (exit_code) {
		FREE_NULL_LIST(name_list);
		slurmdb_destroy_federation_rec(start_fed);
		return SLURM_ERROR;
	} else if (!list_count(name_list)) {
		slurmdb_destroy_federation_rec(start_fed);
		FREE_NULL_LIST(name_list);
		exit_code=1;
		fprintf(stderr, " Need name of federation to add.\n");
		return SLURM_ERROR;
	} else {
		List temp_list = NULL;
		slurmdb_federation_cond_t fed_cond;

		slurmdb_init_federation_cond(&fed_cond, 0);
		fed_cond.federation_list = name_list;

		temp_list = acct_storage_g_get_federations(db_conn, my_uid,
							   &fed_cond);
		if (!temp_list) {
			exit_code=1;
			slurmdb_destroy_federation_rec(start_fed);
			fprintf(stderr,
				" Problem getting federations from database.  "
				"Contact your admin.\n");
			return SLURM_ERROR;
		}

		itr_c = list_iterator_create(name_list);
		itr = list_iterator_create(temp_list);
		while((name = list_next(itr_c))) {
			slurmdb_federation_rec_t *fed_rec = NULL;

			list_iterator_reset(itr);
			while((fed_rec = list_next(itr))) {
				if (!strcasecmp(fed_rec->name, name))
					break;
			}
			if (fed_rec) {
				printf(" This federation %s already exists.  "
				       "Not adding.\n", name);
				list_delete_item(itr_c);
			}
		}
		list_iterator_destroy(itr);
		list_iterator_destroy(itr_c);
		FREE_NULL_LIST(temp_list);
		if (!list_count(name_list)) {
			FREE_NULL_LIST(name_list);
			slurmdb_destroy_federation_rec(start_fed);
			return SLURM_ERROR;
		}
	}

	printf(" Adding Federation(s)\n");
	federation_list = list_create(slurmdb_destroy_federation_rec);
	itr = list_iterator_create(name_list);
	while((name = list_next(itr))) {
		if (!name[0]) {
			exit_code=1;
			fprintf(stderr, " No blank names are "
				"allowed when adding.\n");
			rc = SLURM_ERROR;
			continue;
		}
		fed = xmalloc(sizeof(slurmdb_federation_rec_t));
		slurmdb_init_federation_rec(fed, 0);
		list_append(federation_list, fed);
		slurmdb_copy_federation_rec_limits(fed, start_fed);
		fed->name = xstrdup(name);
		printf("  %s\n", fed->name);
	}
	list_iterator_destroy(itr);
	FREE_NULL_LIST(name_list);

	if (limit_set) {
		printf(" Settings\n");
		sacctmgr_print_federation_limits(start_fed);
	}
	slurmdb_destroy_federation_rec(start_fed);

	if (!list_count(federation_list)) {
		printf(" Nothing new added.\n");
		rc = SLURM_ERROR;
		goto end_it;
	}

	/* Since we are creating tables with add federation that can't be
	   rolled back.  So we ask before hand if they are serious
	   about it so we can rollback if needed.
	*/
	if (commit_check("Would you like to commit changes?")) {
		notice_thread_init();
		rc = acct_storage_g_add_federations(db_conn, my_uid,
						    federation_list);
		notice_thread_fini();
		if (rc == SLURM_SUCCESS) {
			acct_storage_g_commit(db_conn, 1);
		} else {
			exit_code=1;
			fprintf(stderr, " Problem adding federation(s): %s\n",
				slurm_strerror(rc));
			/* this isn't really needed, but just to be safe */
			acct_storage_g_commit(db_conn, 0);
		}
	} else {
		printf(" Changes Discarded\n");
		/* this isn't really needed, but just to be safe */
		acct_storage_g_commit(db_conn, 0);
	}

end_it:
	FREE_NULL_LIST(federation_list);

	return rc;
}

extern int sacctmgr_list_federation(int argc, char *argv[])
{
	int rc = SLURM_SUCCESS;
	slurmdb_federation_cond_t *federation_cond =
		xmalloc(sizeof(slurmdb_federation_cond_t));
	List federation_list;
	int i=0;
	ListIterator itr = NULL;
	ListIterator itr2 = NULL;
	slurmdb_federation_rec_t *federation = NULL;
	/*char *tmp_char = NULL;*/

	int field_count = 0;

	print_field_t *field = NULL;

	List format_list = list_create(slurm_destroy_char);
	List print_fields_list; /* types are of print_field_t */

	slurmdb_init_federation_cond(federation_cond, 0);
	federation_cond->federation_list = list_create(slurm_destroy_char);
	for (i=0; i<argc; i++) {
		int command_len = strlen(argv[i]);
		if (!strncasecmp(argv[i], "Where", MAX(command_len, 5))
		    || !strncasecmp(argv[i], "Set", MAX(command_len, 3)))
			i++;
		_set_cond(&i, argc, argv, federation_cond, format_list);
	}

	if (exit_code) {
		slurmdb_destroy_federation_cond(federation_cond);
		FREE_NULL_LIST(format_list);
		return SLURM_ERROR;
	}

	if (!list_count(format_list)) {
		slurm_addto_char_list(format_list, "Federation,Flags");
	}

	print_fields_list = sacctmgr_process_format_list(format_list);
	FREE_NULL_LIST(format_list);

	if (exit_code) {
		slurmdb_destroy_federation_cond(federation_cond);
		FREE_NULL_LIST(print_fields_list);
		return SLURM_ERROR;
	}

	federation_list = acct_storage_g_get_federations(db_conn, my_uid,
							 federation_cond);
	slurmdb_destroy_federation_cond(federation_cond);

	if (!federation_list) {
		exit_code=1;
		fprintf(stderr, " Problem with query.\n");
		FREE_NULL_LIST(print_fields_list);
		return SLURM_ERROR;
	}

	itr = list_iterator_create(federation_list);
	itr2 = list_iterator_create(print_fields_list);
	print_fields_header(print_fields_list);

	field_count = list_count(print_fields_list);

	while ((federation = list_next(itr))) {
		int curr_inx = 1;
		while((field = list_next(itr2))) {
			switch(field->type) {
			case PRINT_FEDERATION:
				field->print_routine(field,
						     federation->name,
						     (curr_inx == field_count));
				break;
			case PRINT_FLAGS:
			{
				char *tmp_char = slurmdb_federation_flags_str(
					federation->flags);
				field->print_routine(
					field,
					tmp_char,
					(curr_inx == field_count));
				xfree(tmp_char);
				break;
			}
			default:
				field->print_routine(field, NULL,
						     (curr_inx == field_count));
				break;
			}
			curr_inx++;
		}
		list_iterator_reset(itr2);
		printf("\n");
	}

	list_iterator_destroy(itr2);
	list_iterator_destroy(itr);
	FREE_NULL_LIST(federation_list);
	FREE_NULL_LIST(print_fields_list);

	return rc;
}

extern int sacctmgr_modify_federation(int argc, char *argv[])
{
	int rc = SLURM_SUCCESS;
	int i=0;
	int cond_set = 0, prev_set = 0, rec_set = 0, set = 0;
	List ret_list = NULL;
	slurmdb_federation_cond_t *federation_cond =
		xmalloc(sizeof(slurmdb_federation_cond_t));
	slurmdb_federation_rec_t *federation =
		xmalloc(sizeof(slurmdb_federation_rec_t));

	slurmdb_init_federation_cond(federation_cond, 0);
	slurmdb_init_federation_rec(federation, 0);

	for (i=0; i<argc; i++) {
		int command_len = strlen(argv[i]);
		if (!strncasecmp(argv[i], "Where", MAX(command_len, 5))) {
			i++;
			prev_set = _set_cond(&i, argc, argv,
					     federation_cond, NULL);
			cond_set |= prev_set;
		} else if (!strncasecmp(argv[i], "Set", MAX(command_len, 3))) {
			i++;
			prev_set = _set_rec(&i, argc, argv, NULL, federation);
			rec_set |= prev_set;
		} else {
			prev_set = _set_cond(&i, argc, argv,
					     federation_cond, NULL);
			cond_set |= prev_set;
		}
	}

	if (!rec_set) {
		exit_code=1;
		fprintf(stderr, " You didn't give me anything to set\n");
		rc = SLURM_ERROR;
		goto end_it;
	} else if (!cond_set) {
		if (!commit_check("You didn't set any conditions "
				  "with 'WHERE'.\n"
				  "Are you sure you want to continue?")) {
			printf("Aborted\n");
			rc = SLURM_SUCCESS;
			goto end_it;
		}
	} else if (exit_code) {
		rc = SLURM_ERROR;
		goto end_it;
	}

	notice_thread_init();
	ret_list = acct_storage_g_modify_federations(db_conn, my_uid,
						     federation_cond,
						     federation);

	if (ret_list && list_count(ret_list)) {
		char *object = NULL;
		ListIterator itr = list_iterator_create(ret_list);
		printf(" Modified federation...\n");
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

	notice_thread_fini();

	if (set) {
		if (commit_check("Would you like to commit changes?"))
			acct_storage_g_commit(db_conn, 1);
		else {
			printf(" Changes Discarded\n");
			acct_storage_g_commit(db_conn, 0);
		}
	}
end_it:
	slurmdb_destroy_federation_cond(federation_cond);
	slurmdb_destroy_federation_rec(federation);

	return rc;
}

extern int sacctmgr_delete_federation(int argc, char *argv[])
{
	int rc = SLURM_SUCCESS;
	slurmdb_federation_cond_t *fed_cond =
		xmalloc(sizeof(slurmdb_federation_cond_t));
	int i=0;
	List ret_list = NULL;
	int cond_set = 0, prev_set;

	slurmdb_init_federation_cond(fed_cond, 0);
	fed_cond->federation_list = list_create(slurm_destroy_char);

	for (i=0; i<argc; i++) {
		int command_len = strlen(argv[i]);
		if (!strncasecmp(argv[i], "Where", MAX(command_len, 5))
		    || !strncasecmp(argv[i], "Set", MAX(command_len, 3)))
			i++;
		prev_set = _set_cond(&i, argc, argv, fed_cond, NULL);
		cond_set |= prev_set;
	}

	if (exit_code) {
		slurmdb_destroy_federation_cond(fed_cond);
		return SLURM_ERROR;
	} else if (!cond_set) {
		exit_code=1;
		fprintf(stderr,
			" No conditions given to remove, not executing.\n");
		slurmdb_destroy_federation_cond(fed_cond);
		return SLURM_ERROR;
	}

	if (!list_count(fed_cond->federation_list)) {
		exit_code=1;
		fprintf(stderr,
			"problem with delete request.  "
			"Nothing given to delete.\n");
		slurmdb_destroy_federation_cond(fed_cond);
		return SLURM_SUCCESS;
	}
	notice_thread_init();
	ret_list = acct_storage_g_remove_federations(db_conn, my_uid, fed_cond);
	rc = errno;
	notice_thread_fini();

	slurmdb_destroy_federation_cond(fed_cond);

	if (ret_list && list_count(ret_list)) {
		char *object = NULL;
		ListIterator itr = list_iterator_create(ret_list);

		printf(" Deleting federations...\n");
		while((object = list_next(itr))) {
			printf("  %s\n", object);
		}
		list_iterator_destroy(itr);
		if (commit_check("Would you like to commit changes?")) {
			acct_storage_g_commit(db_conn, 1);
		} else {
			printf(" Changes Discarded\n");
			acct_storage_g_commit(db_conn, 0);
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

	FREE_NULL_LIST(ret_list);

	return rc;
}
