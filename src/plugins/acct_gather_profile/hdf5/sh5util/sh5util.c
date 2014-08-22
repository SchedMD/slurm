/*****************************************************************************\
 *  sh5util.c - slurm profile accounting plugin for io and energy using hdf5.
 *            - Utility to merge node-step files into a job file
 *            - or extract data from an job file
 *****************************************************************************
 *  Copyright (C) 2013 Bull S. A. S.
 *		Bull, Rue Jean Jaures, B.P.68, 78340, Les Clayes-sous-Bois.
 *
 *  Written by Rod Schultz <rod.schultz@bull.com>
 *
 *  Copyright (C) 2013 SchedMD LLC
 *
 *  Written by Danny Auble <da@schedmd.com>
 *
 *  This file is part of SLURM, a resource management program.
 *  For details, see <http://www.schedmd.com/slurmdocs/>.
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
 *
\*****************************************************************************/

#ifndef _GNU_SOURCE
#  define _GNU_SOURCE
#endif

#ifdef HAVE_CONFIG_H
#  include "config.h"
#endif

#if HAVE_GETOPT_H
#  include <getopt.h>
#else
#  include "src/common/getopt.h"
#endif

#include <dirent.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>

#include "src/common/uid.h"
#include "src/common/read_config.h"
#include "src/common/proc_args.h"
#include "src/common/xstring.h"
#include "../hdf5_api.h"

typedef enum {
	SH5UTIL_MODE_MERGE,
	SH5UTIL_MODE_EXTRACT,
	SH5UTIL_MODE_ITEM_EXTRACT,
} sh5util_mode_t;

typedef struct {
	char *dir;
	int help;
	char *input;
	int job_id;
	bool keepfiles;
	char *level;
	sh5util_mode_t mode;
	char *node;
	char *output;
	char *series;
	char *data_item;
	int step_id;
	char *user;
	int verbose;
} sh5util_opts_t;


static sh5util_opts_t params;
static char **series_names;
static int num_series;

static int _set_options(const int argc, char **argv);
static int _merge_step_files(void);
static int _extract_data(void);
static int _series_data(void);
static int  _check_params(void);
static void _free_options(void);
static void _remove_empty_output(void);

static void _help_msg(void)
{
	printf("\
Usage sh5util [<OPTION>] -j <job[.stepid]>\n"
"\n"
"Valid <OPTION> values are:\n"
" -E, --extract        Extract data series from job file.\n"
"     -i, --input      merged file to extract from (default ./job_$jobid.h5)\n"
"     -N, --node       Node name to extract (default is all)\n"
"     -l, --level      Level to which series is attached\n"
"                      [Node:Totals|Node:TimeSeries] (default Node:Totals)\n"
"     -s, --series     Name of series:\n"
"                      Energy | Lustre | Network | Tasks | Task_#\n"
"                      'Tasks' is all tasks, Task_# is task_id (default is all)\n"
" -I, --item-extract   Extract data item from one series from \n"
"                      all samples on all nodes from thejob file.\n"
"     -i, --input      merged file to extract from (default ./job_$jobid.h5)\n"
"     -s, --series     Name of series:\n"
"                      Energy | Lustre | Network | Task\n"
"     -d, --data       Name of data item in series (see man page) \n"
" -j, --jobs           Format is <job(.step)>. Merge this job/step.\n"
"                      or comma-separated list of job steps. This option is\n"
"                      required.  Not specifying a step will result in all\n"
"                      steps found to be processed.\n"
" -h, --help           Print this description of use.\n"
" -o, --output         Path to a file into which to write.\n"
"                      Default for merge is ./job_$jobid.h5\n"
"                      Default for extract is ./extract_$jobid.csv\n"
" -p, --profiledir     Profile directory location where node-step files exist\n"
"		               default is what is set in acct_gather.conf\n"
" -S, --savefiles      Don't remove node-step files after merging them \n"
" --user               User who profiled job. (Handy for root user, defaults to \n"
"		               user running this command.)\n"
" --usage              Display brief usage message\n");
}

int
main(int argc, char **argv)
{
	int cc;

	cc = _set_options(argc, argv);
	if (cc < 0)
		goto ouch;

	cc = _check_params();
	if (cc < 0)
		goto ouch;

	profile_init();

	switch (params.mode) {

		case SH5UTIL_MODE_MERGE:

			info("Merging node-step files into %s",
			     params.output);
			cc = _merge_step_files();
			if (cc < 0)
				goto ouch;
			break;

		case SH5UTIL_MODE_EXTRACT:

			info("Extracting job data from %s into %s",
			     params.input, params.output);
			cc = _extract_data();
			if (cc < 0)
				goto ouch;
			break;

		case SH5UTIL_MODE_ITEM_EXTRACT:

			info("Extracting '%s' from '%s' data from %s into %s",
			     params.data_item, params.series,
			     params.input, params.output);
			cc = _series_data();
			if (cc < 0)
				goto ouch;
			break;

		default:
			error("Unknown type %d", params.mode);
			break;
	}

	_remove_empty_output();
	profile_fini();
	_free_options();

	return 0;

ouch:
	_remove_empty_output();
	_free_options();

	return -1;
}

/* _free_options()
 */
static void
_free_options(void)
{
	xfree(params.dir);
	xfree(params.input);
	xfree(params.node);
	xfree(params.output);
	xfree(params.series);
	xfree(params.data_item);
	xfree(params.user);
}
/*
 * delete list of strings
 *
 * Parameters
 *	list	- xmalloc'd list of pointers of xmalloc'd strings.
 *	listlen - number of strings in the list
 */
static void _delete_string_list(char **list, int listLen)
{
	int ix;

	if (list == NULL)
		return;

	for (ix = 0; ix < listLen; ix++) {
		xfree(list[ix]);
	}

	xfree(list);

}

static void _remove_empty_output(void)
{
	struct stat sb;

	if (stat(params.output, &sb) == -1) {
		/* Ignore the error as the file may have
		 * not been created yet.
		 */
		return;
	}

	/* Remove the file if 0 size which means
	 * the program failed somewhere along the
	 * way and the file is just left hanging...
	 */
	if (sb.st_size == 0)
		remove(params.output);
}

static void _init_opts(void)
{
	memset(&params, 0, sizeof(sh5util_opts_t));
	params.job_id = -1;
	params.mode = SH5UTIL_MODE_MERGE;
	params.step_id = -1;
}

