/*****************************************************************************\
 *  common.c - common functions for generating reports
 *             from accounting infrastructure.
 *****************************************************************************
 *
 *  Copyright (C) 2008 Lawrence Livermore National Security.
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

#include "sreport.h"
#include "src/common/proc_args.h"

extern void slurmdb_report_print_time(print_field_t *field, uint64_t value,
				      uint64_t total_time, int last)
{
	int abs_len = abs(field->len);

	if (!total_time)
		total_time = 1;

	/* (value == unset)  || (value == cleared) */
	if ((value == NO_VAL) || (value == INFINITE)) {
		if (print_fields_parsable_print
		   == PRINT_FIELDS_PARSABLE_NO_ENDING
		   && last)
			;
		else if (print_fields_parsable_print)
			printf("|");
		else
			printf("%-*s ", abs_len, " ");
	} else {
		char *output = NULL;
		double percent = (double)value;
		double temp_d = (double)value;

		switch(time_format) {
		case SLURMDB_REPORT_TIME_SECS:
			output = xstrdup_printf("%"PRIu64"", value);
			break;
		case SLURMDB_REPORT_TIME_MINS:
			temp_d /= 60;
			output = xstrdup_printf("%.0lf", temp_d);
			break;
		case SLURMDB_REPORT_TIME_HOURS:
			temp_d /= 3600;
			output = xstrdup_printf("%.0lf", temp_d);
			break;
		case SLURMDB_REPORT_TIME_PERCENT:
			percent /= total_time;
			percent *= 100;
			output = xstrdup_printf("%.2lf%%", percent);
			break;
		case SLURMDB_REPORT_TIME_SECS_PER:
			percent /= total_time;
			percent *= 100;
			output = xstrdup_printf("%"PRIu64"(%.2lf%%)",
						value, percent);
			break;
		case SLURMDB_REPORT_TIME_MINS_PER:
			percent /= total_time;
			percent *= 100;
			temp_d /= 60;
			output = xstrdup_printf("%.0lf(%.2lf%%)",
						temp_d, percent);
			break;
		case SLURMDB_REPORT_TIME_HOURS_PER:
			percent /= total_time;
			percent *= 100;
			temp_d /= 3600;
			output = xstrdup_printf("%.0lf(%.2lf%%)",
						temp_d, percent);
			break;
		default:
			temp_d /= 60;
			output = xstrdup_printf("%.0lf", temp_d);
			break;
		}

		if (print_fields_parsable_print
		   == PRINT_FIELDS_PARSABLE_NO_ENDING
		   && last)
			printf("%s", output);
		else if (print_fields_parsable_print)
			printf("%s|", output);
		else if (field->len == abs_len)
			printf("%*.*s ", abs_len, abs_len, output);
		else
			printf("%-*.*s ", abs_len, abs_len, output);

		xfree(output);
	}
}

extern int parse_option_end(char *option)
{
	int end = 0;

	if (!option)
		return 0;

	while(option[end] && option[end] != '=')
		end++;
	if (!option[end])
		return 0;
	end++;
	return end;
}

/* you need to xfree whatever is sent from here */
extern char *strip_quotes(char *option, int *increased)
{
	int end = 0;
	int i=0, start=0;
	char *meat = NULL;

	if (!option)
		return NULL;

	/* first strip off the ("|')'s */
	if (option[i] == '\"' || option[i] == '\'')
		i++;
	start = i;

	while(option[i]) {
		if (option[i] == '\"' || option[i] == '\'') {
			end++;
			break;
		}
		i++;
	}
	end += i;

	meat = xmalloc((i-start)+1);
	memcpy(meat, option+start, (i-start));

	if (increased)
		(*increased) += end;

	return meat;
}

extern void addto_char_list(List char_list, char *names)
{
	int i=0, start=0;
	char *name = NULL, *tmp_char = NULL;
	ListIterator itr = NULL;

	if (!char_list) {
		error("No list was given to fill in");
		return;
	}

	itr = list_iterator_create(char_list);
	if (names) {
		if (names[i] == '\"' || names[i] == '\'')
			i++;
		start = i;
		while(names[i]) {
			if (names[i] == '\"' || names[i] == '\'')
				break;
			else if (names[i] == ',') {
				if ((i-start) > 0) {
					name = xmalloc((i-start+1));
					memcpy(name, names+start, (i-start));

					while((tmp_char = list_next(itr))) {
						if (!strcasecmp(tmp_char, name))
							break;
					}

					if (!tmp_char)
						list_append(char_list, name);
					else
						xfree(name);
					list_iterator_reset(itr);
				}
				i++;
				start = i;
			}
			i++;
		}
		if ((i-start) > 0) {
			name = xmalloc((i-start)+1);
			memcpy(name, names+start, (i-start));
			while((tmp_char = list_next(itr))) {
				if (!strcasecmp(tmp_char, name))
					break;
			}

			if (!tmp_char)
				list_append(char_list, name);
			else
				xfree(name);
		}
	}
	list_iterator_destroy(itr);
}

