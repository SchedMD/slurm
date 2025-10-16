/*****************************************************************************\
 *  metrics plugin interface header
 *****************************************************************************
 *  Copyright (C) SchedMD LLC.
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

#ifndef _INTERFACES_METRICS_H
#define _INTERFACES_METRICS_H

#include "src/interfaces/data_parser.h"
#include "src/slurmctld/statistics.h"

typedef struct metric_keyval {
	char *key;
	char *val;
} metric_keyval_t;

typedef struct metric_set {
	void *arg; /* actual metrics data provided by the specific plugin */
	int plugin_id;
	const char *plugin_type; /* ptr to plugin plugin_type - do not xfree */
} metric_set_t;

typedef struct metric {
	int attr; /* Custom Attributes */
	void *data; /* Data */
	char *desc; /* Metric description */
	void *id; /* Plugin custom identifier of this metric */
	metric_keyval_t **keyval; /* Array of key-values strings, last element
				   * is NULL=NULL */
	char *name; /* Metric name */
	metric_set_t *set; /* Pointer to a metric set */
	data_parser_type_t type; /* Data type */
} metric_t;

extern metric_t *metrics_create_metric(metric_set_t *set,
				       data_parser_type_t type, void *data,
				       ssize_t sz_data, char *name, char *desc,
				       int attr, metric_keyval_t **kv);
extern void metrics_free_metric(metric_t *metric);

extern int metrics_g_init(void);
extern void metrics_g_fini(void);
extern int metrics_g_dump(metric_set_t *set, char **buf);
extern int metrics_g_free_set(metric_set_t *set);
extern metric_set_t *metrics_g_parse_jobs_metrics(jobs_stats_t *s);
extern metric_set_t *metrics_g_parse_nodes_metrics(nodes_stats_t *s);
extern metric_set_t *metrics_g_parse_parts_metrics(partitions_stats_t *s);
extern metric_set_t *metrics_g_parse_sched_metrics(scheduling_stats_t *s);
extern metric_set_t *metrics_g_parse_ua_metrics(users_accts_stats_t *s);
#endif
