/*****************************************************************************\
 *  jobcomp_kafka_conf.c - Parse config helper for jobcomp/kafka.
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

#include <librdkafka/rdkafka.h>

#include "slurm/slurm_errno.h"
#include "src/common/data.h"
#include "src/common/list.h"
#include "src/common/log.h"
#include "src/common/read_config.h"
#include "src/common/xmalloc.h"
#include "src/common/xstring.h"
#include "src/plugins/jobcomp/common/jobcomp_common.h"
#include "src/plugins/jobcomp/kafka/jobcomp_kafka_conf.h"

#define DEFAULT_FLUSH_TIMEOUT 500
#define DEFAULT_POLL_INTERVAL 2

kafka_conf_t *kafka_conf = NULL;
pthread_rwlock_t kafka_conf_rwlock = PTHREAD_RWLOCK_INITIALIZER;
list_t *rd_kafka_conf_list = NULL;

static void _destroy_kafka_conf(void)
{
	slurm_rwlock_wrlock(&kafka_conf_rwlock);
	if (!kafka_conf) {
		slurm_rwlock_unlock(&kafka_conf_rwlock);
		return;
	}

	xfree(kafka_conf->topic);
	xfree(kafka_conf);
	kafka_conf = NULL;
	slurm_rwlock_unlock(&kafka_conf_rwlock);
}

static void _parse_flags(char *flags_str)
{
	kafka_conf->flags = 0;
	if (xstrcasestr(flags_str, "purge_in_flight"))
		kafka_conf->flags |= KAFKA_CONF_FLAG_PURGE_IN_FLIGHT;

	if (xstrcasestr(flags_str, "purge_non_blocking"))
		kafka_conf->flags |= KAFKA_CONF_FLAG_PURGE_NON_BLOCKING;

	if (xstrcasestr(flags_str, "requeue_on_msg_timeout"))
		kafka_conf->flags |= KAFKA_CONF_FLAG_REQUEUE_ON_MSG_TIMEOUT;

	if (xstrcasestr(flags_str, "requeue_purge_in_flight"))
		kafka_conf->flags |= KAFKA_CONF_FLAG_REQUEUE_PURGE_IN_FLIGHT;
}

static bool _parse_key_value_line(char *line, char **key, char **value)
{
	char *ptr;

	/* If there's no '=' there's no key=value pair */
	if (!(ptr = xstrchr(line, '=')))
		return false;

	/* Substitute '=' by '\0' */
	*ptr = '\0';

	 /* Advance ptr to beginning of value */
	ptr++;

	*key = line;
	/* Can't be a comment in key */
	if (xstrchr(*key, '#'))
		return false;

	/* Trim key in both ways */
	xstrtrim(*key);

	/* Value start after '=' */
	*value = ptr;

	/* If there's a comment ignore rest of value */
	if ((ptr = xstrchr(*value, '#')))
		*ptr = '\0';

	/* Trim value in both ways */
	xstrtrim(*value);

	return true;
}

static int _parse_uint32(uint32_t *result, char *key, const char *nptr)
{
	char *endptr = NULL;
	unsigned long conversion;

	/* Reset errno to 0 before call. See strtoul(3) NOTES. */
	errno = 0;
	conversion = strtoul(nptr, &endptr, 0);

	if (!errno && (!*endptr || (endptr != nptr))) {
		*result = (uint32_t) conversion;
		return SLURM_SUCCESS;
	}

	error("%s: invalid %s%s value", plugin_type, key, nptr);

	return SLURM_ERROR;
}

extern void jobcomp_kafka_conf_init(void)
{
	kafka_conf = xmalloc(sizeof(*kafka_conf));
	rd_kafka_conf_list = list_create(destroy_config_key_pair);
}

extern void jobcomp_kafka_conf_fini(void)
{
	FREE_NULL_LIST(rd_kafka_conf_list);
	_destroy_kafka_conf();
}

extern int jobcomp_kafka_conf_parse_location(char *location)
{
	FILE *fp;
	char *line = NULL, *key = NULL, *value = NULL;
	size_t len = 0;
	ssize_t nread;

	xassert(rd_kafka_conf_list);

	if (!(fp = fopen(location, "r"))) {
		error("%s: fopen() failed for file '%s': %m",
		      plugin_type, location);
		return SLURM_ERROR;
	}

	while ((nread = getline(&line, &len, fp)) != -1) {
		if (!_parse_key_value_line(line, &key, &value))
			continue;

		read_config_add_key_pair(rd_kafka_conf_list, key, value);
	}

	free(line);
	fclose(fp);

	return SLURM_SUCCESS;
}

extern void jobcomp_kafka_conf_parse_params(void)
{
	char *begin = NULL, *end = NULL, *start = NULL;
	static char *flush_timeout_key = "flush_timeout=";
	static char *poll_interval_key = "poll_interval=";
	static char *topic_key = "topic=";

	xassert(kafka_conf);

	slurm_rwlock_wrlock(&kafka_conf_rwlock);

	_parse_flags(slurm_conf.job_comp_params);

	if (!(begin = xstrstr(slurm_conf.job_comp_params, flush_timeout_key))) {
		kafka_conf->flush_timeout = DEFAULT_FLUSH_TIMEOUT;
	} else {
		start = begin + strlen(flush_timeout_key);
		kafka_conf->flush_timeout = atoi(start);
	}

	if (!(begin = xstrstr(slurm_conf.job_comp_params, poll_interval_key))) {
		kafka_conf->poll_interval = DEFAULT_POLL_INTERVAL;
	} else {
		start = begin + strlen(poll_interval_key);
		if (!_parse_uint32(&kafka_conf->poll_interval,
				   poll_interval_key, start))
			kafka_conf->poll_interval = DEFAULT_POLL_INTERVAL;
	}

	xfree(kafka_conf->topic);
	if (!(begin = xstrstr(slurm_conf.job_comp_params, topic_key))) {
		kafka_conf->topic = xstrdup(slurm_conf.cluster_name);
	} else {
		start = begin + strlen(topic_key);
		if ((end = xstrstr(start, ",")))
			kafka_conf->topic = xstrndup(start, (end - start));
		else
			kafka_conf->topic = xstrdup(start);
	}

	slurm_rwlock_unlock(&kafka_conf_rwlock);
}