static int _set_options(const int argc, char **argv)
{
	int option_index = 0;
	int cc;
	log_options_t logopt = LOG_OPTS_STDERR_ONLY;
	char *next_str = NULL;
	uid_t u;

	static struct option long_options[] = {
		{"extract", no_argument, 0, 'E'},
		{"item-extract", no_argument, 0, 'I'},
		{"data", required_argument, 0, 'd'},
		{"help", no_argument, 0, 'h'},
		{"jobs", required_argument, 0, 'j'},
		{"input", required_argument, 0, 'i'},
		{"level", required_argument, 0, 'l'},
		{"node", required_argument, 0, 'N'},
		{"output", required_argument, 0, 'o'},
		{"profiledir", required_argument, 0, 'p'},
		{"series", required_argument, 0, 's'},
		{"savefiles", no_argument, 0, 'S'},
		{"usage", no_argument, 0, 'U'},
		{"user", required_argument, 0, 'u'},
		{"verbose", no_argument, 0, 'v'},
		{"version", no_argument, 0, 'V'},
		{0, 0, 0, 0}};

	log_init(xbasename(argv[0]), logopt, 0, NULL);

	_init_opts();

	while ((cc = getopt_long(argc, argv, "d:Ehi:Ij:l:N:o:p:s:S:u:UvV",
	                         long_options, &option_index)) != EOF) {
		switch (cc) {
			case 'd':
				params.data_item = xstrdup(optarg);
				params.data_item = xstrtolower(params.data_item);
				break;
			case 'E':
				params.mode = SH5UTIL_MODE_EXTRACT;
				break;
			case 'I':
				params.mode = SH5UTIL_MODE_ITEM_EXTRACT;
				break;
			case 'h':
				_help_msg();
				return -1;
				break;
			case 'i':
				params.input = xstrdup(optarg);
				break;
			case 'j':
				params.job_id = strtol(optarg, &next_str, 10);
				if (next_str[0] == '.')
					params.step_id =
						strtol(next_str + 1, NULL, 10);
				break;
			case 'l':
				params.level = xstrdup(optarg);
				break;
			case 'N':
				params.node = xstrdup(optarg);
				break;
			case 'o':
				params.output = xstrdup(optarg);
				break;
			case 'p':
				params.dir = xstrdup(optarg);
				break;
			case 's':
				if (strcmp(optarg, GRP_ENERGY)
				    && strcmp(optarg, GRP_LUSTRE)
				    && strcmp(optarg, GRP_NETWORK)
				    && strncmp(optarg,GRP_TASK,strlen(GRP_TASK))) {
					error("Bad value for --series=\"%s\"", optarg);
					return -1;
				}
				params.series = xstrdup(optarg);
				break;
			case 'S':
				params.keepfiles = 1;
				break;
			case 'u':
				if (uid_from_string(optarg, &u) < 0) {
					error("No such user --uid=\"%s\"", optarg);
					return -1;
				}
				params.user = uid_to_string(u);
				break;
			case 'U':
				_help_msg();
				return -1;
				break;
			case 'v':
				params.verbose++;
				break;
			case 'V':
				print_slurm_version();
				return -1;
				break;
			case ':':
			case '?': /* getopt() has explained it */
				return -1;
		}
	}

	if (params.verbose) {
		logopt.stderr_level += params.verbose;
		log_alter(logopt, SYSLOG_FACILITY_USER, NULL);
	}

	return 0;
}

/* _check_params()
 */
static int
_check_params(void)
{
	if (params.job_id == -1) {
		error("JobID must be specified.");
		return -1;
	}

	if (params.user == NULL)
		params.user = uid_to_string(getuid());

	if (!params.dir)
		acct_gather_profile_g_get(ACCT_GATHER_PROFILE_DIR, &params.dir);

	if (!params.dir) {
		error("Cannot read/parse acct_gather.conf");
		return -1;
	}

	if (params.mode == SH5UTIL_MODE_EXTRACT) {
		if (!params.level)
			params.level = xstrdup("Node:Totals");
		if (!params.input)
			params.input = xstrdup_printf(
				"./job_%d.h5", params.job_id);
		if (!params.output)
			params.output = xstrdup_printf(
				"./extract_%d.csv", params.job_id);

	}
	if (params.mode == SH5UTIL_MODE_ITEM_EXTRACT) {
		if (!params.data_item)
			fatal("Must specify data option --data ");

		if (!params.series)
			fatal("Must specify series option --series");

		if (!params.input)
			params.input = xstrdup_printf("./job_%d.h5", params.job_id);

		if (!params.output)
			params.output = xstrdup_printf("./%s_%s_%d.csv",
			                               params.series,
			                               params.data_item,
			                               params.job_id);
	}

	if (!params.output)
		params.output = xstrdup_printf("./job_%d.h5", params.job_id);

	return 0;
}

/* ============================================================================
 * ============================================================================
 * Functions for merging samples from node step files into a job file
 * ============================================================================
 * ========================================================================= */

static void* _get_all_samples(hid_t gid_series, char* nam_series, uint32_t type,
                              int nsamples)
{
	void*   data = NULL;

	hid_t   id_data_set, dtyp_memory, g_sample, sz_dest;
	herr_t  ec;
	int     smpx ,len;
	void    *data_prior = NULL, *data_cur = NULL;
	char 	name_sample[MAX_GROUP_NAME+1];
	hdf5_api_ops_t* ops;

	ops = profile_factory(type);
	if (ops == NULL) {
		error("Failed to create operations for %s",
		      acct_gather_profile_type_to_string(type));
		return NULL;
	}
	data = (*(ops->init_job_series))(nsamples);
	if (data == NULL) {
		xfree(ops);
		error("Failed to get memory for combined data");
		return NULL;
	}
	dtyp_memory = (*(ops->create_memory_datatype))();
	if (dtyp_memory < 0) {
		xfree(ops);
		xfree(data);
		error("Failed to create %s memory datatype",
		      acct_gather_profile_type_to_string(type));
		return NULL;
	}
	for (smpx=0; smpx<nsamples; smpx++) {
		len = H5Lget_name_by_idx(gid_series, ".", H5_INDEX_NAME,
		                         H5_ITER_INC, smpx, name_sample,
		                         MAX_GROUP_NAME, H5P_DEFAULT);
		if (len<1 || len>MAX_GROUP_NAME) {
			error("Invalid group name %s", name_sample);
			continue;
		}
		g_sample = H5Gopen(gid_series, name_sample, H5P_DEFAULT);
		if (g_sample < 0) {
			info("Failed to open %s", name_sample);
		}
		id_data_set = H5Dopen(g_sample, get_data_set_name(name_sample),
		                      H5P_DEFAULT);
		if (id_data_set < 0) {
			H5Gclose(g_sample);
			error("Failed to open %s dataset",
			      acct_gather_profile_type_to_string(type));
			continue;
		}
		sz_dest = (*(ops->dataset_size))();
		data_cur = xmalloc(sz_dest);
		if (data_cur == NULL) {
			H5Dclose(id_data_set);
			H5Gclose(g_sample);
			error("Failed to get memory for prior data");
			continue;
		}
		ec = H5Dread(id_data_set, dtyp_memory, H5S_ALL, H5S_ALL,
		             H5P_DEFAULT, data_cur);
		if (ec < 0) {
			xfree(data_cur);
			H5Dclose(id_data_set);
			H5Gclose(g_sample);
			error("Failed to read %s data",
			      acct_gather_profile_type_to_string(type));
			continue;
		}
		(*(ops->merge_step_series))(g_sample, data_prior, data_cur,
		                            data+(smpx)*sz_dest);

		xfree(data_prior);
		data_prior = data_cur;
		H5Dclose(id_data_set);
		H5Gclose(g_sample);
	}
	xfree(data_cur);
	H5Tclose(dtyp_memory);
	xfree(ops);

	return data;
}

