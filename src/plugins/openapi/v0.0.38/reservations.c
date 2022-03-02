/*****************************************************************************\
 *  reservations.c - Slurm REST API reservations http operations handlers
 *****************************************************************************
 *  Copyright (C) 2020 UT-Battelle, LLC.
 *  Written by Matt Ezell <ezellma@ornl.gov>
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

#include "config.h"

#define _GNU_SOURCE

#include <search.h>
#include <stdint.h>
#include <unistd.h>

#include "slurm/slurm.h"

#include "src/common/ref.h"
#include "src/common/xassert.h"
#include "src/common/xmalloc.h"
#include "src/common/xstring.h"

#include "src/slurmrestd/operations.h"

#include "src/plugins/openapi/v0.0.38/api.h"

typedef enum {
	URL_TAG_UNKNOWN = 0,
	URL_TAG_RESERVATION,
	URL_TAG_RESERVATIONS,
} url_tag_t;

typedef struct {
	uint64_t flag;
	char *name;
} res_flags_t;

/* based on strings in reservation_flags_string() */
static const res_flags_t res_flags[] = {
	{ RESERVE_FLAG_MAINT, "MAINT" },
	{ RESERVE_FLAG_NO_MAINT, "NO_MAINT" },
	{ RESERVE_FLAG_FLEX, "FLEX" },
	{ RESERVE_FLAG_OVERLAP, "OVERLAP" },
	{ RESERVE_FLAG_IGN_JOBS, "IGNORE_JOBS" },
	{ RESERVE_FLAG_HOURLY, "HOURLY" },
	{ RESERVE_FLAG_NO_HOURLY, "NO_HOURLY" },
	{ RESERVE_FLAG_DAILY, "DAILY" },
	{ RESERVE_FLAG_NO_DAILY, "NO_DAILY" },
	{ RESERVE_FLAG_WEEKDAY, "WEEKDAY" },
	{ RESERVE_FLAG_WEEKEND, "WEEKEND" },
	{ RESERVE_FLAG_WEEKLY, "WEEKLY" },
	{ RESERVE_FLAG_NO_WEEKLY, "NO_WEEKLY" },
	{ RESERVE_FLAG_SPEC_NODES, "SPEC_NODES" },
	{ RESERVE_FLAG_ALL_NODES, "ALL_NODES" },
	{ RESERVE_FLAG_ANY_NODES, "ANY_NODES" },
	{ RESERVE_FLAG_NO_ANY_NODES, "NO_ANY_NODES" },
	{ RESERVE_FLAG_STATIC, "STATIC" },
	{ RESERVE_FLAG_NO_STATIC, "NO_STATIC" },
	{ RESERVE_FLAG_PART_NODES, "PART_NODES" },
	{ RESERVE_FLAG_NO_PART_NODES, "NO_PART_NODES" },
	{ RESERVE_FLAG_FIRST_CORES, "FIRST_CORES" },
	{ RESERVE_FLAG_TIME_FLOAT, "TIME_FLOAT" },
	{ RESERVE_FLAG_REPLACE, "REPLACE" },
	{ RESERVE_FLAG_REPLACE_DOWN, "REPLACE_DOWN" },
	/* skipping RESERVE_FLAG_PURGE_COMP due to setting */
	{ RESERVE_FLAG_NO_HOLD_JOBS, "NO_HOLD_JOBS_AFTER_END" },
	{ RESERVE_FLAG_MAGNETIC, "MAGNETIC" },
	{ RESERVE_FLAG_NO_MAGNETIC, "NO_MAGNETIC" },
};

