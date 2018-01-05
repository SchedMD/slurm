/*****************************************************************************\
 *  common.c - common functions for generating reports
 *             from accounting infrastructure.
 *****************************************************************************
 *  Portions Copyright (C) 2010-2017 SchedMD LLC.
 *  Copyright (C) 2008 Lawrence Livermore National Security.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Danny Auble <da@llnl.gov>
 *  CODE-OCEC-09-009. All rights reserved.
 *
 *  This file is part of SLURM, a resource management program.
 *  For details, see <https://slurm.schedmd.com/>.
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

		switch (time_format) {
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


/* Do not allow the endtime request for sreport to exceed 'now'. */
extern time_t sanity_check_endtime(time_t endtime)
{
	time_t now = time(NULL);

	if (endtime > now)
		endtime = now;

	return endtime;
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

	while (option[i]) {
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
	int i = 0, start = 0;
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
						if (!xstrcasecmp(tmp_char,
								 name))
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
				if (!xstrcasecmp(tmp_char, name))
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
	/* FIXME : this only works for CPUs now */
	int tres_id = TRES_CPU;

	user_a = *(slurmdb_report_user_rec_t **)v1;
	user_b = *(slurmdb_report_user_rec_t **)v2;

	if (sort_flag == SLURMDB_REPORT_SORT_TIME) {
		slurmdb_tres_rec_t *tres_a, *tres_b;

		if (!user_a->tres_list || !user_b->tres_list)
			return 0;

		if (!(tres_a = list_find_first(user_a->tres_list,
					       slurmdb_find_tres_in_list,
					       &tres_id)))
			return 1;

		if (!(tres_b = list_find_first(user_b->tres_list,
					       slurmdb_find_tres_in_list,
					       &tres_id)))
			return -1;


		if (tres_a->alloc_secs > tres_b->alloc_secs)
			return -1;
		else if (tres_a->alloc_secs < tres_b->alloc_secs)
			return 1;
	}

	if (!user_a->name || !user_b->name)
		return 0;

	diff = xstrcmp(user_a->name, user_b->name);

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

	diff = xstrcmp(cluster_a->name, cluster_b->name);

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

	diff = xstrcmp(assoc_a->acct, assoc_b->acct);

	if (diff > 0)
		return 1;
	else if (diff < 0)
		return -1;

	if (!assoc_a->user && assoc_b->user)
		return 1;
	else if (!assoc_b->user)
		return -1;

	diff = xstrcmp(assoc_a->user, assoc_b->user);

	if (diff > 0)
		return 1;
	else if (diff < 0)
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

extern void sreport_set_tres_recs(slurmdb_tres_rec_t **cluster_tres_rec,
				  slurmdb_tres_rec_t **tres_rec,
				  List cluster_tres_list, List tres_list,
				  slurmdb_tres_rec_t *tres_rec_in)
{
	if (!(*cluster_tres_rec = list_find_first(cluster_tres_list,
						  slurmdb_find_tres_in_list,
						  &tres_rec_in->id))) {
		debug2("No cluster TRES %s%s%s(%d)",
		       tres_rec_in->type,
		       tres_rec_in->name ? "/" : "",
		       tres_rec_in->name ? tres_rec_in->name : "",
		       tres_rec_in->id);
	}

	if (!(*tres_rec = list_find_first(tres_list,
					  slurmdb_find_tres_in_list,
					  &tres_rec_in->id))) {
		debug2("No TRES %s%s%s(%d)",
		       tres_rec_in->type,
		       tres_rec_in->name ? "/" : "",
		       tres_rec_in->name ? tres_rec_in->name : "",
		       tres_rec_in->id);
	} else if (!(*tres_rec)->alloc_secs) {
		debug2("No TRES %s%s%s(%d) usage",
		       tres_rec_in->type,
		       tres_rec_in->name ? "/" : "",
		       tres_rec_in->name ? tres_rec_in->name : "",
		       tres_rec_in->id);
	}
}

extern void sreport_set_usage_col_width(print_field_t *field, uint64_t number)
{
	uint64_t order, max_order;

	//info("got %p %"PRIu64, field, number);
	if (!field)
		return;

	order = 100000000;
	max_order = order * 100000000000000000;

	/* smallest usage we want if this changes change order and
	 * max_order appropriately */
	field->len = 8;

	while (order < max_order) {
		if (number < order)
			break;

		field->len++;
		order *= 10;
	}

	if (time_format == SLURMDB_REPORT_TIME_SECS_PER
	    || time_format == SLURMDB_REPORT_TIME_MINS_PER
	    || time_format == SLURMDB_REPORT_TIME_HOURS_PER)
		field->len += 9;
}

extern void sreport_set_usage_column_width(print_field_t *usage_field,
					   print_field_t *energy_field,
					   List slurmdb_report_cluster_list)
{
	uint64_t max_usage = 0, max_energy = 0;
	ListIterator tres_itr, cluster_itr;
	slurmdb_report_cluster_rec_t *slurmdb_report_cluster = NULL;

	xassert(slurmdb_report_cluster_list);

	tres_itr = list_iterator_create(tres_list);
	cluster_itr = list_iterator_create(slurmdb_report_cluster_list);
	while ((slurmdb_report_cluster = list_next(cluster_itr))) {
		slurmdb_tres_rec_t *tres, *tres_rec;
		List use_list = slurmdb_report_cluster->tres_list;

		/* The first association will always be the largest
		 * count of any TRES, so just peek at it.  If the
		 * cluster doesn't have assoications for some reason
		 * use the cluster main one which has the total time.
		 */

		if (slurmdb_report_cluster->assoc_list) {
			slurmdb_report_assoc_rec_t *slurmdb_report =
				list_peek(slurmdb_report_cluster->assoc_list);
			if (slurmdb_report)
				use_list = slurmdb_report->tres_list;
		} else if (slurmdb_report_cluster->user_list) {
			slurmdb_report_user_rec_t *slurmdb_report;
			list_sort(slurmdb_report_cluster->user_list,
				  (ListCmpF)sort_user_dec);
			slurmdb_report =
				list_peek(slurmdb_report_cluster->user_list);
			if (slurmdb_report)
				use_list = slurmdb_report->tres_list;
		} else {
			error("%s: unknown type of slurmdb_report_cluster "
			      "given for cluster %s",
			      __func__, slurmdb_report_cluster->name);
			continue;
		}

		if (energy_field) {
			uint32_t tres_id = TRES_CPU;
			if ((tres_rec = list_find_first(
				     use_list,
				     slurmdb_find_tres_in_list,
				     &tres_id))) {
				max_usage = MAX(max_usage,
						tres_rec->alloc_secs);
			}
			tres_id = TRES_ENERGY;
			if ((tres_rec = list_find_first(
				     use_list,
				     slurmdb_find_tres_in_list,
				     &tres_id))) {
				max_energy = MAX(max_energy,
						 tres_rec->alloc_secs);
			}
		} else {
			list_iterator_reset(tres_itr);
			while ((tres = list_next(tres_itr))) {
				if (tres->id == NO_VAL)
					continue;
				if (!(tres_rec = list_find_first(
					      use_list,
					      slurmdb_find_tres_in_list,
					      &tres->id)))
					continue;
				max_usage = MAX(max_usage,
						tres_rec->alloc_secs);
			}
		}
	}
	list_iterator_destroy(tres_itr);
	list_iterator_destroy(cluster_itr);

	sreport_set_usage_col_width(usage_field, max_usage);
	sreport_set_usage_col_width(energy_field, max_energy);
}

/* Return 1 if UID for two association records match */
static int _match_assoc(void *x, void *key)
{
	slurmdb_report_assoc_rec_t *orig_report_assoc;
	slurmdb_report_assoc_rec_t *dup_report_assoc;

	orig_report_assoc = (slurmdb_report_assoc_rec_t *) key;
	dup_report_assoc  = (slurmdb_report_assoc_rec_t *) x;
	if (!xstrcmp(orig_report_assoc->acct, dup_report_assoc->acct) &&
	    !xstrcmp(orig_report_assoc->user, dup_report_assoc->user))
		return 1;
	return 0;
}

/* Return 1 if a slurmdb associatin record has NULL tres_list */
static int _find_empty_assoc_tres(void *x, void *key)
{
	slurmdb_report_assoc_rec_t *dup_report_assoc;

	dup_report_assoc = (slurmdb_report_assoc_rec_t *) x;
	if (dup_report_assoc->tres_list == NULL)
		return 1;
	return 0;
}

/* For duplicate user/account records, combine TRES records into the original
 * list and purge the duplicate records */
extern void combine_assoc_tres(List first_assoc_list, List new_assoc_list)
{
	slurmdb_report_assoc_rec_t *orig_report_assoc = NULL;
	slurmdb_report_assoc_rec_t *dup_report_assoc = NULL;
	ListIterator iter = NULL;

	if (!first_assoc_list || !new_assoc_list)
		return;

	iter = list_iterator_create(first_assoc_list);
	while ((orig_report_assoc = list_next(iter))) {
		dup_report_assoc = list_find_first(new_assoc_list,
						   _match_assoc,
						   orig_report_assoc);
		if (!dup_report_assoc)
			continue;
		combine_tres_list(orig_report_assoc->tres_list,
				  dup_report_assoc->tres_list);
		FREE_NULL_LIST(dup_report_assoc->tres_list);
	}
	list_iterator_destroy(iter);

	(void) list_delete_all(new_assoc_list, _find_empty_assoc_tres, NULL);
	list_transfer(first_assoc_list, new_assoc_list);
}

/* Return 1 if two TRES records have the same TRES ID */
static int _match_tres_id(void *x, void *key)
{
	slurmdb_tres_rec_t *orig_tres = (slurmdb_tres_rec_t *) key;
	slurmdb_tres_rec_t *dup_tres = (slurmdb_tres_rec_t *) x;

	if (orig_tres->id == dup_tres->id)
		return 1;
	return 0;
}

/* Return 1 if a TRES alloc_secs is zero, the entry has already been merged */
static int _zero_alloc_secs(void *x, void *key)
{
	slurmdb_tres_rec_t *dup_tres;

	dup_tres = (slurmdb_tres_rec_t *) x;
	if (dup_tres->alloc_secs == 0)
		return 1;
	return 0;
}

/* Given two TRES lists, combine the content of the second with the first,
 * adding the counts for duplicate TRES IDs */
extern void combine_tres_list(List orig_tres_list, List dup_tres_list)
{
	slurmdb_tres_rec_t *orig_tres, *dup_tres;
	ListIterator iter = NULL;

	if (!orig_tres_list || !dup_tres_list)
		return;

	/* Merge counts for common TRES */
	iter = list_iterator_create(orig_tres_list);
	while ((orig_tres = list_next(iter))) {
		dup_tres = list_find_first(dup_tres_list, _match_tres_id,
					   orig_tres);
		if (!dup_tres)
			continue;

		orig_tres->alloc_secs += dup_tres->alloc_secs;
		orig_tres->rec_count  += dup_tres->rec_count;
		orig_tres->count      += dup_tres->count;
		dup_tres->alloc_secs  = 0;
	}
	list_iterator_destroy(iter);

	/* Now transfer TRES records that exists only in the duplicate */
	list_delete_all(dup_tres_list, _zero_alloc_secs, NULL);
	list_transfer(orig_tres_list, dup_tres_list);
}


/* Return 1 if a slurmdb user record has NULL tres_list */
static int _find_empty_user_tres(void *x, void *key)
{
	slurmdb_report_user_rec_t *dup_report_user;

	dup_report_user = (slurmdb_report_user_rec_t *) x;
	if (dup_report_user->tres_list == NULL)
		return 1;
	return 0;
}

/* Return 1 if UID for two user records match */
static int _match_user_acct(void *x, void *key)
{
	slurmdb_report_user_rec_t *orig_report_user;
	slurmdb_report_user_rec_t *dup_report_user;

	orig_report_user = (slurmdb_report_user_rec_t *) key;
	dup_report_user  = (slurmdb_report_user_rec_t *) x;
	if ((orig_report_user->uid == dup_report_user->uid) &&
	    !xstrcmp(orig_report_user->acct, dup_report_user->acct))
		return 1;
	return 0;
}

/* For duplicate user/account records, combine TRES records into the original
 * list and purge the duplicate records */
extern void combine_user_tres(List first_user_list, List new_user_list)
{
	slurmdb_report_user_rec_t *orig_report_user = NULL;
	slurmdb_report_user_rec_t *dup_report_user = NULL;
	ListIterator iter = NULL;

	if (!first_user_list || !new_user_list)
		return;

	iter = list_iterator_create(first_user_list);
	while ((orig_report_user = list_next(iter))) {
		dup_report_user = list_find_first(new_user_list,
						  _match_user_acct,
						  orig_report_user);
		if (!dup_report_user)
			continue;
		combine_tres_list(orig_report_user->tres_list,
				  dup_report_user->tres_list);
		FREE_NULL_LIST(dup_report_user->tres_list);
	}
	list_iterator_destroy(iter);

	(void) list_delete_all(new_user_list, _find_empty_user_tres, NULL);
	list_transfer(first_user_list, new_user_list);
}