static void _merge_series_data(hid_t jgid_tasks, hid_t jg_node, hid_t nsg_node)
{
	hid_t   jg_samples, nsg_samples;
	hid_t   g_series, g_series_total = -1;
	hsize_t num_samples, n_series;
	int     idsx, len;
	void    *data = NULL, *series_total = NULL;
	uint32_t type;
	char *data_type;
	char    nam_series[MAX_GROUP_NAME+1];
	hdf5_api_ops_t* ops = NULL;
	H5G_info_t group_info;
	H5O_info_t object_info;

	if (jg_node < 0) {
		info("Job Node is not HDF5 object");
		return;
	}
	if (nsg_node < 0) {
		info("Node-Step is not HDF5 object");
		return;
	}

	jg_samples = H5Gcreate(jg_node, GRP_SAMPLES,
	                       H5P_DEFAULT, H5P_DEFAULT, H5P_DEFAULT);
	if (jg_samples < 0) {
		info("Failed to create job node Samples");
		return;
	}
	nsg_samples = get_group(nsg_node, GRP_SAMPLES);
	if (nsg_samples < 0) {
		H5Gclose(jg_samples);
		debug("Failed to get node-step Samples");
		return;
	}
	H5Gget_info(nsg_samples, &group_info);
	n_series = group_info.nlinks;
	if (n_series < 1) {
		// No series?
		H5Gclose(jg_samples);
		H5Gclose(nsg_samples);
		info("No Samples");
		return;
	}
	for (idsx = 0; idsx < n_series; idsx++) {
		H5Oget_info_by_idx(nsg_samples, ".", H5_INDEX_NAME, H5_ITER_INC,
		                   idsx, &object_info, H5P_DEFAULT);
		if (object_info.type != H5O_TYPE_GROUP)
			continue;

		len = H5Lget_name_by_idx(nsg_samples, ".", H5_INDEX_NAME,
		                         H5_ITER_INC, idsx, nam_series,
		                         MAX_GROUP_NAME, H5P_DEFAULT);
		if (len<1 || len>MAX_GROUP_NAME) {
			info("Invalid group name %s", nam_series);
			continue;
		}
		g_series = H5Gopen(nsg_samples, nam_series, H5P_DEFAULT);
		if (g_series < 0) {
			info("Failed to open %s", nam_series);
			continue;
		}
		H5Gget_info(g_series, &group_info);
		num_samples = group_info.nlinks;
		if (num_samples <= 0) {
			H5Gclose(g_series);
			info("_series %s has no samples", nam_series);
			continue;
		}
		// Get first sample in series to find out how big the data is.
		data_type = get_string_attribute(g_series, ATTR_DATATYPE);
		if (!data_type) {
			H5Gclose(g_series);
			info("Failed to get datatype for Time Series Dataset");
			continue;
		}
		type = acct_gather_profile_type_from_string(data_type);
		xfree(data_type);
		data = _get_all_samples(g_series, nam_series, type,
		                        num_samples);
		if (data == NULL) {
			H5Gclose(g_series);
			info("Failed to get memory for Time Series Dataset");
			continue;
		}
		put_hdf5_data(jg_samples, type, SUBDATA_SERIES, nam_series,
		              data, num_samples);
		ops = profile_factory(type);
		if (ops == NULL) {
			xfree(data);
			H5Gclose(g_series);
			info("Failed to create operations for %s",
			     acct_gather_profile_type_to_string(type));
			continue;
		}
		series_total = (*(ops->series_total))(num_samples, data);
		if (series_total != NULL) {
			// Totals for series attaches to node
			g_series_total = make_group(jg_node, GRP_TOTALS);
			if (g_series_total < 0) {
				H5Gclose(g_series);
				xfree(series_total);
				xfree(data);
				xfree(ops);
				info("Failed to make Totals for Node");
				continue;
			}
			put_hdf5_data(g_series_total, type,
			              SUBDATA_SUMMARY,
			              nam_series, series_total, 1);
			H5Gclose(g_series_total);
		}
		xfree(series_total);
		xfree(ops);
		xfree(data);
		H5Gclose(g_series);
	}

	return;
}

/* ============================================================================
 * Functions for merging tasks data into a job file
 ==========================================================================*/

static void _merge_task_totals(hid_t jg_tasks, hid_t nsg_node, char* node_name)
{
	hid_t   jg_task, jg_totals, nsg_totals,
		g_total, nsg_tasks, nsg_task = -1;
	hsize_t nobj, ntasks = -1;
	int	i, len, taskx, taskid, taskcpus, size_data;
	void    *data;
	uint32_t type;
	char    buf[MAX_GROUP_NAME+1];
	char    group_name[MAX_GROUP_NAME+1];
	H5G_info_t group_info;

	if (jg_tasks < 0) {
		info("Job Tasks is not HDF5 object");
		return;
	}
	if (nsg_node < 0) {
		info("Node-Step is not HDF5 object");
		return;
	}

	nsg_tasks = get_group(nsg_node, GRP_TASKS);
	if (nsg_tasks < 0) {
		debug("No Tasks group in node-step file");
		return;
	}

	H5Gget_info(nsg_tasks, &group_info);
	ntasks = group_info.nlinks;
	for (taskx = 0; ((int)ntasks>0) && (taskx<((int)ntasks)); taskx++) {
		// Get the name of the group.
		len = H5Lget_name_by_idx(nsg_tasks, ".", H5_INDEX_NAME,
		                         H5_ITER_INC, taskx, buf,
		                         MAX_GROUP_NAME, H5P_DEFAULT);
		if (len<1 || len>MAX_GROUP_NAME) {
			info("Invalid group name %s", buf);
			continue;
		}
		nsg_task = H5Gopen(nsg_tasks, buf, H5P_DEFAULT);
		if (nsg_task < 0) {
			debug("Failed to open %s", buf);
			continue;
		}
		taskid = get_int_attribute(nsg_task, ATTR_TASKID);
		sprintf(group_name, "%s_%d", GRP_TASK, taskid);
		jg_task = H5Gcreate(jg_tasks, group_name,
		                    H5P_DEFAULT, H5P_DEFAULT, H5P_DEFAULT);
		if (jg_task < 0) {
			H5Gclose(nsg_task);
			info("Failed to create job task group");
			continue;
		}
		put_string_attribute(jg_task, ATTR_NODENAME, node_name);
		put_int_attribute(jg_task, ATTR_TASKID, taskid);
		taskcpus = get_int_attribute(nsg_task, ATTR_CPUPERTASK);
		put_int_attribute(jg_task, ATTR_CPUPERTASK, taskcpus);
		nsg_totals = get_group(nsg_task, GRP_TOTALS);
		if (nsg_totals < 0) {
			H5Gclose(jg_task);
			H5Gclose(nsg_task);
			continue;
		}
		jg_totals = H5Gcreate(jg_task, GRP_TOTALS,
		                      H5P_DEFAULT, H5P_DEFAULT, H5P_DEFAULT);
		if (jg_totals < 0) {
			H5Gclose(jg_task);
			H5Gclose(nsg_task);
			info("Failed to create job task totals");
			continue;
		}
		H5Gget_info(nsg_totals, &group_info);
		nobj = group_info.nlinks;
		for (i = 0; (nobj>0) && (i<nobj); i++) {
			// Get the name of the group.
			len = H5Lget_name_by_idx(nsg_totals, ".", H5_INDEX_NAME,
			                         H5_ITER_INC, i, buf,
			                         MAX_GROUP_NAME, H5P_DEFAULT);

			if (len<1 || len>MAX_GROUP_NAME) {
				info("Invalid group name %s", buf);
				continue;
			}
			g_total = H5Gopen(nsg_totals, buf, H5P_DEFAULT);
			if (g_total < 0) {
				info("Failed to open %s", buf);
				continue;
			}
			type = get_uint32_attribute(g_total, ATTR_DATATYPE);
			if (!type) {
				H5Gclose(g_total);
				info("No %s attribute", ATTR_DATATYPE);
				continue;
			}
			data = get_hdf5_data(g_total, type, buf, &size_data);
			if (data == NULL) {
				H5Gclose(g_total);
				info("Failed to get group %s type %s data", buf,
				     acct_gather_profile_type_to_string(type));
				continue;
			}
			put_hdf5_data(jg_totals, type, SUBDATA_DATA,
			              buf, data, 1);
			xfree(data);
			H5Gclose(g_total);
		}
		H5Gclose(nsg_totals);
		H5Gclose(nsg_task);
		H5Gclose(jg_totals);
		H5Gclose(jg_task);
	}
	H5Gclose(nsg_tasks);
}

