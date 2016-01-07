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
#include "src/common/assoc_mgr.h"

extern int sacctmgr_add_federation(int argc, char *argv[])
{
	int rc = SLURM_SUCCESS;
	int i = 0, command_len = 0;
	slurmdb_federation_rec_t *federation = NULL;
	List name_list = list_create(slurm_destroy_char);
	List federation_list;

	ListIterator itr = NULL, itr_c = NULL;
	char *name = NULL;

	for (i=0; i<argc; i++) {
		int end = parse_option_end(argv[i]);
		if (!end)
			command_len=strlen(argv[i]);
		else
			command_len=end-1;
		if (!end
		   || !strncasecmp(argv[i], "Names", MAX(command_len, 1))
		   || !strncasecmp(argv[i], "Federations", MAX(command_len, 1))) {
			if (!slurm_addto_char_list(name_list, argv[i]+end))
				exit_code=1;
		}
	}
	if (exit_code) {
		FREE_NULL_LIST(name_list);
		return SLURM_ERROR;
	} else if (!list_count(name_list)) {
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
		federation = xmalloc(sizeof(slurmdb_federation_rec_t));
		slurmdb_init_federation_rec(federation, 0);

		list_append(federation_list, federation);
		federation->name = xstrdup(name);
		printf("  Name          = %s\n", federation->name);
	}
	list_iterator_destroy(itr);
	FREE_NULL_LIST(name_list);

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
		rc = acct_storage_g_add_federations(db_conn, my_uid, federation_list);
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
