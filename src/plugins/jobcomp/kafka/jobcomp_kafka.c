/*****************************************************************************\
 *  jobcomp_kafka.c - Kafka Slurm job completion logging plugin.
 *****************************************************************************
 *  Copyright (C) 2022 SchedMD LLC.
 *  Written by Alejandro Sanchez <alex@schedmd.com>
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

#include "src/common/slurm_xlator.h"
#include "src/common/data.h"
#include "src/common/list.h"
#include "src/common/xstring.h"
#include "src/interfaces/serializer.h"
#include "src/plugins/jobcomp/common/jobcomp_common.h"
#include "src/plugins/jobcomp/kafka/jobcomp_kafka_conf.h"
#include "src/plugins/jobcomp/kafka/jobcomp_kafka_message.h"

/*
 * These variables are required by the generic plugin interface.  If they
 * are not found in the plugin, the plugin loader will ignore it.
 *
 * plugin_name - a string giving a human-readable description of the
 * plugin.  There is no maximum length, but the symbol must refer to
 * a valid string.
 *
 * plugin_type - a string suggesting the type of the plugin or its
 * applicability to a particular form of data or method of data handling.
 * If the low-level plugin API is used, the contents of this string are
 * unimportant and may be anything.  Slurm uses the higher-level plugin
 * interface which requires this string to be of the form
 *
 *	<application>/<method>
 *
 * where <application> is a description of the intended application of
 * the plugin (e.g., "jobcomp" for Slurm job completion logging) and <method>
 * is a description of how this plugin satisfies that application.  Slurm will
 * only load job completion logging plugins if the plugin_type string has a
 * prefix of "jobcomp/".
 *
 * plugin_version - an unsigned 32-bit integer containing the Slurm version
 * (major.minor.micro combined into a single number).
 */
const char plugin_name[]       	= "Job completion logging Kafka plugin";
const char plugin_type[]       	= "jobcomp/kafka";
const uint32_t plugin_version	= SLURM_VERSION_NUMBER;

/*
 * init() is called when the plugin is loaded, before any other functions
 * are called.  Put global initialization here.
 */
extern int init(void)
{
	int rc = SLURM_SUCCESS;

	log_flag(JOBCOMP, "loaded");

	if ((rc = data_init())) {
		error("%s: unable to init data structures: %s",
		      plugin_type, slurm_strerror(rc));
		return rc;
	}

	if ((rc = serializer_g_init(MIME_TYPE_JSON_PLUGIN, NULL))) {
		error("%s: unable to load JSON serializer: %s",
		      plugin_type, slurm_strerror(rc));
		return rc;
	}

	jobcomp_kafka_conf_init();
	jobcomp_kafka_conf_parse_params();
	if ((rc = jobcomp_kafka_conf_parse_location(slurm_conf.job_comp_loc)))
		return rc;

	if ((rc = jobcomp_kafka_message_init()))
		return rc;

	return rc;
}

extern int fini(void)
{
	jobcomp_kafka_message_fini();
	jobcomp_kafka_conf_fini();

	return SLURM_SUCCESS;
}

/*
 * The remainder of this file implements the standard Slurm job completion
 * logging API.
 */
extern int jobcomp_p_set_location(void)
{
	static bool first = true;

	/* This op is coupled to init(), avoid parsing again first time. */
	if (first)
		first = false;
	else
		jobcomp_kafka_conf_parse_params();

	return SLURM_SUCCESS;
}

extern int jobcomp_p_log_record(job_record_t *job_ptr)
{
	int rc = SLURM_SUCCESS;
	char *job_record_serialized = NULL;
	data_t *job_record_data = NULL;

	if (!(job_record_data = jobcomp_common_job_record_to_data(job_ptr))) {
		error("%s: unable to build data_t. %pJ discarded",
		      plugin_type, job_ptr);
		rc = SLURM_ERROR;
		goto end;
	}

	if ((rc = serialize_g_data_to_string(&job_record_serialized,
					     NULL,
					     job_record_data,
					     MIME_TYPE_JSON,
					     SER_FLAGS_COMPACT))) {
		error("%s: %pJ discarded, unable to serialize to JSON: %s",
		      plugin_type, job_ptr, slurm_strerror(rc));
		goto end;
	}

	jobcomp_kafka_message_produce(job_ptr->job_id, job_record_serialized);

end:
	xfree(job_record_serialized);
	FREE_NULL_DATA(job_record_data);
	return rc;
}

extern list_t *jobcomp_p_get_jobs(void *job_cond)
{
	return NULL;
}