/* ============================================================================
 * Functions for merging node totals into a job file
 ==========================================================================*/

static void _merge_node_totals(hid_t jg_node, hid_t nsg_node)
{
	hid_t	jg_totals, nsg_totals, g_total;
	hsize_t nobj;
	int 	i, len, size_data;
	void  	*data;
	uint32_t type;
	char 	buf[MAX_GROUP_NAME+1];
	H5G_info_t group_info;

	if (jg_node < 0) {
		info("Job Node is not HDF5 object");
		return;
	}
	if (nsg_node < 0) {
		info("Node-Step is not HDF5 object");
		return;
	}
	jg_totals = H5Gcreate(jg_node, GRP_TOTALS,
	                      H5P_DEFAULT, H5P_DEFAULT, H5P_DEFAULT);
	if (jg_totals < 0) {
		info("Failed to create job node totals");
		return;
	}
	nsg_totals = get_group(nsg_node, GRP_TOTALS);
	if (nsg_totals < 0) {
		H5Gclose(jg_totals);
		return;
	}

	H5Gget_info(nsg_totals, &group_info);
	nobj = group_info.nlinks;
	for (i = 0; (nobj>0) && (i<nobj); i++) {
		// Get the name of the group.
		len = H5Lget_name_by_idx(nsg_totals, ".", H5_INDEX_NAME,
		                         H5_ITER_INC, i, buf,
		                         MAX_GROUP_NAME, H5P_DEFAULT);
		if (len<1 || len>MAX_GROUP_NAME) {
			info("invalid group name %s", buf);
			continue;
		}
		g_total = H5Gopen(nsg_totals, buf, H5P_DEFAULT);
		if (g_total < 0) {
			info("Failed to open %s", buf);
			continue;
		}
		type = get_uint32_attribute(g_total, ATTR_DATATYPE);
		if (!type) {
			H5Gclose(g_total);
			info("No %s attribute", ATTR_DATATYPE);
			continue;
		}
		data = get_hdf5_data(g_total, type, buf, &size_data);
		if (data == NULL) {
			H5Gclose(g_total);
			info("Failed to get group %s type %s data",
			     buf, acct_gather_profile_type_to_string(type));
			continue;
		}
		put_hdf5_data(jg_totals, type, SUBDATA_DATA, buf, data, 1);
		xfree(data);
		H5Gclose(g_total);
	}
	H5Gclose(nsg_totals);
	H5Gclose(jg_totals);
	return;
}

/* ============================================================================
 * Functions for merging step data into a job file
 ==========================================================================*/

static void _merge_node_step_data(hid_t fid_job, char* file_name, int nodeIndex,
                                  char* node_name, hid_t jgid_nodes,
                                  hid_t jgid_tasks)
{
	hid_t	fid_nodestep, jgid_node, nsgid_root, nsgid_node;
	char	*start_time;
	char	group_name[MAX_GROUP_NAME+1];

	jgid_node = H5Gcreate(jgid_nodes, node_name,
	                      H5P_DEFAULT, H5P_DEFAULT, H5P_DEFAULT);
	if (jgid_node < 0) {
		error("Failed to create group %s",node_name);
		return;
	}
	put_string_attribute(jgid_node, ATTR_NODENAME, node_name);
	// Process node step file
	// Open the file and the node group.
	fid_nodestep = H5Fopen(file_name, H5F_ACC_RDONLY, H5P_DEFAULT);
	if (fid_nodestep < 0) {
		H5Gclose(jgid_node);
		error("Failed to open %s",file_name);
		return;
	}
	nsgid_root = H5Gopen(fid_nodestep,"/", H5P_DEFAULT);
	sprintf(group_name, "/%s_%s", GRP_NODE, node_name);
	nsgid_node = H5Gopen(nsgid_root, group_name, H5P_DEFAULT);
	if (nsgid_node < 0) {
		H5Gclose(fid_nodestep);
		H5Gclose(jgid_node);
		error("Failed to open node group");
		return;;
	}
	start_time = get_string_attribute(nsgid_node,ATTR_STARTTIME);
	if (start_time == NULL) {
		info("No %s attribute", ATTR_STARTTIME);
	} else {
		put_string_attribute(jgid_node, ATTR_STARTTIME, start_time);
		xfree(start_time);
	}
	_merge_node_totals(jgid_node, nsgid_node);
	_merge_task_totals(jgid_tasks, nsgid_node, node_name);
	_merge_series_data(jgid_tasks, jgid_node, nsgid_node);
	H5Gclose(nsgid_node);
	H5Fclose(fid_nodestep);
	H5Gclose(jgid_node);

	if (!params.keepfiles)
		remove(file_name);

	return;
}

static int _merge_step_files(void)
{
	hid_t fid_job = -1;
	hid_t jgid_step = -1;
	hid_t jgid_nodes = -1;
	hid_t jgid_tasks = -1;
	DIR *dir;
	struct  dirent *de;
	char file_name[MAX_PROFILE_PATH+1];
	char step_dir[MAX_PROFILE_PATH+1];
	char step_path[MAX_PROFILE_PATH+1];
	char jgrp_step_name[MAX_GROUP_NAME+1];
	char jgrp_nodes_name[MAX_GROUP_NAME+1];
	char jgrp_tasks_name[MAX_GROUP_NAME+1];
	char *step_node;
	char *pos_char;
	char *stepno;
	int	stepx = 0;
	int num_steps = 0;
	int nodex = -1;
	int max_step = -1;
	int	jobid, stepid;
	bool found_files = false;

	sprintf(step_dir, "%s/%s", params.dir, params.user);

	while (max_step == -1 || stepx <= max_step) {

		if (!(dir = opendir(step_dir))) {
			error("Cannot open %s job profile directory: %m", step_dir);
			return -1;
		}

		nodex = 0;
		while ((de = readdir(dir))) {

			strcpy(file_name, de->d_name);
			if (file_name[0] == '.')
				continue;

			pos_char = strstr(file_name,".h5");
			if (!pos_char)
				continue;
			*pos_char = 0;

			pos_char = strchr(file_name,'_');
			if (!pos_char)
				continue;
			*pos_char = 0;

			jobid = strtol(file_name, NULL, 10);
			if (jobid != params.job_id)
				continue;

			stepno = pos_char + 1;
			pos_char = strchr(stepno,'_');
			if (!pos_char) {
				continue;
			}
			*pos_char = 0;

			stepid = strtol(stepno, NULL, 10);
			if (stepid > max_step)
				max_step = stepid;
			if (stepid != stepx)
				continue;

			step_node = pos_char + 1;

			if (!found_files) {
				fid_job = H5Fcreate(params.output,
				                    H5F_ACC_TRUNC,
				                    H5P_DEFAULT,
				                    H5P_DEFAULT);
				if (fid_job < 0) {
					error("Failed create HDF5 file %s", params.output);
					return -1;
				}
				found_files = true;
			}

			if (nodex == 0) {

				num_steps++;
				sprintf(jgrp_step_name, "/%s_%d", GRP_STEP,
				        stepx);

				jgid_step = make_group(fid_job, jgrp_step_name);
				if (jgid_step < 0) {
					error("Failed to create %s", jgrp_step_name);
					continue;
				}

				sprintf(jgrp_nodes_name,"%s/%s",
				        jgrp_step_name,
				        GRP_NODES);
				jgid_nodes = make_group(jgid_step,
				                        jgrp_nodes_name);
				if (jgid_nodes < 0) {
					error("Failed to create %s", jgrp_nodes_name);
					continue;
				}

				sprintf(jgrp_tasks_name,"%s/%s",
				        jgrp_step_name,
				        GRP_TASKS);
				jgid_tasks = make_group(jgid_step,
				                        jgrp_tasks_name);
				if (jgid_tasks < 0) {
					error("Failed to create %s", jgrp_tasks_name);
					continue;
				}
			}

			sprintf(step_path, "%s/%s", step_dir, de->d_name);
			debug("Adding %s to the job file", step_path);
			_merge_node_step_data(fid_job, step_path,
			                      nodex, step_node,
			                      jgid_nodes, jgid_tasks);
			nodex++;
		}

		closedir(dir);

		if (nodex > 0) {
			put_int_attribute(jgid_step, ATTR_NNODES, nodex);
			H5Gclose(jgid_tasks);
			H5Gclose(jgid_nodes);
			H5Gclose(jgid_step);
		}

		/* If we did not find the step 0
		 * bail out.
		 */
		if (stepx == 0
			&& !found_files)
			break;

		stepx++;
	}

	if (!found_files)
		info("No node-step files found for jobid %d", params.job_id);
	else
		put_int_attribute(fid_job, ATTR_NSTEPS, num_steps);

	if (fid_job != -1)
		H5Fclose(fid_job);

	return 0;
}

