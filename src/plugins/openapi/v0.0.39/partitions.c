/*****************************************************************************\
 *  partitions.c - Slurm REST API partitions http operations handlers
 *****************************************************************************
 *  Copyright (C) 2019-2020 SchedMD LLC.
 *  Written by Nathan Rini <nate@schedmd.com>
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

#include "src/common/read_config.h"
#include "src/common/ref.h"
#include "src/common/xassert.h"
#include "src/common/xmalloc.h"
#include "src/common/xstring.h"

#include "src/slurmrestd/operations.h"

#include "src/plugins/openapi/v0.0.39/api.h"

typedef enum {
	URL_TAG_PARTITION = 392,
	URL_TAG_PARTITIONS = 12891
} url_tag_t;

extern int _op_handler_partitions(const char *context_id,
				  http_request_method_t method,
				  data_t *parameters, data_t *query, int tag,
				  data_t *resp, void *auth)
{
	int rc;
	const char *name = NULL;
	partition_info_msg_t *part_info_ptr = NULL;
	time_t update_time = 0;
	ctxt_t *ctxt = init_connection(context_id, method, parameters, query,
				       tag, resp, auth);
	data_t *dpart = data_key_set(resp, "partitions");

	if (ctxt->rc)
		goto done;

	if (method != HTTP_REQUEST_GET) {
		resp_error(ctxt, ESLURM_REST_INVALID_QUERY, __func__,
			   "Unsupported HTTP method requested: %s",
			   get_http_method_string(method));
		goto done;
	}

	if ((rc = get_date_param(query, "update_time", &update_time)))
		goto done;

	if ((tag == URL_TAG_PARTITION) &&
	    !(name = get_str_param("partition_name", ctxt))) {
		resp_error(
			ctxt, ESLURM_REST_INVALID_QUERY, __func__,
			"partition_name must be provided for singular partition query");
		goto done;
	}

	errno = 0;
	if ((rc = slurm_load_partitions(update_time, &part_info_ptr,
					SHOW_ALL))) {
		if ((rc == SLURM_ERROR) && errno)
			rc = errno;

		goto done;
	}

	if (part_info_ptr && name) {
		partition_info_t *parts[2] = { 0 };

		for (int i = 0; !rc && i < part_info_ptr->record_count; i++) {
			if (!xstrcasecmp(name, part_info_ptr->partition_array[i]
						       .name)) {
				parts[0] = &part_info_ptr->partition_array[i];
				break;
			}
		}

		if (!parts[0]) {
			resp_error(ctxt, ESLURM_REST_INVALID_QUERY, __func__,
				   "Unable to find partition %s", name);
		} else {
			partition_info_t **p = parts;
			DATA_DUMP(ctxt->parser, PARTITION_INFO_ARRAY, p, dpart);
		}
	} else {
		DATA_DUMP(ctxt->parser, PARTITION_INFO_MSG, *part_info_ptr,
			  dpart);
	}

done:
	slurm_free_partition_info_msg(part_info_ptr);
	return fini_connection(ctxt);
}

extern void init_op_partitions(void)
{
	bind_operation_handler("/slurm/v0.0.39/partitions/",
			       _op_handler_partitions, URL_TAG_PARTITIONS);
	bind_operation_handler("/slurm/v0.0.39/partition/{partition_name}",
			       _op_handler_partitions, URL_TAG_PARTITION);
}

extern void destroy_op_partitions(void)
{
	unbind_operation_handler(_op_handler_partitions);
}