static int _dump_res(data_t *p, reserve_info_t *res)
{
	data_t *d = data_set_dict(data_list_append(p));

	data_t *flags = data_set_list(data_key_set(d, "flags"));
	data_set_string(data_key_set(d, "accounts"), res->accounts);
	data_set_string(data_key_set(d, "burst_buffer"), res->burst_buffer);
	data_set_int(data_key_set(d, "core_count"), res->core_cnt);
	data_set_int(data_key_set(d, "core_spec_cnt"), res->core_spec_cnt);
	data_set_int(data_key_set(d, "end_time"), res->end_time);
	data_set_string(data_key_set(d, "features"), res->features);

	for (int i = 0; i < ARRAY_SIZE(res_flags); i++)
		if (res->flags & res_flags[i].flag)
			data_set_string(data_list_append(flags),
					res_flags[i].name);

	data_set_string(data_key_set(d, "groups"), res->groups);
	data_set_string(data_key_set(d, "licenses"), res->licenses);
	data_set_int(data_key_set(d, "max_start_delay"), res->max_start_delay);
	data_set_string(data_key_set(d, "name"), res->name);
	data_set_int(data_key_set(d, "node_count"), res->node_cnt);
	/* skipping node_inx */
	data_set_string(data_key_set(d, "node_list"), res->node_list);
	data_set_string(data_key_set(d, "partition"), res->partition);

	/* purgecomp is a flag with a time setting */
	if (res->flags & RESERVE_FLAG_PURGE_COMP) {
		data_t *pd = data_set_dict(data_key_set(d, "purge_completed"));
		data_set_int(data_key_set(pd, "time"), res->purge_comp_time);
	}

	data_set_int(data_key_set(d, "start_time"), res->start_time);
	data_set_int(data_key_set(d, "watts"), res->resv_watts);
	data_set_string(data_key_set(d, "tres"), res->tres_str);
	data_set_string(data_key_set(d, "users"), res->users);

	return SLURM_SUCCESS;
}

static int _op_handler_reservations(const char *context_id,
				    http_request_method_t method,
				    data_t *parameters, data_t *query, int tag,
				    data_t *d, void *auth)
{
	int rc = SLURM_SUCCESS;
	data_t *errors = populate_response_format(d);
	data_t *reservations = data_set_list(data_key_set(d, "reservations"));
	char *name = NULL;
	reserve_info_msg_t *res_info_ptr = NULL;
	time_t update_time = 0;

	if ((rc = get_date_param(query, "update_time", &update_time)))
		goto done;

	if (tag == URL_TAG_RESERVATION) {
		const data_t *res_name = data_key_get_const(parameters,
							    "reservation_name");
		if (!res_name || data_get_string_converted(res_name, &name) ||
		    !name)
			rc = ESLURM_RESERVATION_INVALID;
	}

	if (!rc)
		rc = slurm_load_reservations(update_time, &res_info_ptr);

	if ((tag == URL_TAG_RESERVATION) &&
	    (!res_info_ptr || (res_info_ptr->record_count == 0)))
		rc = ESLURM_RESERVATION_INVALID;

	if (errno == SLURM_NO_CHANGE_IN_DATA) {
		/* no-op: nothing to do here */
		rc = errno;
		goto done;
	} else if (!rc && res_info_ptr) {
		int found = 0;

		for (int i = 0; !rc && (i < res_info_ptr->record_count); i++) {
			if ((tag == URL_TAG_RESERVATIONS) ||
			    !xstrcasecmp(
				    name,
				    res_info_ptr->reservation_array[i].name)) {
				rc = _dump_res(
					reservations,
					&res_info_ptr->reservation_array[i]);
				found++;
			}
		}

		if (!found && (tag == URL_TAG_RESERVATION))
			rc = ESLURM_RESERVATION_INVALID;
	}

	if (rc) {
		data_t *e = data_set_dict(data_list_append(errors));
		data_set_string(data_key_set(e, "error"), slurm_strerror(rc));
		data_set_int(data_key_set(e, "errno"), rc);
	}

done:
	slurm_free_reservation_info_msg(res_info_ptr);
	xfree(name);
	return rc;
}

extern void init_op_reservations(void)
{
	bind_operation_handler("/slurm/v0.0.38/reservations/",
			       _op_handler_reservations, URL_TAG_RESERVATIONS);
	bind_operation_handler("/slurm/v0.0.38/reservation/{reservation_name}",
			       _op_handler_reservations, URL_TAG_RESERVATION);
}

extern void destroy_op_reservations(void)
{
	unbind_operation_handler(_op_handler_reservations);
}