/* ============================================================================
 * ============================================================================
 * Functions for data extraction
 * ============================================================================
 * ========================================================================= */

static hid_t _get_series_parent(hid_t group)
{
	hid_t gid_level = -1;

	if (strcasecmp(params.level, "Node:Totals") == 0) {
		gid_level = get_group(group, GRP_TOTALS);
		if (gid_level < 0) {
			info("Failed to open  group %s", GRP_TOTALS);
		}
	} else if (strcasecmp(params.level, "Node:TimeSeries") == 0) {
		gid_level = get_group(group, GRP_SAMPLES);
		if (gid_level < 0) {
			info("Failed to open group %s", GRP_SAMPLES);
		}
	} else {
		info("%s is an illegal level", params.level);
		return -1;

	}

	return gid_level;
}


static void _get_series_names(hid_t group)
{
	int i, len;
	char buf[MAX_GROUP_NAME+1];
	H5G_info_t group_info;

	H5Gget_info(group, &group_info);
	num_series = (int)group_info.nlinks;
	if (num_series < 0) {
		debug("No Data Series in group");
		return;
	}
	series_names = xmalloc(sizeof(char*)*num_series);
	for (i = 0; (num_series>0) && (i<num_series); i++) {
		len = H5Lget_name_by_idx(group, ".", H5_INDEX_NAME,
		                         H5_ITER_INC, i, buf,
		                         MAX_GROUP_NAME, H5P_DEFAULT);
		if ((len < 0) || (len > MAX_GROUP_NAME)) {
			info("Invalid series name=%s", buf);
			// put into list anyway so list doesn't have a null.
		}
		series_names[i] = xstrdup(buf);
	}

}

static void _extract_series(FILE* fp, int stepx, bool header, hid_t gid_level,
			    char* node_name, char* data_set_name) {
	hid_t	gid_series;
	int 	size_data;
	void	*data;
	uint32_t type;
	char	*data_type, *subtype;
	hdf5_api_ops_t* ops;
	gid_series = get_group(gid_level, data_set_name);
	if (gid_series < 0) {
		// This is okay, may not have ran long enough for
		// a sample (hostname????)
		// OR trying to get all tasks
		return;
	}
	data_type = get_string_attribute(gid_series, ATTR_DATATYPE);
	if (!data_type) {
		H5Gclose(gid_series);
		info("No datatype in %s", data_set_name);
		return;
	}
	type = acct_gather_profile_type_from_string(data_type);
	xfree(data_type);
	subtype = get_string_attribute(gid_series, ATTR_SUBDATATYPE);
	if (subtype == NULL) {
		H5Gclose(gid_series);
		info("No %s attribute", ATTR_SUBDATATYPE);
		return;
	}
	ops = profile_factory(type);
	if (ops == NULL) {
		xfree(subtype);
		H5Gclose(gid_series);
		info("Failed to create operations for %s",
		     acct_gather_profile_type_to_string(type));
		return;
	}
	data = get_hdf5_data(
		gid_series, type, data_set_name, &size_data);
	if (data) {
		if (strcmp(subtype,SUBDATA_SUMMARY) != 0)
			(*(ops->extract_series)) (fp, header, params.job_id,
				 stepx, node_name, data_set_name,
				 data, size_data);
		else
			(*(ops->extract_total)) (fp, header, params.job_id,
				 stepx, node_name, data_set_name,
				 data, size_data);
		xfree(data);
	} else {
		fprintf(fp, "%d,%d,%s,No %s Data\n",
		        params.job_id, stepx, node_name,
		        data_set_name);
	}
	xfree(ops);
	H5Gclose(gid_series);

}
static void _extract_node_level(FILE* fp, int stepx, hid_t jgid_nodes,
                                int nnodes, char* data_set_name)
{

	hid_t	jgid_node, gid_level;
	int 	nodex, len;
	char    jgrp_node_name[MAX_GROUP_NAME+1];
	bool header = true;
	for (nodex=0; nodex<nnodes; nodex++) {
		len = H5Lget_name_by_idx(jgid_nodes, ".", H5_INDEX_NAME,
		                         H5_ITER_INC, nodex, jgrp_node_name,
		                         MAX_GROUP_NAME, H5P_DEFAULT);
		if ((len < 0) || (len > MAX_GROUP_NAME)) {
			info("Invalid node name=%s", jgrp_node_name);
			continue;
		}
		jgid_node = get_group(jgid_nodes, jgrp_node_name);
		if (jgid_node < 0) {
			info("Failed to open group %s", jgrp_node_name);
			continue;
		}
		if (params.node
		    && strcmp(params.node, "*")
		    && strcmp(params.node, jgrp_node_name))
			continue;
		gid_level = _get_series_parent(jgid_node);
		if (gid_level == -1) {
			H5Gclose(jgid_node);
			continue;
		}
		_extract_series(fp, stepx, header, gid_level, jgrp_node_name,
				data_set_name);
		header = false;
		H5Gclose(gid_level);
		H5Gclose(jgid_node);
	}
}

static void _extract_all_tasks(FILE *fp, hid_t gid_step, hid_t gid_nodes,
		int nnodes, int stepx)
{

	hid_t	gid_tasks, gid_task = 0, gid_node = -1, gid_level = -1;
	H5G_info_t group_info;
	int	ntasks, itx, len, task_id;
	char	task_name[MAX_GROUP_NAME+1];
	char*   node_name;
	char	buf[MAX_GROUP_NAME+1];
	bool hd = true;

	gid_tasks = get_group(gid_step, GRP_TASKS);
	if (gid_tasks < 0)
		fatal("No tasks in step %d", stepx);
	H5Gget_info(gid_tasks, &group_info);
	ntasks = (int) group_info.nlinks;
	if (ntasks <= 0)
		fatal("No tasks in step %d", stepx);

	for (itx = 0; itx<ntasks; itx++) {
		// Get the name of the group.
		len = H5Lget_name_by_idx(gid_tasks, ".", H5_INDEX_NAME,
		                         H5_ITER_INC, itx, buf, MAX_GROUP_NAME,
		                         H5P_DEFAULT);
		if ((len > 0) && (len < MAX_GROUP_NAME)) {
			gid_task = H5Gopen(gid_tasks, buf, H5P_DEFAULT);
			if (gid_task < 0)
				fatal("Failed to open %s", buf);
		} else
			fatal("Illegal task name %s",buf);
		task_id = get_int_attribute(gid_task, ATTR_TASKID);
		node_name = get_string_attribute(gid_task, ATTR_NODENAME);
		sprintf(task_name,"%s_%d", GRP_TASK, task_id);
		gid_node = H5Gopen(gid_nodes, node_name, H5P_DEFAULT);
		if (gid_node < 0)
			fatal("Failed to open %s for Task_%d",
					node_name, task_id);
		gid_level = get_group(gid_node, GRP_SAMPLES);
		if (gid_level < 0)
			fatal("Failed to open group %s for node=%s task=%d",
					GRP_SAMPLES,node_name, task_id);
		_extract_series(fp, stepx, hd, gid_level, node_name, task_name);

		hd = false;
		xfree(node_name);
		H5Gclose(gid_level);
		H5Gclose(gid_node);
		H5Gclose(gid_task);
	}
	H5Gclose(gid_tasks);
}

