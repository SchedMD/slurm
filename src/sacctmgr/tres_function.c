/*****************************************************************************\
 *  tres_functions.c - functions dealing with TRES in the accounting system.
 *****************************************************************************
 *  Copyright (C) 2015 SchedMD LLC.
 *  Written by David Bigagli <david@schedmd.com>
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

static int _set_cond(int *start, int argc, char **argv,
		     slurmdb_tres_cond_t *tres_cond,
		     List format_list)
{
	int i;
	int set = 0;
	int end = 0;
	int command_len = 0;

	if (!tres_cond) {
		exit_code=1;
		fprintf(stderr, "No tres_cond given");
		return -1;
	}

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

		if (!xstrncasecmp(argv[i], "Set", MAX(command_len, 3))) {
			i--;
			break;
		} else if (!end && !xstrncasecmp(argv[i], "WithDeleted",
						 MAX(command_len, 5))) {
			tres_cond->with_deleted = 1;
		} else if (!end && !xstrncasecmp(argv[i], "where",
						 MAX(command_len, 5))) {
			continue;
		} else if (!end
			  || !xstrncasecmp(argv[i], "Type",
					   MAX(command_len, 2))) {
			if (!tres_cond->type_list) {
				tres_cond->type_list =
					list_create(slurm_destroy_char);
			}
			if (slurm_addto_char_list(
				   tres_cond->type_list,
				   argv[i]+end))
				set = 1;
		} else if (!xstrncasecmp(argv[i], "Names",
					 MAX(command_len, 1))) {
			if (!tres_cond->name_list) {
				tres_cond->name_list =
					list_create(slurm_destroy_char);
			}
			if (slurm_addto_char_list(tres_cond->name_list,
						  argv[i]+end))
				set = 1;
		} else if (!xstrncasecmp(argv[i], "Format",
					 MAX(command_len, 1))) {
			if (format_list)
				slurm_addto_char_list(format_list, argv[i]+end);
		} else if (!xstrncasecmp(argv[i], "Ids",
					 MAX(command_len, 1))) {
			if (!tres_cond->id_list) {
				tres_cond->id_list =
					list_create(slurm_destroy_char);
			}
			if (slurm_addto_char_list(tres_cond->id_list,
						 argv[i]+end))
				set = 1;
		} else {
			exit_code=1;
			fprintf(stderr, " Unknown condition: %s\n"
				" Use keyword 'set' to modify value\n",
				argv[i]);
		}
	}

	(*start) = i;

	if (set)
		return 1;

	return 0;
}

/* sacctmgr_list_tres()
 */
int sacctmgr_list_tres(int argc, char **argv)
{
	List tres_list;
	ListIterator itr;
	ListIterator itr2;
	List format_list = list_create(slurm_destroy_char);
	List print_fields_list;
	slurmdb_tres_cond_t *tres_cond = xmalloc(sizeof(slurmdb_tres_cond_t));
	slurmdb_tres_rec_t *tres;
	int field_count, i;
	print_field_t *field;

    	for (i=0; i<argc; i++) {
		int command_len = strlen(argv[i]);
		if (!xstrncasecmp(argv[i], "Where", MAX(command_len, 5))
		    || !xstrncasecmp(argv[i], "Set", MAX(command_len, 3)))
			i++;
		_set_cond(&i, argc, argv, tres_cond, format_list);
	}

	if (exit_code) {
		slurmdb_destroy_tres_cond(tres_cond);
		FREE_NULL_LIST(format_list);
		return SLURM_ERROR;
	}

	if (!list_count(format_list)) {
		/* Append to the format list the fields
		 * we want to print, these are the data structure
		 * members of the type returned by slurmdbd
		 */
		slurm_addto_char_list(format_list, "Type,Name%15,ID");
	}

	tres_list = slurmdb_tres_get(db_conn, tres_cond);
	slurmdb_destroy_tres_cond(tres_cond);

	if (!tres_list) {
		exit_code=1;
		fprintf(stderr, " Problem with query.\n");
		FREE_NULL_LIST(format_list);
		return SLURM_ERROR;
	}


	/* Process the format list creating a list of
	 * print field_t structures
	 */
	print_fields_list = sacctmgr_process_format_list(format_list);
	FREE_NULL_LIST(format_list);

	itr = list_iterator_create(tres_list);
	itr2 = list_iterator_create(print_fields_list);
	print_fields_header(print_fields_list);
	field_count = list_count(print_fields_list);

	/* For each tres prints the data structure members
	 */
	while ((tres = list_next(itr))) {
		int curr_inx = 1;
		while ((field = list_next(itr2))) {
			switch (field->type) {
				case PRINT_NAME:
					field->print_routine(field,
							     tres->name,
							     (curr_inx ==
							      field_count));
					break;
				case PRINT_ID:
					field->print_routine(field,
							     tres->id,
							     (curr_inx ==
							      field_count));
					break;
				case PRINT_TYPE:
					field->print_routine(field,
							     tres->type,
							     (curr_inx ==
							      field_count));
					break;
			}
			curr_inx++;
		}
		list_iterator_reset(itr2);
		printf("\n");
	}
	list_iterator_destroy(itr);
	list_iterator_destroy(itr2);
	FREE_NULL_LIST(tres_list);
	FREE_NULL_LIST(print_fields_list);

	return 0;
}