/*
 * Comparator used for sorting users largest cpu to smallest cpu
 *
 * returns: 1: user_a > user_b   0: user_a == user_b   -1: user_a < user_b
 *
 */
extern int sort_user_dec(void *v1, void *v2)
{
	slurmdb_report_user_rec_t *user_a;
	slurmdb_report_user_rec_t *user_b;
	int diff;

	user_a = *(slurmdb_report_user_rec_t **)v1;
	user_b = *(slurmdb_report_user_rec_t **)v2;

	if (sort_flag == SLURMDB_REPORT_SORT_TIME) {
		if (user_a->cpu_secs > user_b->cpu_secs)
			return -1;
		else if (user_a->cpu_secs < user_b->cpu_secs)
			return 1;
	}

	if (!user_a->name || !user_b->name)
		return 0;

	diff = strcmp(user_a->name, user_b->name);

	if (diff > 0)
		return 1;
	else if (diff < 0)
		return -1;

	return 0;

}

/*
 * Comparator used for sorting clusters alphabetically
 *
 * returns: 1: cluster_a > cluster_b
 *           0: cluster_a == cluster_b
 *           -1: cluster_a < cluster_b
 *
 */
extern int sort_cluster_dec(void *v1, void *v2)
{
	int diff;
	slurmdb_report_cluster_rec_t *cluster_a;
	slurmdb_report_cluster_rec_t *cluster_b;

	cluster_a = *(slurmdb_report_cluster_rec_t **)v1;
	cluster_b = *(slurmdb_report_cluster_rec_t **)v2;

	if (!cluster_a->name || !cluster_b->name)
		return 0;

	diff = strcmp(cluster_a->name, cluster_b->name);

	if (diff > 0)
		return 1;
	else if (diff < 0)
		return -1;

	return 0;
}

/*
 * Comparator used for sorting assocs alphabetically by acct and then
 * by user.  The association with a total count of time is at the top
 * of the accts.
 *
 * returns: -1: assoc_a > assoc_b
 *           0: assoc_a == assoc_b
 *           1: assoc_a < assoc_b
 *
 */
extern int sort_assoc_dec(void *v1, void *v2)
{
	int diff;
	slurmdb_report_assoc_rec_t *assoc_a;
	slurmdb_report_assoc_rec_t *assoc_b;

	assoc_a = *(slurmdb_report_assoc_rec_t **)v1;
	assoc_b = *(slurmdb_report_assoc_rec_t **)v2;

	if (!assoc_a->acct || !assoc_b->acct)
		return 0;

	diff = strcmp(assoc_a->acct, assoc_b->acct);

	if (diff > 0)
		return 1;
	else if (diff < 0)
		return -1;

	if (!assoc_a->user && assoc_b->user)
		return 1;
	else if (!assoc_b->user)
		return -1;

	diff = strcmp(assoc_a->user, assoc_b->user);

	if (diff > 0)
		return 1;
	else if (diff < 0)
		return -1;


	return 0;
}

/*
 * Comparator used for sorting resvs largest cpu to smallest cpu
 *
 * returns: 1: resv_a > resv_b   0: resv_a == resv_b   -1: resv_a < resv_b
 *
 */
extern int sort_reservations_dec(void *v1, void *v2)
{
	int diff;
	slurmdb_reservation_rec_t *resv_a;
	slurmdb_reservation_rec_t *resv_b;

	resv_a = *(slurmdb_reservation_rec_t **)v1;
	resv_b = *(slurmdb_reservation_rec_t **)v2;

	if (!resv_a->cluster || !resv_b->cluster)
		return 0;

	diff = strcmp(resv_a->cluster, resv_b->cluster);

	if (diff > 0)
		return 1;
	else if (diff < 0)
		return -1;

	if (!resv_a->name || !resv_b->name)
		return 0;

	diff = strcmp(resv_a->name, resv_b->name);

	if (diff > 0)
		return 1;
	else if (diff < 0)
		return -1;

	if (resv_a->time_start < resv_b->time_start)
		return 1;
	else if (resv_a->time_start > resv_b->time_start)
		return -1;

	return 0;
}

extern int get_uint(char *in_value, uint32_t *out_value, char *type)
{
	char *ptr = NULL, *meat = NULL;
	long num;

	if (!(meat = strip_quotes(in_value, NULL))) {
		error("Problem with strip_quotes");
		return SLURM_ERROR;
	}
	num = strtol(meat, &ptr, 10);
	if ((num == 0) && ptr && ptr[0]) {
		error("Invalid value for %s (%s)", type, meat);
		xfree(meat);
		return SLURM_ERROR;
	}
	xfree(meat);

	if (num < 0)
		*out_value = INFINITE;		/* flag to clear */
	else
		*out_value = (uint32_t) num;
	return SLURM_SUCCESS;
}