/* _extract_data()
 */
static int _extract_data(void)
{
	hid_t fid_job;
	hid_t jgid_root;
	hid_t jgid_step;
	hid_t jgid_nodes;
	hid_t jgid_node;
	hid_t jgid_level;
	int	nsteps;
	int nnodes;
	int stepx;
	int isx;
	int len;
	char jgrp_step_name[MAX_GROUP_NAME+1];
	char jgrp_node_name[MAX_GROUP_NAME+1];
	FILE *fp;

	fp = fopen(params.output, "w");
	if (fp == NULL) {
		error("Failed to create output file %s -- %m",
		      params.output);
	}

	fid_job = H5Fopen(params.input, H5F_ACC_RDONLY, H5P_DEFAULT);
	if (fid_job < 0) {
		error("Failed to open %s", params.input);
		return -1;
	}

	jgid_root = H5Gopen(fid_job, "/", H5P_DEFAULT);
	if (jgid_root < 0) {
		H5Fclose(fid_job);
		error("Failed to open  root");
		return -1;
	}

	nsteps = get_int_attribute(jgid_root, ATTR_NSTEPS);
	for (stepx = 0; stepx < nsteps; stepx++) {

		if ((params.step_id != -1) && (stepx != params.step_id))
			continue;

		sprintf(jgrp_step_name, "%s_%d", GRP_STEP, stepx);
		jgid_step = get_group(jgid_root, jgrp_step_name);
		if (jgid_step < 0) {
			error("Failed to open group %s", jgrp_step_name);
			continue;
		}

		if (params.level && !strncasecmp(params.level, "Node:", 5)) {

			nnodes = get_int_attribute(jgid_step, ATTR_NNODES);

			jgid_nodes = get_group(jgid_step, GRP_NODES);
			if (jgid_nodes < 0) {
				H5Gclose(jgid_step);
				error("Failed to open  group %s", GRP_NODES);
				continue;
			}

			len = H5Lget_name_by_idx(jgid_nodes, ".", H5_INDEX_NAME,
			                         H5_ITER_INC, 0, jgrp_node_name,
			                         MAX_GROUP_NAME, H5P_DEFAULT);
			if ((len < 0) || (len > MAX_GROUP_NAME)) {
				H5Gclose(jgid_nodes);
				H5Gclose(jgid_step);
				error("Invalid node name %s", jgrp_node_name);
				continue;
			}

			jgid_node = get_group(jgid_nodes, jgrp_node_name);
			if (jgid_node < 0) {
				H5Gclose(jgid_nodes);
				H5Gclose(jgid_step);
				info("Failed to open group %s", jgrp_node_name);
				continue;
			}

			jgid_level = _get_series_parent(jgid_node);
			if (jgid_level == -1) {
				H5Gclose(jgid_node);
				H5Gclose(jgid_nodes);
				H5Gclose(jgid_step);
				continue;
			}

			_get_series_names(jgid_level);
			H5Gclose(jgid_level);
			H5Gclose(jgid_node);

			if (!params.series || !strcmp(params.series, "*")) {
				for (isx = 0; isx < num_series; isx++) {
					if (strncasecmp(series_names[isx],
							GRP_TASK,
							strlen(GRP_TASK)) == 0)
						continue;
					_extract_node_level(fp, stepx, jgid_nodes,
					                    nnodes,
					                    series_names[isx]);
					// Now handle all tasks.
				}
			} else if (strcasecmp(params.series, GRP_TASKS) == 0
			           || strcasecmp(params.series, GRP_TASK) == 0) {
				for (isx = 0; isx < num_series; isx++) {
					if (strstr(series_names[isx],
					           GRP_TASK)) {
						_extract_node_level(fp, stepx, jgid_nodes,
						                    nnodes,
						                    series_names[isx]);
					}
				}
			} else {
				_extract_node_level(fp, stepx, jgid_nodes,
				                    nnodes,
				                    params.series);
			}

			_delete_string_list(series_names, num_series);
			series_names = NULL;
			num_series = 0;
			if (!params.series || !strcmp(params.series, "*"))
				_extract_all_tasks(fp, jgid_step, jgid_nodes,
						nnodes, stepx);

			H5Gclose(jgid_nodes);
		} else {
			error("%s is an illegal level", params.level);
		}
		H5Gclose(jgid_step);
	}

	H5Gclose(jgid_root);
	H5Fclose(fid_job);
	fclose(fp);

	return 0;
}


/* ============================================================================
 * ============================================================================
 * Functions for data item extraction
 * ============================================================================
 * ========================================================================= */

// Get the data_set for a node
static void *_get_series_data(hid_t jgid_node, char* series,
                              hdf5_api_ops_t **ops_p, int *nsmp)
{

	hid_t	gid_level, gid_series;
	int 	size_data;
	void	*data;
	uint32_t type;
	char	*data_type;
	hdf5_api_ops_t* ops;

	*nsmp = 0;	// Initialize return arguments.
	*ops_p = NULL;

	// Navigate from the node group to the data set
	gid_level = get_group(jgid_node, GRP_SAMPLES);
	if (gid_level == -1) {
		return NULL;
	}
	gid_series = get_group(gid_level, series);
	if (gid_series < 0) {
		// This is okay, may not have ran long enough for
		// a sample (srun hostname)
		H5Gclose(gid_level);
		return NULL;
	}
	data_type = get_string_attribute(gid_series, ATTR_DATATYPE);
	if (!data_type) {
		H5Gclose(gid_series);
		H5Gclose(gid_level);
		debug("No datatype in %s", series);
		return NULL;
	}
	// Invoke the data type operator to get the data set
	type = acct_gather_profile_type_from_string(data_type);
	xfree(data_type);
	ops = profile_factory(type);
	if (ops == NULL) {
		H5Gclose(gid_series);
		H5Gclose(gid_level);
		debug("Failed to create operations for %s",
		      acct_gather_profile_type_to_string(type));
		return NULL;
	}
	data = get_hdf5_data(gid_series, type, series, &size_data);
	if (data) {
		*nsmp = (size_data / ops->dataset_size());
		*ops_p = ops;
	} else {
		xfree(ops);
	}
	H5Gclose(gid_series);
	H5Gclose(gid_level);
	return data;
}

static void _series_analysis(FILE *fp, bool hd, int stepx, int nseries,
                             int nsmp, char **series_name, char **tod, double *et,
                             double **all_series, uint64_t *series_smp)
{
	double *mn_series;	// Min Value, each sample
	double *mx_series;	// Max value, each sample
	double *sum_series;	// Total of all series, each sample
	double *smp_series;	// all samples for one node
	uint64_t *mn_sx;	// Index of series with minimum value
	uint64_t *mx_sx;   	// Index of series with maximum value
	uint64_t *series_in_smp; // Number of series in the sample
	int max_smpx = 0;
	double max_smp_series = 0;
	double ave_series;
	int ix, isx;

	mn_series = xmalloc(nsmp * sizeof(double));
	mx_series = xmalloc(nsmp * sizeof(double));
	sum_series =xmalloc(nsmp * sizeof(double));
	mn_sx = xmalloc(nsmp * sizeof(uint64_t));
	mx_sx = xmalloc(nsmp * sizeof(uint64_t));
	series_in_smp = xmalloc(nsmp * sizeof(uint64_t));

	for (ix = 0; ix < nsmp; ix++) {
		for (isx=0; isx<nseries; isx++) {
			if (series_smp[isx]<nsmp && ix>=series_smp[isx])
				continue;
			series_in_smp[ix]++;
			smp_series = all_series[isx];
			if (smp_series) {
				sum_series[ix] += smp_series[ix];
				if (mn_series[ix] == 0
				    || smp_series[ix] < mn_series[ix]) {
					mn_series[ix] = smp_series[ix];
					mn_sx[ix] = isx;
				}
				if (mx_series[ix] == 0
				    || smp_series[ix] > mx_series[ix]) {
					mx_series[ix] = smp_series[ix];
					mx_sx[ix] = isx;
				}
			}
		}
	}

	for (ix = 0; ix < nsmp; ix++) {
		if (sum_series[ix] > max_smp_series) {
			max_smpx = ix;
			max_smp_series = sum_series[ix];
		}
	}

	ave_series = sum_series[max_smpx] / series_in_smp[max_smpx];
	printf("    Step %d Maximum accumulated %s Value (%f) occurred "
	       "at %s (Elapsed Time=%d) Ave Node %f\n",
	       stepx, params.data_item, max_smp_series,
	       tod[max_smpx], (int) et[max_smpx], ave_series);

	// Put data for step
	if (!hd) {
		fprintf(fp,"TOD,Et,JobId,StepId,Min Node,Min %s,"
		        "Ave %s,Max Node,Max %s,Total %s,"
		        "Num Nodes",params.data_item,params.data_item,
		        params.data_item,params.data_item);
		for (isx = 0; isx < nseries; isx++) {
			fprintf(fp,",%s",series_name[isx]);
		}
		fprintf(fp,"\n");
	}

	for (ix = 0; ix < nsmp; ix++) {
		fprintf(fp,"%s, %d",tod[ix], (int) et[ix]);
		fprintf(fp,",%d,%d",params.job_id,stepx);
		fprintf(fp,",%s,%f",series_name[mn_sx[ix]],
		        mn_series[ix]);
		ave_series = sum_series[ix] / series_in_smp[ix];
		fprintf(fp,",%f",ave_series);
		fprintf(fp,",%s,%f",series_name[mx_sx[ix]],
		        mx_series[ix]);
		fprintf(fp,",%f",sum_series[ix]);
		fprintf(fp,",%"PRIu64"",series_in_smp[ix]);
		for (isx = 0; isx < nseries; isx++) {
			if (series_smp[isx]<nsmp && ix>=series_smp[isx]) {
				fprintf(fp,",0.0");
			} else {
				smp_series = all_series[isx];
				fprintf(fp,",%f",smp_series[ix]);
			}
		}
		fprintf(fp,"\n");
	}

	xfree(mn_series);
	xfree(mx_series);
	xfree(sum_series);
	xfree(mn_sx);
	xfree(mx_sx);
}

static void _get_all_node_series(FILE *fp, bool hd, hid_t jgid_step, int stepx)
{
	char     **tod = NULL;  // Date time at each sample
	char     **node_name;	// Node Names
	double **all_series;	// Pointers to all sampled for each node
	double *et = NULL;	// Elapsed time at each sample
	uint64_t *series_smp;   // Number of samples in this series

	hid_t	jgid_nodes, jgid_node;
	int	nnodes, ndx, len, nsmp = 0, nitem = -1;
	char	jgrp_node_name[MAX_GROUP_NAME+1];
	void*   series_data = NULL;
	hdf5_api_ops_t* ops;

	nnodes = get_int_attribute(jgid_step, ATTR_NNODES);
	// allocate node arrays

	series_smp = xmalloc(nnodes * (sizeof(uint64_t)));
	if (series_smp == NULL) {
		fatal("Failed to get memory for node_samples");
		return;		/* fix for CLANG false positive */
	}

	node_name = xmalloc(nnodes * (sizeof(char*)));
	if (node_name == NULL) {
		fatal("Failed to get memory for node_name");
		return;		/* fix for CLANG false positive */
	}

	all_series = xmalloc(nnodes * (sizeof(double*)));
	if (all_series == NULL) {
		fatal("Failed to get memory for all_series");
		return;		/* fix for CLANG false positive */
	}

	jgid_nodes = get_group(jgid_step, GRP_NODES);
	if (jgid_nodes < 0)
		fatal("Failed to open  group %s", GRP_NODES);

	for (ndx=0; ndx<nnodes; ndx++) {
		len = H5Lget_name_by_idx(jgid_nodes, ".", H5_INDEX_NAME,
		                         H5_ITER_INC, ndx, jgrp_node_name,
		                         MAX_GROUP_NAME, H5P_DEFAULT);
		if ((len < 0) || (len > MAX_GROUP_NAME)) {
			debug("Invalid node name=%s", jgrp_node_name);
			continue;
		}
		node_name[ndx] = xstrdup(jgrp_node_name);
		jgid_node = get_group(jgid_nodes, jgrp_node_name);
		if (jgid_node < 0) {
			debug("Failed to open group %s", jgrp_node_name);
			continue;
		}
		ops = NULL;
		nitem = 0;
		series_data = _get_series_data(jgid_node, params.series,
		                               &ops, &nitem);
		if (series_data==NULL || nitem==0 || ops==NULL) {
			if (ops != NULL)
				xfree(ops);
			continue;
		}
		all_series[ndx] = ops->get_series_values(
			params.data_item, series_data, nitem);
		if (!all_series[ndx])
			fatal("No data item %s",params.data_item);
		series_smp[ndx] = nitem;
		if (ndx == 0) {
			nsmp = nitem;
			tod = ops->get_series_tod(series_data, nitem);
			et = ops->get_series_values("time",
			                            series_data, nitem);
		} else {
			if (nitem > nsmp) {
				// new largest number of samples
				_delete_string_list(tod, nsmp);
				xfree(et);
				nsmp = nitem;
				tod = ops->get_series_tod(series_data,
				                          nitem);
				et = ops->get_series_values("time",
				                            series_data, nitem);
			}
		}
		xfree(ops);
		xfree(series_data);
		H5Gclose(jgid_node);
	}
	if (nsmp == 0) {
		// May be bad series name
		info("No values %s for series %s found in step %d",
		     params.data_item,params.series,
		     stepx);
	} else {
		_series_analysis(fp, hd, stepx, nnodes, nsmp,
		                 node_name, tod, et, all_series, series_smp);
	}
	for (ndx=0; ndx<nnodes; ndx++) {
		xfree(node_name[ndx]);
		xfree(all_series[ndx]);
	}
	xfree(node_name);
	xfree(all_series);
	xfree(series_smp);
	_delete_string_list(tod, nsmp);
	xfree(et);

	H5Gclose(jgid_nodes);

}

static void _get_all_task_series(FILE *fp, bool hd, hid_t jgid_step, int stepx)
{

	hid_t	jgid_tasks, jgid_task = 0, jgid_nodes, jgid_node;
	H5G_info_t group_info;
	int	ntasks,itx, tid;
	uint64_t *task_id;
	char     **task_node_name;	/* Node Name for each task */
	char     **tod = NULL;  /* Date time at each sample */
	char     **series_name;	/* Node Names */
	double **all_series;	/* Pointers to all sampled for each node */
	double *et = NULL;	/* Elapsed time at each sample */
	uint64_t *series_smp;   /* Number of samples in this series */
	int	nnodes, ndx, len, nsmp = 0, nitem = -1;
	char	jgrp_node_name[MAX_GROUP_NAME+1];
	char	jgrp_task_name[MAX_GROUP_NAME+1];
	char	buf[MAX_GROUP_NAME+1];
	void*   series_data = NULL;
	hdf5_api_ops_t* ops;

	jgid_nodes = get_group(jgid_step, GRP_NODES);
	if (jgid_nodes < 0)
		fatal("Failed to open  group %s", GRP_NODES);
	jgid_tasks = get_group(jgid_step, GRP_TASKS);
	if (jgid_tasks < 0)
		fatal("No tasks in step %d", stepx);
	H5Gget_info(jgid_tasks, &group_info);
	ntasks = (int) group_info.nlinks;
	if (ntasks <= 0)
		fatal("No tasks in step %d", stepx);
	task_id = xmalloc(ntasks*sizeof(uint64_t));
	if (task_id == NULL)
		fatal("Failed to get memory for task_ids");
	task_node_name = xmalloc(ntasks*sizeof(char*));
	if (task_node_name == NULL)
		fatal("Failed to get memory for task_node_names");

	for (itx = 0; itx<ntasks; itx++) {
		// Get the name of the group.
		len = H5Lget_name_by_idx(jgid_tasks, ".", H5_INDEX_NAME,
		                         H5_ITER_INC, itx, buf, MAX_GROUP_NAME,
		                         H5P_DEFAULT);
		if ((len > 0) && (len < MAX_GROUP_NAME)) {
			jgid_task = H5Gopen(jgid_tasks, buf, H5P_DEFAULT);
			if (jgid_task < 0)
				fatal("Failed to open %s", buf);
		} else
			fatal("Illegal task name %s",buf);
		task_id[itx] = get_int_attribute(jgid_task, ATTR_TASKID);
		task_node_name[itx] = get_string_attribute(jgid_task,
		                                           ATTR_NODENAME);
		H5Gclose(jgid_task);
	}
	H5Gclose(jgid_tasks);

	nnodes = get_int_attribute(jgid_step, ATTR_NNODES);
	// allocate node arrays
	series_smp = (uint64_t*) xmalloc(ntasks*(sizeof(uint64_t)));
	if (series_smp == NULL) {
		fatal("Failed to get memory for node_samples");
		return; /* Fix for CLANG false positive */
	}
	series_name = (char**) xmalloc(ntasks*(sizeof(char*)));
	if (series_name == NULL) {
		fatal("Failed to get memory for series_name");
		return; /* Fix for CLANG false positive */
	}
	all_series = (double**) xmalloc(ntasks*(sizeof(double*)));
	if (all_series == NULL) {
		fatal("Failed to get memory for all_series");
		return; /* Fix for CLANG false positive */
	}

	for (ndx=0; ndx<nnodes; ndx++) {

		len = H5Lget_name_by_idx(jgid_nodes, ".", H5_INDEX_NAME,
		                         H5_ITER_INC, ndx, jgrp_node_name,
		                         MAX_GROUP_NAME, H5P_DEFAULT);
		if ((len < 0) || (len > MAX_GROUP_NAME))
			fatal("Invalid node name=%s", jgrp_node_name);
		jgid_node = get_group(jgid_nodes, jgrp_node_name);

		if (jgid_node < 0)
			fatal("Failed to open group %s", jgrp_node_name);
		for (itx = 0; itx<ntasks; itx++) {
			if (strcmp(jgrp_node_name, task_node_name[itx]) != 0)
				continue;
			tid = task_id[itx];
			series_name[itx] = xstrdup_printf("%s_%d %s",
			                                  GRP_TASK,tid,jgrp_node_name);
			sprintf(jgrp_task_name,"%s_%d",GRP_TASK, tid);

			ops = NULL;
			nitem = 0;
			series_data = _get_series_data(jgid_node,
			                               jgrp_task_name, &ops, &nitem);
			if (series_data==NULL || nitem==0 || ops==NULL) {
				if (ops != NULL)
					xfree(ops);
				continue;
			}
			all_series[itx] = ops->get_series_values(
				params.data_item, series_data, nitem);
			if (!all_series[ndx])
				fatal("No data item %s",params.data_item);
			series_smp[itx] = nitem;
			if (nsmp == 0) {
				nsmp = nitem;
				tod = ops->get_series_tod(series_data, nitem);
				et = ops->get_series_values("time",
				                            series_data, nitem);
			} else {
				if (nitem > nsmp) {
					// new largest number of samples
					_delete_string_list(tod, nsmp);
					xfree(et);
					nsmp = nitem;
					tod = ops->get_series_tod(series_data,
					                          nitem);
					et = ops->get_series_values("time",
					                            series_data, nitem);
				}
			}
			xfree(ops);
			xfree(series_data);
		}
		H5Gclose(jgid_node);
	}
	if (nsmp == 0) {
		// May be bad series name
		info("No values %s for series %s found in step %d",
		     params.data_item,params.series,
		     stepx);
	} else {
		_series_analysis(fp, hd, stepx, ntasks, nsmp,
		                 series_name, tod, et, all_series, series_smp);
	}
	for (itx=0; itx<ntasks; itx++) {
		xfree(all_series[itx]);
	}
	xfree(series_name);
	xfree(all_series);
	xfree(series_smp);
	_delete_string_list(tod, nsmp);
	xfree(et);
	_delete_string_list(task_node_name, ntasks);
	xfree(task_id);

	H5Gclose(jgid_nodes);
}

static int _series_data(void)
{
	FILE *fp;
	bool hd = false;
	hid_t fid_job;
	hid_t jgid_root;
	hid_t jgid_step;
	int	nsteps;
	int stepx;
	char jgrp_step_name[MAX_GROUP_NAME + 1];

	fp = fopen(params.output, "w");
	if (fp == NULL) {
		error("Failed open file %s -- %m", params.output);
		return -1;
	}

	fid_job = H5Fopen(params.input, H5F_ACC_RDONLY, H5P_DEFAULT);
	if (fid_job < 0) {
		fclose(fp);
		error("Failed to open %s", params.input);
		return -1;
	}

	jgid_root = H5Gopen(fid_job, "/", H5P_DEFAULT);
	if (jgid_root < 0) {
		fclose(fp);
		H5Fclose(fid_job);
		error("Failed to open root");
		return -1;
	}

	nsteps = get_int_attribute(jgid_root, ATTR_NSTEPS);
	for (stepx = 0; stepx < nsteps; stepx++) {

		if ((params.step_id != -1) && (stepx != params.step_id))
			continue;

		sprintf(jgrp_step_name, "%s_%d", GRP_STEP, stepx);
		jgid_step = get_group(jgid_root, jgrp_step_name);
		if (jgid_step < 0) {
			error("Failed to open  group %s", jgrp_step_name);
			return -1;
		}

		if (strncmp(params.series,GRP_TASK,strlen(GRP_TASK)) == 0)
			_get_all_task_series(fp,hd,jgid_step, stepx);
		else
			_get_all_node_series(fp,hd,jgid_step, stepx);

		hd = true;
		H5Gclose(jgid_step);
	}

	H5Gclose(jgid_root);
	H5Fclose(fid_job);
	fclose(fp);

	return 0;
}
