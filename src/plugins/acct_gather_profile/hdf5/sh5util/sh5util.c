/*****************************************************************************\
 *  sh5util.c - slurm profile accounting plugin for io and energy using hdf5.
 *            - Utility to merge node-step files into a job file
 *            - or extract data from an job file
 *****************************************************************************
 *  Copyright (C) 2013 Bull S. A. S.
 *		Bull, Rue Jean Jaures, B.P.68, 78340, Les Clayes-sous-Bois.
 *
 *  Copyright (C) 2013-2016 SchedMD LLC
 *
 *  Initially written by Rod Schultz <rod.schultz@bull.com> @ Bull
 *  and Danny Auble <da@schedmd.com> @ SchedMD.
 *  Adapted by Yoann Blein <yoann.blein@bull.net> @ Bull.
 *
 *  This file is part of Slurm, a resource management program.
 *  For details, see <https://slurm.schedmd.com>.
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
 *
\*****************************************************************************/

#include "config.h"

#define _GNU_SOURCE

#include <dirent.h>
#include <fcntl.h>
#include <getopt.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/time.h>

#include "src/common/uid.h"
#include "src/common/read_config.h"
#include "src/common/proc_args.h"
#include "src/common/xstring.h"
#include "src/common/slurm_acct_gather_profile.h"
#include "src/common/slurm_jobacct_gather.h"
#include "../hdf5_api.h"
#include "sh5util.h"

#define MAX_PROFILE_PATH 1024
// #define MAX_ATTR_NAME 64
#define MAX_GROUP_NAME 64
// #define MAX_DATASET_NAME 64

// #define ATTR_NODENAME "Node Name"
// #define ATTR_STARTTIME "Start Time"
#define ATTR_NSTEPS "Number of Steps"
#define ATTR_NNODES "Number of Nodes"
// #define ATTR_NTASKS "Number of Tasks"
// #define ATTR_TASKID "Task Id"
// #define ATTR_CPUPERTASK "CPUs per Task"
// #define ATTR_DATATYPE "Data Type"
// #define ATTR_SUBDATATYPE "Subdata Type"
// #define ATTR_STARTTIME "Start Time"
// #define ATTR_STARTSEC "Start Second"
// #define SUBDATA_DATA "Data"
// #define SUBDATA_NODE "Node"
// #define SUBDATA_SAMPLE "Sample"
// #define SUBDATA_SERIES "Series"
// #define SUBDATA_TOTAL "Total"
// #define SUBDATA_SUMMARY "Summary"

#define GRP_ENERGY "Energy"
#define GRP_FILESYSTEM "Filesystem"
// #define GRP_STEP "Step"
#define GRP_STEPS "Steps"
#define GRP_NODES "Nodes"
// #define GRP_NODE "Node"
#define GRP_NETWORK "Network"
// #define GRP_SAMPLES "Time Series"
// #define GRP_SAMPLE "Sample"
// #define GRP_TASKS "Tasks"
#define GRP_TASK "Task"
// #define GRP_TOTALS "Totals"

// Data types supported by all HDF5 plugins of this type

/*
 * H5_VERSION_LE (lifted from 1.10.1 H5public.h) was not added until 1.8.7
 * (centos 6 has 1.8.5 by default)
 */
#ifndef H5_VERSION_LE
#define H5_VERSION_LE(Maj,Min,Rel) \
	(((H5_VERS_MAJOR==Maj) && (H5_VERS_MINOR==Min) &&		\
	  (H5_VERS_RELEASE<=Rel)) ||					\
	 ((H5_VERS_MAJOR==Maj) && (H5_VERS_MINOR<Min)) ||		\
	 (H5_VERS_MAJOR<Maj))
#endif

/* H5free_memory was introduced in 1.8.13, before it just needed to be 'free' */
#if H5_VERSION_LE(1,8,13)
#define H5free_memory free
#endif

sh5util_opts_t params;

typedef struct table {
	const char *step;
	const char *node;
	const char *group;
	const char *name;
} table_t;

typedef struct {
	char *file_name;
	int job_id;
	char *node_name;
	int step_id;
} sh5util_file_t;

static FILE* output_file;
static bool group_mode = false;
static const char *current_step;
static const char *current_node;

static void _cleanup(void);
static int _set_options(const int argc, char **argv);
static int _merge_step_files(void);
static int _extract_series(void);
static int _extract_item(void);
static int  _check_params(void);
static void _free_options(void);
static void _remove_empty_output(void);
static int _list_items(void);
static int _fields_intersection(hid_t fid_job, List tables, List fields);

static void _help_msg(void)
{
	printf("Usage sh5util [<OPTION>] -j <job[.stepid]>\n\n"
	       "Valid <OPTION> values are:\n"
	       " -L, --list           Print the items of a series contained in a job file.\n"
	       "     -i, --input      merged file to extract from (default ./job_$jobid.h5)\n"
	       "     -s, --series     Name of series:\n"
	       "                      Energy | Lustre | Network | Tasks\n"
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

	switch (params.mode) {
	case SH5UTIL_MODE_MERGE:
		info("Merging node-step files into %s",
		     params.output);
		cc = _merge_step_files();
		break;
	case SH5UTIL_MODE_EXTRACT:
		info("Extracting job data from %s into %s",
		     params.input, params.output);
		cc = _extract_series();
		break;
	case SH5UTIL_MODE_ITEM_EXTRACT:
		info("Extracting '%s' from '%s' data from %s into %s",
		     params.data_item, params.series,
		     params.input, params.output);
		cc = _extract_item();
		break;
	case SH5UTIL_MODE_ITEM_LIST:
		info("Listing items from %s", params.input);
		cc = _list_items();
		break;
	default:
		error("Unknown type %d", params.mode);
		break;
	}

ouch:
	_cleanup();

	return cc;
}

static void _destroy_sh5util_file(void *arg)
{
	sh5util_file_t *object;

	if (!arg)
		return;

	object = (sh5util_file_t *)arg;

	xfree(object->file_name);
	xfree(object->node_name);
	xfree(object);
}

static int _sh5util_sort_files_dec(void *s1, void *s2)
{
	sh5util_file_t* rec_a = *(sh5util_file_t **)s1;
	sh5util_file_t* rec_b = *(sh5util_file_t **)s2;

	if (rec_a->step_id < rec_b->step_id)
		return -1;
	else if (rec_a->step_id > rec_b->step_id)
		return 1;

	return 0;
}

static void _cleanup(void)
{
	_remove_empty_output();
	_free_options();
	log_fini();
	slurm_conf_destroy();
	jobacct_gather_fini();
	acct_gather_profile_fini();
	acct_gather_conf_destroy();
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

static void _void_free(void *str)
{
	xfree(str);
}

static int _str_cmp(void *str1, void *str2)
{
	return !xstrcmp((const char *)str1, (const char *)str2);
}

static void _remove_empty_output(void)
{
	struct stat sb;

	if (stat(params.output, &sb) == -1) {
		/*
		 * Ignore the error as the file may have not been created yet.
		 */
		return;
	}

	/*
	 * Remove the file if 0 size which means
	 * the program failed somewhere along the
	 * way and the file is just left hanging...
	 */
	if (!sb.st_size) {
		info("Output file generated is empty, removing it: %s",
		     params.output);
		if (remove(params.output) == -1)
			error("%s: remove(%s): %m", __func__, params.output);
	}
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
		{"list", no_argument, 0, 'L'},
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

#if DEBUG
	/* Move HDF5 trace printing to log file instead of stderr */
	H5Eset_auto(H5E_DEFAULT, (herr_t (*)(hid_t, void *))H5Eprint,
	            log_fp());
#else
	/* Silent HDF5 errors */
	H5Eset_auto(H5E_DEFAULT, NULL, NULL);
#endif

	_init_opts();

	while ((cc = getopt_long(argc, argv, "d:Ehi:Ij:l:LN:o:p:s:Su:UvV",
	                         long_options, &option_index)) != EOF) {
		switch (cc) {
		case 'd':
			params.data_item = xstrdup(optarg);
			/* params.data_item =
			   xstrtolower(params.data_item); */
			break;
		case 'E':
			params.mode = SH5UTIL_MODE_EXTRACT;
			break;
		case 'I':
			params.mode = SH5UTIL_MODE_ITEM_EXTRACT;
			break;
		case 'L':
			params.mode = SH5UTIL_MODE_ITEM_LIST;
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
			if (xstrcmp(optarg, GRP_ENERGY)
			    && xstrcmp(optarg, GRP_FILESYSTEM)
			    && xstrcmp(optarg, GRP_NETWORK)
			    && xstrncmp(optarg,GRP_TASK,
					strlen(GRP_TASK))) {
				error("Bad value for --series=\"%s\"",
				      optarg);
				return -1;
			}
			params.series = xstrdup(optarg);
			break;
		case 'S':
			params.keepfiles = 1;
			break;
		case 'u':
			if (uid_from_string(optarg, &u) < 0) {
				error("No such user --uid=\"%s\"",
				      optarg);
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
		if (!params.series)
			fatal("Must specify series option --series");

	}
	if (params.mode == SH5UTIL_MODE_ITEM_EXTRACT) {
		if (!params.data_item)
			fatal("Must specify data option --data ");

		if (!params.series)
			fatal("Must specify series option --series");

		if (!params.input)
			params.input = xstrdup_printf("./job_%d.h5",
						      params.job_id);

		if (!params.output)
			params.output = xstrdup_printf("./%s_%s_%d.csv",
			                               params.series,
			                               params.data_item,
			                               params.job_id);
	}
	if (params.mode == SH5UTIL_MODE_ITEM_LIST) {
		if (!params.input)
			params.input = xstrdup_printf(
				"./job_%d.h5", params.job_id);
		if (!params.series)
			fatal("Must specify series option --series");
	}

	if (!params.output)
		params.output = xstrdup_printf("./job_%d.h5", params.job_id);

	return 0;
}

/*
 * Copy the group "/{NodeName}" of the hdf5 file file_name into the location
 * jgid_nodes
 */
static int _merge_node_step_data(char* file_name, hid_t jgid_nodes,
				 sh5util_file_t *sh5util_file)
{
	hid_t fid_nodestep;
	char *group_name = NULL;
	int rc = SLURM_SUCCESS;

	fid_nodestep = H5Fopen(file_name, H5F_ACC_RDONLY, H5P_DEFAULT);
	if (fid_nodestep < 0) {
		error("Failed to open %s",file_name);
		return SLURM_ERROR;
	}

	group_name = xstrdup_printf("/%s", sh5util_file->node_name);
	hid_t ocpypl_id = H5Pcreate(H5P_OBJECT_COPY); /* default copy */
	hid_t lcpl_id   = H5Pcreate(H5P_LINK_CREATE); /* parameters */
	if (H5Ocopy(fid_nodestep, group_name, jgid_nodes,
		    sh5util_file->node_name,
	            ocpypl_id, lcpl_id) < 0) {
		debug("Failed to copy node step data of %s into the job file, "
		      "trying with old method",
		      sh5util_file->node_name);
		rc = SLURM_PROTOCOL_VERSION_ERROR;
		goto endit;
	}

	if (!params.keepfiles &&
	    (remove(file_name) == -1))
		error("%s: remove(%s): %m", __func__, file_name);

endit:
	xfree(group_name);
	H5Fclose(fid_nodestep);

	return rc;
}

/* Look for step and node files and merge them together into one job file */
static int _merge_step_files(void)
{
	hid_t fid_job = -1;
	hid_t jgid_steps = -1;
	hid_t jgid_step = -1;
	hid_t jgid_nodes = -1;
	DIR *dir;
	struct  dirent *de;

	char *file_name = NULL;
	char *jgrp_nodes_name = NULL;
	char *jgrp_step_name = NULL;
	char *pos_char = NULL;
	char *step_dir = NULL;
	char *step_path = NULL;
	char *stepno = NULL;
	int node_cnt = -1;
	int last_step = -1, step_cnt = 0;
	int job_id;
	int rc = SLURM_SUCCESS;
	ListIterator itr;
	List file_list = NULL;
	sh5util_file_t *sh5util_file = NULL;

	step_dir = xstrdup_printf("%s/%s", params.dir, params.user);

	if (!(dir = opendir(step_dir))) {
		error("Cannot open %s job profile directory: %m",
		      step_dir);
		rc = -1;
		goto endit;
	}

	while ((de = readdir(dir))) {
		xfree(file_name);
		file_name = xstrdup(de->d_name);

		if (file_name[0] == '.')
			continue;

		pos_char = strstr(file_name, ".h5");
		if (!pos_char)
			continue;
		*pos_char = 0;

		pos_char = strchr(file_name, '_');
		if (!pos_char)
			continue;
		*pos_char = 0;

		job_id = strtol(file_name, NULL, 10);
		if (job_id != params.job_id)
			continue;

		stepno = pos_char + 1;
		pos_char = strchr(stepno, '_');
		if (!pos_char) {
			continue;
		}
		*pos_char = 0;

		if (!file_list)
			file_list = list_create(_destroy_sh5util_file);

		sh5util_file = xmalloc(sizeof(sh5util_file_t));
		list_append(file_list, sh5util_file);

		sh5util_file->file_name = xstrdup(de->d_name);
		sh5util_file->job_id = job_id;

		if (!xstrcmp(stepno, "batch"))
			sh5util_file->step_id = -2;
		else
			sh5util_file->step_id = strtol(stepno, NULL, 10);

		stepno = pos_char + 1;
		sh5util_file->node_name = xstrdup(stepno);
	}
	closedir(dir);

	if (!file_list || !list_count(file_list)) {
		info("No node-step files found for jobid %d", params.job_id);
		goto endit;
	}

	fid_job = H5Fcreate(
		params.output, H5F_ACC_TRUNC, H5P_DEFAULT, H5P_DEFAULT);
	if (fid_job < 0) {
		error("Failed create HDF5 file %s", params.output);
		rc = -1;
		goto endit;
	}

	jgid_steps = make_group(fid_job, GRP_STEPS);
	if (jgid_steps < 0) {
		error("Failed to create group %s",
		      GRP_STEPS);
		rc = -1;
		goto endit;
	}

	/* sort the files so they are in step order */
	list_sort(file_list, (ListCmpF) _sh5util_sort_files_dec);

	node_cnt = 0;
	itr = list_iterator_create(file_list);
	while ((sh5util_file = list_next(itr))) {
		//info("got file of %s", sh5util_file->file_name);

		/* make a group for each step */
		if (sh5util_file->step_id != last_step) {
			last_step = sh5util_file->step_id;
			step_cnt++;
			/* on to the next step, close down the last one */
			if (jgid_step) {
				put_int_attribute(
					jgid_step, ATTR_NNODES, node_cnt);
				H5Gclose(jgid_nodes);
				H5Gclose(jgid_step);
				node_cnt = 0;
			}

			if (sh5util_file->step_id == -2)
				jgrp_step_name = xstrdup_printf(
					"/%s/batch", GRP_STEPS);
			else
				jgrp_step_name = xstrdup_printf(
					"/%s/%d", GRP_STEPS,
					sh5util_file->step_id);

//			info("making group for step %d", sh5util_file->step_id);
			jgid_step = make_group(fid_job, jgrp_step_name);
			if (jgid_step < 0) {
				error("Failed to create %s",
				      jgrp_step_name);
				xfree(jgrp_step_name);
				continue;
			}

			jgrp_nodes_name = xstrdup_printf(
				"%s/%s", jgrp_step_name, GRP_NODES);
			xfree(jgrp_step_name);

			jgid_nodes = make_group(
				jgid_step, jgrp_nodes_name);
			if (jgid_nodes < 0) {
				error("Failed to create %s",
				      jgrp_nodes_name);
				xfree(jgrp_nodes_name);
				continue;
			}
			xfree(jgrp_nodes_name);
		}

		node_cnt++;

		/* append onto the step */
		step_path = xstrdup_printf(
			"%s/%s", step_dir, sh5util_file->file_name);
		rc = _merge_node_step_data(
			step_path, jgid_nodes, sh5util_file);
		xfree(step_path);

	}
	list_iterator_destroy(itr);

	put_int_attribute(fid_job, ATTR_NSTEPS, step_cnt);


endit:
	FREE_NULL_LIST(file_list);
	xfree(file_name);
	xfree(step_dir);

	if (jgid_steps != -1)
		H5Gclose(jgid_steps);
	if (jgid_step != -1)
		H5Gclose(jgid_step);
	if (jgid_nodes != -1)
		H5Gclose(jgid_nodes);
	if (fid_job != -1)
		H5Fclose(fid_job);

	return rc;
}

/* ============================================================================
 * ============================================================================
 * Functions for data extraction
 * ============================================================================
 * ========================================================================= */

static void _table_free(void *table)
{
	table_t *t = (table_t *)table;
	xfree(t->step);
	xfree(t->node);
	xfree(t->group);
	xfree(t->name);
	xfree(table);
}

static void _table_path(table_t *t, char *path)
{
	snprintf(path, MAX_PROFILE_PATH,
	         "/"GRP_STEPS"/%s/"GRP_NODES"/%s/%s/%s",
	         t->step, t->node, t->group, t->name);
}

static herr_t _collect_tables_group(hid_t g_id, const char *name,
                                    const H5L_info_t *link_info, void *op_data)
{
	List tables = (List)op_data;
	hid_t table_id = -1;

	/* open the dataset. */
	if ((table_id = H5Dopen(g_id, name, H5P_DEFAULT)) < 0) {
		error("Failed to open the dataset %s", name);
		return -1;
	}
	H5Dclose(table_id);

	group_mode = true;

	table_t *t = xmalloc(sizeof(table_t));
	t->step  = xstrdup(current_step);
	t->node  = xstrdup(current_node);
	t->group = xstrdup(params.series);
	t->name  = xstrdup(name);
	list_append(tables, t);

	return 0;
}

static herr_t _collect_tables_node(hid_t g_id, const char *name,
                                   const H5L_info_t *link_info, void *op_data)
{
	char object_path[MAX_PROFILE_PATH+1];
	List tables = (List)op_data;
	hid_t object_id = -1;
	herr_t err;

	/* node filter */
	if (params.node
	    && xstrcmp(params.node, "*") != 0
	    && xstrcmp(params.node, name) != 0)
		return 0;

	snprintf(object_path, MAX_PROFILE_PATH+1, "%s/%s", name, params.series);
	current_node = name;

	/* open the dataset. */
	if ((object_id = H5Oopen(g_id, object_path, H5P_DEFAULT)) < 0) {
		error("Series %s not found", params.series);
		return -1;
	}

	if (H5Iget_type(object_id) == H5I_DATASET) {
		table_t *t = xmalloc(sizeof(table_t));
		t->step  = xstrdup(current_step);
		t->node  = xstrdup(name);
		t->group = xstrdup("");
		t->name  = xstrdup(params.series);
		list_append(tables, t);
	} else if (H5Iget_type(object_id) == H5I_GROUP) {
		err = H5Literate(object_id, H5_INDEX_NAME, H5_ITER_INC, NULL,
		                 _collect_tables_group, op_data);
		if (err < 0) {
			debug("2 Failed to iterate through group %s",
			      object_path);
			return SLURM_PROTOCOL_VERSION_ERROR;
		}
	} else {
		error("Object of unknown type: %s", object_path);
		H5Oclose(object_id);
		return -1;
	}

	H5Oclose(object_id);

	return 0;
}

static herr_t _collect_tables_step(hid_t g_id, const char *name,
                                   const H5L_info_t *link_info, void *op_data)
{
	char nodes_path[MAX_PROFILE_PATH];
	herr_t err;

	/* step filter */
	if ((params.step_id != -1) && (atoi(name) != params.step_id))
		return 0;

	snprintf(nodes_path, MAX_PROFILE_PATH, "%s/"GRP_NODES, name);
	current_step = name;

	err = H5Literate_by_name(g_id, nodes_path, H5_INDEX_NAME,
	                         H5_ITER_INC, NULL, _collect_tables_node,
	                         op_data, H5P_DEFAULT);
	if (err < 0) {
		debug("3 Failed to iterate through group /"GRP_STEPS"/%s",
		      nodes_path);
		return err;
	}

	return 0;
}

static int _tables_list(hid_t fid_job, List tables)
{
	herr_t err;
	ListIterator it;
	table_t *t;

	/* Find the list of tables to be extracted */
	err = H5Literate_by_name(fid_job, "/"GRP_STEPS, H5_INDEX_NAME,
	                         H5_ITER_INC, NULL, _collect_tables_step,
	                         (void *)tables, H5P_DEFAULT);
	if (err < 0) {
		debug("4 Failed to iterate through group /" GRP_STEPS);
		return SLURM_PROTOCOL_VERSION_ERROR;
	}

	debug("tables found (group mode: %d):", group_mode);
	it = list_iterator_create(tables);
	while ((t = list_next(it))) {
		debug(" /"GRP_STEPS"/%s/"GRP_NODES"/%s/%s/%s",
		      t->step, t->node, t->group, t->name);
	}
	list_iterator_destroy(it);

	return SLURM_SUCCESS;
}


/**
 * Print the total values of a table to the output file
 *
 * @param nb_fields Number of fields in the dataset
 * @param offsets   Offset of each field
 * @param types     Type of each field
 * @param type_size Size of of a record in the dataset
 * @param table_id  ID of the table to extract from
 * @param state     State of the current extraction
 * @param node_name Name of the node containing this table
 * @param output    output file
 */
static void _extract_totals(size_t nb_fields, size_t *offsets, hid_t *types,
                            hsize_t type_size, hid_t table_id,
                            table_t *table, FILE *output)
{
	hsize_t nrecords;
	size_t i, j;
	uint8_t *data;

	/* allocate space for aggregate values: 4 values (min, max,
	 * sum, avg) on 8 bytes (uint64_t/double) for each field */
	uint64_t *agg_i;
	double *agg_d;

	data = xmalloc(type_size);
	agg_i = xmalloc(nb_fields * 4 * sizeof(uint64_t));
	agg_d = (double *)agg_i;
	H5PTget_num_packets(table_id, &nrecords);

	/* compute min/max/sum */
	for (i = 0; i < nrecords; ++i) {
		H5PTget_next(table_id, 1, data);
		for (j = 0; j < nb_fields; ++j) {
			if (H5Tequal(types[j], H5T_NATIVE_UINT64)) {
				uint64_t v = *(uint64_t *)(data + offsets[j]);
				uint64_t *a = agg_i + j * 4;
				if (i == 0 || v < a[0]) /* min */
					a[0] = v;
				if (v > a[1]) /* max */
					a[1] = v;
				a[2] += v; /* sum */
			} else if (H5Tequal(types[j], H5T_NATIVE_DOUBLE)) {
				double v = *(double *)(data + offsets[j]);
				double *a = agg_d + j * 4;
				if (i == 0 || v < a[0]) /* min */
					a[0] = v;
				if (v > a[1]) /* max */
					a[1] = v;
				a[2] += v; /* sum */
			}
		}
	}

	/* compute avg */
	if (nrecords) {
		for (j = 0; j < nb_fields; ++j) {
			if (H5Tequal(types[j], H5T_NATIVE_UINT64)) {
				agg_d[j*4+3] = (double)agg_i[j*4+2] / nrecords;
			} else if (H5Tequal(types[j], H5T_NATIVE_DOUBLE)) {
				agg_d[j*4+3] = (double)agg_d[j*4+2] / nrecords;
			}
		}
	}

	/* step, node */
	fprintf(output, "%s,%s", table->step, table->node);

	if (group_mode)
		fprintf(output, ",%s", table->name);

	/* elapsed time (first field in the last record) */
	fprintf(output, ",%"PRIu64, *(uint64_t *)data);

	/* aggregate values */
	for (j = 0; j < nb_fields; ++j) {
		if (H5Tequal(types[j], H5T_NATIVE_UINT64)) {
			fprintf(output, ",%"PRIu64",%"PRIu64",%"PRIu64",%lf",
			        agg_i[j * 4 + 0],
			        agg_i[j * 4 + 1],
			        agg_i[j * 4 + 2],
			        agg_d[j * 4 + 3]);
		} else if (H5Tequal(types[j], H5T_NATIVE_DOUBLE)) {
			fprintf(output, ",%lf,%lf,%lf,%lf",
			        agg_d[j * 4 + 0],
			        agg_d[j * 4 + 1],
			        agg_d[j * 4 + 2],
			        agg_d[j * 4 + 3]);
		}
	}
	fputc('\n', output);
	xfree(agg_i);
	xfree(data);
}

/**
 * Extract the content of a table within a node. This function first discovers
 * the content of the table and then handles both timeseries and totals levels.
 */
static int _extract_series_table(hid_t fid_job, table_t *table, List fields,
				 FILE *output, bool level_total)
{
	char path[MAX_PROFILE_PATH];

	size_t i, j;

	size_t max_fields = list_count(fields);
	size_t nb_fields = 0;
	size_t offsets[max_fields];
	hid_t types[max_fields];

	hid_t did = -1;    /* dataset id */
	hid_t tid = -1;    /* file type ID */
	hid_t n_tid = -1;  /* native type ID */
	hid_t m_tid = -1;  /* member type ID */
	hid_t nm_tid = -1; /* native member ID */
	hid_t table_id = -1;
	hsize_t nmembers;
	hsize_t type_size;
	hsize_t nrecords;
	char *m_name;

	_table_path(table, path);
	debug("Extracting from table %s", path);

	/* open the dataset. */
	if ((did = H5Dopen(fid_job, path, H5P_DEFAULT)) < 0) {
		error("Failed to open the table %s", path);
		goto error;
	}

	/* get the datatype */
	if ((tid = H5Dget_type(did)) < 0)
		goto error;
	if ((n_tid = H5Tget_native_type(tid, H5T_DIR_DEFAULT)) < 0)
		goto error;

	type_size = H5Tget_size(n_tid);

	/* get the number of members */
	if ((nmembers = H5Tget_nmembers(tid)) == 0)
		goto error;

	/* iterate through the members */
	for (i = 0; i < nmembers; i++) {
		m_name = H5Tget_member_name(tid, (unsigned)i);
		/* continue if the field must not be extracted */
		if (!list_find_first(fields, _str_cmp, m_name)) {
			H5free_memory(m_name);
			continue;
		}
		H5free_memory(m_name);

		/* get the member type */
		if ((m_tid = H5Tget_member_type(tid, (unsigned)i)) < 0)
			goto error;
		if ((nm_tid = H5Tget_native_type(m_tid, H5T_DIR_DEFAULT)) < 0)
			goto error;

		types[nb_fields] = nm_tid;
		offsets[nb_fields] = H5Tget_member_offset(n_tid, (unsigned)i);
		++nb_fields;

		/*H5Tclose(nm_tid);*/
		H5Tclose(m_tid);
	}

	H5Tclose(n_tid);
	H5Tclose(tid);
	H5Dclose(did);

	/* open the table */
	if ((table_id = H5PTopen(fid_job, path)) < 0) {
		error("Failed to open the series %s", params.input);
		goto error;
	}

	if (level_total) {
		_extract_totals(nb_fields, offsets, types, type_size,
		                table_id, table, output);
	} else {
		/* Timeseries level */
		H5PTget_num_packets(table_id, &nrecords);
		uint8_t data[type_size];

		/* print the expected fields of all the records */
		for (i = 0; i < nrecords; ++i) {
			H5PTget_next(table_id, 1, data);
			fprintf(output, "%s,%s", table->step, table->node);
			if (group_mode)
				fprintf(output, ",%s", table->name);

			for (j = 0; j < nb_fields; ++j) {
				if (H5Tequal(types[j], H5T_NATIVE_UINT64)) {
					fprintf(output, ",%"PRIu64,
					        *(uint64_t *)(data+offsets[j]));
				} else if (H5Tequal(types[j],
				                    H5T_NATIVE_DOUBLE)) {
					fprintf(output, ",%lf",
					        *(double *)(data + offsets[j]));
				} else {
					error("Unknown type");
					goto error;
				}
			}
			fputc('\n', output);
		}
	}

	H5PTclose(table_id);

	return SLURM_SUCCESS;

error:
	if (nm_tid >= 0) H5Dclose(nm_tid);
	if (m_tid >= 0) H5Dclose(m_tid);
	if (n_tid >= 0) H5Dclose(n_tid);
	if (tid >= 0) H5Dclose(tid);
	if (did >= 0) H5PTclose(did);
	if (table_id >= 0) H5PTclose(table_id);
	return SLURM_ERROR;
}

/* _extract_series()
 */
static int _extract_series(void)
{
	hid_t fid_job = -1;
	bool level_total;
	const char *field;
	List tables = NULL;
	List fields = NULL;
	ListIterator it;
	FILE *output = NULL;
	int rc = SLURM_ERROR;
	table_t *t;

	level_total = (xstrcasecmp(params.level, "Node:Totals") == 0);

	output = fopen(params.output, "w");
	if (output == NULL) {
		error("Failed to create output file %s -- %m",
		      params.output);
		goto error;
	}

	fid_job = H5Fopen(params.input, H5F_ACC_RDONLY, H5P_DEFAULT);
	if (fid_job < 0) {
		error("Failed to open %s", params.input);
		goto error;
	}

	/* Find the list of tables to be extracted */
	tables = list_create(_table_free);
	if ((rc = _tables_list(fid_job, tables)) != SLURM_SUCCESS) {
		debug("Failed to list tables %s", params.series);
		goto error;
	}

	/* Find the fields to be extracted */
	fields = list_create(_void_free);
	if ((rc = _fields_intersection(fid_job, tables, fields))
	    != SLURM_SUCCESS) {
		error("Failed to find data items for series %s", params.series);
		goto error;
	}

	/* csv header */
	fprintf(output, "Step,Node");

	if (group_mode) {
		fprintf(output, ",Series");
	}

	if (level_total) {
		/* do not aggregate time values */
		list_delete_all(fields, _str_cmp, "ElapsedTime");
		fputs(",ElapsedTime", output);
	}

	it = list_iterator_create(fields);
	while ((field = list_next(it))) {
		if (level_total) {
			fprintf(output, ",Min_%s,Max_%s,Sum_%s,Avg_%s",
			        field, field, field, field);
		} else {
			fprintf(output, ",%s", field);
		}
	}
	fputc('\n', output);
	list_iterator_destroy(it);

	/* Extract from every table */
	it = list_iterator_create(tables);
	while ((t = list_next(it))) {
		if (_extract_series_table(fid_job, t, fields,
		                          output, level_total) < 0) {
			error("Failed to extract series");
			goto error;
		}
	}

	FREE_NULL_LIST(tables);
	FREE_NULL_LIST(fields);
	H5Fclose(fid_job);
	fclose(output);
	return SLURM_SUCCESS;

error:
	FREE_NULL_LIST(fields);
	FREE_NULL_LIST(tables);
	if (output) fclose(output);
	if (fid_job >= 0) H5Fclose(fid_job);
	return rc;
}

/* ============================================================================
 * ============================================================================
 * Functions for data item extraction
 * ============================================================================
 * ========================================================================= */

/**
 * Perform the analysis on a given item of type uint64_t, present in multiple
 * tables.
 *
 * @param nb_tables  Number of table to analyze
 * @param tables     IDs of all the tables to analyze
 * @param nb_records Number of records in each table
 * @param buf_size   Size of the largest record of the tables
 * @param offsets    Offset of the item analyzed in each table
 * @param names      Names of the tables
 * @param nodes      Name of the node for each table
 * @param step_name  Name of the current step
 */
static void _item_analysis_uint(hsize_t nb_tables, hid_t *tables,
				hsize_t *nb_records, size_t buf_size,
				size_t *offsets,
				const char *names[], const char *nodes[],
				const char *step_name)
{
	size_t   i;
	uint64_t min_val;
	size_t   min_idx = 0;
	uint64_t max_val;
	size_t   max_idx = 0;
	uint64_t sum, sum_max = 0;
	double   avg, avg_max = 0;
	size_t   nb_series_in_smp;
	uint64_t v;
	uint64_t values[nb_tables];
	uint8_t  *buffer;
	uint64_t et = 0, et_max = 0;

	buffer = xmalloc(buf_size);
	for (;;) {
		min_val = UINT64_MAX;
		max_val = 0;
		sum = 0;
		nb_series_in_smp = 0;

		/* compute aggregate values */
		for (i = 0; i < nb_tables; ++i) {
			if (nb_records[i] == 0)
				continue;
			--nb_records[i];
			++nb_series_in_smp;
			/* read the value of the item in the series i */
			H5PTget_next(tables[i], 1, (void *)buffer);
			v = *(uint64_t *)(buffer + offsets[i]);
			values[i] = v;
			/* compute the sum, min and max */
			sum += v;
			if ((i == 0) || (v < min_val)) {
				min_val = v;
				min_idx = i;
			}
			if ((i == 0) || (v > max_val)) {
				max_val = v;
				max_idx = i;
			}
			/* Elapsed time is always at offset 0 */
			et = *(uint64_t *)buffer;
		}

		if (nb_series_in_smp == 0) /* stop if no more samples */
			break;

		avg = (double)sum / (double)nb_series_in_smp;

		/* store the greatest sum */
		if (sum > sum_max) {
			sum_max = sum;
			avg_max = avg;
			et_max = et;
		}

		if (group_mode) {
			fprintf(output_file,
			        "%s,%"PRIu64",%s %s,%"PRIu64",%s %s,"
				"%"PRIu64",%"PRIu64",%lf,%zu",
			        step_name, et,
			        names[min_idx], nodes[min_idx], min_val,
			        names[max_idx], nodes[max_idx], max_val,
			        sum, avg, nb_series_in_smp);
		} else {
			fprintf(output_file,
			        "%s,%"PRIu64",%s,%"PRIu64",%s,%"PRIu64",%"
				PRIu64",%lf,%zu",
			        step_name, et,
			        nodes[min_idx], min_val,
			        nodes[max_idx], max_val,
			        sum, avg, nb_series_in_smp);
		}

		/* print value of each series */
		for (i = 0; i < nb_tables; ++i) {
			fprintf(output_file, ",%"PRIu64, values[i]);
			/* and set their values to zero if no more values */
			if (values[i] && nb_records[i] == 0)
				values[i] = 0;
		}
		fputc('\n', output_file);
	}
	xfree(buffer);

	printf("    Step %s Maximum accumulated %s Value (%"PRIu64") occurred "
	       "at Time=%"PRIu64", Ave Node %lf\n",
	       step_name, params.data_item, sum_max, et_max, avg_max);
}

/**
 * Perform the analysis on a given item of type double, present in multiple
 * tables.
 * See _item_analysis_uint for parameters description.
 */
static void _item_analysis_double(hsize_t nb_tables, hid_t *tables,
				  hsize_t *nb_records, size_t buf_size,
				  size_t *offsets,
				  const char *names[], const char *nodes[],
				  const char *step_name)
{
	size_t   i;
	double   min_val;
	size_t   min_idx = 0;
	double   max_val;
	size_t   max_idx = 0;
	double   sum, sum_max = 0;
	double   avg, avg_max = 0;
	size_t   nb_series_in_smp;
	double   v;
	double   values[nb_tables];
	uint8_t  *buffer;
	uint64_t et = 0, et_max = 0;

	buffer = xmalloc(buf_size);
	for (;;) {
		min_val = UINT64_MAX;
		max_val = 0;
		sum = 0;
		nb_series_in_smp = 0;

		/* compute aggregate values */
		for (i = 0; i < nb_tables; ++i) {
			if (nb_records[i] == 0)
				continue;
			--nb_records[i];
			++nb_series_in_smp;
			/* read the value of the item in the series i */
			H5PTget_next(tables[i], 1, (void *)buffer);
			v = *(double *)(buffer + offsets[i]);
			values[i] = v;
			/* compute the sum, min and max */
			sum += v;
			if ((i == 0) || (v < min_val)) {
				min_val = v;
				min_idx = i;
			}
			if ((i == 0) || (v > max_val)) {
				max_val = v;
				max_idx = i;
			}
			/* Elapsed time is always at offset 0 */
			et = *(double *)buffer;
		}

		if (nb_series_in_smp == 0) /* stop if no more samples */
			break;

		avg = (double)sum / (double)nb_series_in_smp;

		/* store the greatest sum */
		if (sum > sum_max) {
			sum_max = sum;
			avg_max = avg;
			et_max = et;
		}

		fprintf(output_file,
			"%s,%"PRIu64",%s,%lf,%s,%lf,%lf,%lf,%zu",
		        step_name, et,
		        names[min_idx], min_val, names[max_idx], max_val,
		        sum, avg, nb_series_in_smp);

		/* print value of each series */
		for (i = 0; i < nb_tables; ++i) {
			fprintf(output_file, ",%lf", values[i]);
			/* and set their values to zero if no more values */
			if (values[i] && nb_records[i] == 0)
				values[i] = 0;
		}
		fputc('\n', output_file);
	}
	xfree(buffer);

	printf("    Step %s Maximum accumulated %s Value (%lf) occurred "
	       "at Time=%"PRIu64", Ave Node %lf\n",
	       step_name, params.data_item, sum_max, et_max, avg_max);
}

static herr_t _extract_item_step(hid_t g_id, const char *step_name,
                                 const H5L_info_t *link_info, void *op_data)
{
	static bool first = true;

	char nodes_path[MAX_PROFILE_PATH];
	char path[MAX_PROFILE_PATH];

	size_t i, j;
	size_t buf_size = 0;
	char *m_name;

	hid_t fid_job = *((hid_t *)op_data);
	hid_t did = -1;    /* dataset id */
	hid_t tid = -1;    /* file type ID */
	hid_t n_tid = -1;  /* native type ID */
	hid_t m_tid = -1;  /* member type ID */
	hid_t nm_tid = -1; /* native member ID */
	hsize_t nmembers;
	hid_t item_type = -1;
	herr_t err;

	List tables = NULL;
	ListIterator it = NULL;
	table_t *t;

	/* step filter */
	if ((params.step_id != -1) && (atoi(step_name) != params.step_id))
		return 0;

	current_step = step_name;

	snprintf(nodes_path, MAX_PROFILE_PATH, "%s/"GRP_NODES, step_name);

	tables = list_create(_table_free);
	err = H5Literate_by_name(g_id, nodes_path, H5_INDEX_NAME,
	                         H5_ITER_INC, NULL, _collect_tables_node,
	                         (void *)tables, H5P_DEFAULT);
	if (err < 0) {
		debug("1 Failed to iterate through group /"GRP_STEPS"/%s",
		      nodes_path);
		FREE_NULL_LIST(tables);
		return -1;
	}

	size_t nb_tables = list_count(tables);
	hid_t tables_id[nb_tables];
	size_t offsets[nb_tables];
	hsize_t nb_records[nb_tables];
	const char *names[nb_tables];
	const char *nodes[nb_tables];

	for (i = 0; i < nb_tables; ++i) {
		tables_id[i] = -1;
		nb_records[i] = 0;
	}

	it = list_iterator_create(tables);
	i = 0;
	while ((t = (table_t *)list_next(it))) {
		names[i] = t->name;
		nodes[i] = t->node;

		/* open the dataset. */
		_table_path(t, path);
		if ((did = H5Dopen(fid_job, path, H5P_DEFAULT)) < 0) {
			error("Failed to open the series %s", path);
			goto error;
		}

		/* get the datatype */
		if ((tid = H5Dget_type(did)) < 0)
			goto error;
		if ((n_tid = H5Tget_native_type(tid, H5T_DIR_DEFAULT)) < 0)
			goto error;

		buf_size = MAX(buf_size, H5Tget_size(n_tid));

		/* get the number of members */
		if ((nmembers = H5Tget_nmembers(tid)) == 0)
			goto error;

		/* iterate through the members and stop when params.data_item
		 * is found */
		for (j = 0; j < nmembers; j++) {
			m_name = H5Tget_member_name(tid, (unsigned)j);
			if (xstrcasecmp(params.data_item, m_name) == 0) {
				H5free_memory(m_name);
				break;
			}
			H5free_memory(m_name);
		}

		if (j == nmembers) {
			error("Item %s not found in series %s",
			      params.data_item, path);
			goto error;
		}

		offsets[i] = H5Tget_member_offset(n_tid, (unsigned)j);

		/* get the member type */
		if ((m_tid = H5Tget_member_type(tid, (unsigned)j)) < 0)
			goto error;
		if ((nm_tid = H5Tget_native_type(m_tid, H5T_DIR_DEFAULT)) < 0)
			goto error;

		if (item_type == -1) {
			item_type = nm_tid;
		} else if (nm_tid != item_type) {
			error("Malformed file: fields with the same name in "
			      "tables with the same name must have the same "
			      "types");
			goto error;
		}

		H5Tclose(nm_tid);
		H5Tclose(m_tid);

		H5Tclose(n_tid);
		H5Tclose(tid);
		H5Dclose(did);

		/* open the table */
		if ((tables_id[i] = H5PTopen(fid_job, path)) < 0) {
			error("Failed to open the series %s", path);
			goto error;
		}
		H5PTget_num_packets(tables_id[i], &nb_records[i]);

		++i;
	}

	if (first) {
		/* complete header */
		first = false;
		list_iterator_reset(it);
		while ((t = (table_t *)list_next(it))) {
			if (group_mode)
				fprintf(output_file, ",%s", t->node);
			else
				fprintf(output_file, ",%s %s",
					t->name, t->node);
		}
		fputc('\n', output_file);
	}

	list_iterator_destroy(it);

	if (H5Tequal(item_type, H5T_NATIVE_UINT64)) {
		_item_analysis_uint(nb_tables, tables_id, nb_records, buf_size,
		                    offsets, names, nodes, step_name);
	} else if (H5Tequal(item_type, H5T_NATIVE_DOUBLE)) {
		_item_analysis_double(nb_tables, tables_id,
				      nb_records, buf_size,
		                      offsets, names, nodes, step_name);
	} else {
		error("Unknown type");
		goto error;
	}

	/* clean up */
	for (i = 0; i < nb_tables; ++i) {
		H5PTclose(tables_id[i]);
	}
	FREE_NULL_LIST(tables);

	return 0;

error:
	if (did >= 0) H5Dclose(did);
	if (tid >= 0) H5Tclose(tid);
	if (n_tid >= 0) H5Tclose(n_tid);
	if (m_tid >= 0) H5Tclose(m_tid);
	if (nm_tid >= 0) H5Tclose(nm_tid);
	FREE_NULL_LIST(tables);
	for (i = 0; i < nb_tables; ++i) {
		if (tables_id[i] >= 0)
			H5PTclose(tables_id[i]);
	}
	return -1;
}

static int _extract_item(void)
{
	hid_t fid_job;
	herr_t err;

	output_file = fopen(params.output, "w");
	if (output_file == NULL) {
		error("Failed to create output file %s -- %m",
		      params.output);
	}

	fid_job = H5Fopen(params.input, H5F_ACC_RDONLY, H5P_DEFAULT);
	if (fid_job < 0) {
		fclose(output_file);
		error("Failed to open %s", params.input);
		return SLURM_ERROR;
	}

	/* csv header */
	fputs("Step,ElaspedTime,MinNode,MinValue,MaxNode,MaxValue,Sum,Avg,"
	      "NumNodes", output_file);

	err = H5Literate_by_name(fid_job, "/" GRP_STEPS, H5_INDEX_NAME,
	                         H5_ITER_INC, NULL, _extract_item_step,
	                         (void *)(&fid_job), H5P_DEFAULT);
	if (err < 0) {
		debug("hnere Failed to iterate through group /" GRP_STEPS);
		H5Fclose(fid_job);
		fclose(output_file);
		return SLURM_PROTOCOL_VERSION_ERROR;
	}

	H5Fclose(fid_job);
	fclose(output_file);

	return SLURM_SUCCESS;
}

static int _fields_intersection(hid_t fid_job, List tables, List fields)
{
	hid_t jgid_table = -1;
	hid_t tid = -1;
	hssize_t nb_fields;
	size_t i;
	char *field;
	ListIterator it1, it2;
	bool found;
	char path[MAX_PROFILE_PATH];
	table_t *t;
	bool first = true;

	if (fields == NULL || tables == NULL)
		return SLURM_ERROR;

	it1 = list_iterator_create(tables);
	while ((t = (table_t *)list_next(it1))) {
		_table_path(t, path);
		jgid_table = H5Dopen(fid_job, path, H5P_DEFAULT);
		if (jgid_table < 0) {
			error("Failed to open table %s", path);
			return SLURM_ERROR;
		}

		tid = H5Dget_type(jgid_table);
		nb_fields = H5Tget_nmembers(tid);

		if (first) {
			first = false;
			/* nothing to intersect yet, copy all the fields */
			for (i = 0; i < nb_fields; i++) {
				field = H5Tget_member_name(tid, i);
				list_append(fields, xstrdup(field));
				H5free_memory(field);
			}
		} else {
			/* gather fields */
			char *l_fields[nb_fields];
			for (i = 0; i < nb_fields; i++) {
				l_fields[i] = H5Tget_member_name(tid, i);
			}
			/* remove fields that are not in current table */
			it2 = list_iterator_create(fields);
			while ((field = list_next(it2))) {
				found = false;
				for (i = 0; i < nb_fields; i++) {
					if (xstrcmp(field, l_fields[i]) == 0) {
						found = true;
						break;
					}
				}
				if (!found) {
					list_delete_item(it2);
				}
			}
			list_iterator_destroy(it2);
			/* clean up fields */
			for (i = 0; i < nb_fields; i++)
				H5free_memory(l_fields[i]);
		}

		H5Tclose(tid);
		H5Dclose(jgid_table);
	}
	list_iterator_destroy(it1);

	return SLURM_SUCCESS;
}

/* List the intersection of the items of all tables with the same name, for all
 * table names. The list is printed on the standard output */
static int _list_items(void)
{
	hid_t fid_job = -1;
	List fields;
	ListIterator it;
	const char *field;
	int rc = SLURM_ERROR;
	List tables;

	/* get series names */
	fid_job = H5Fopen(params.input, H5F_ACC_RDONLY, H5P_DEFAULT);
	if (fid_job < 0) {
		error("Failed to open %s", params.input);
		return SLURM_ERROR;
	}

	/* Find the list of tables to be extracted */
	tables = list_create(_table_free);
	if ((rc = _tables_list(fid_job, tables)) != SLURM_SUCCESS) {
		debug("Failed to list tables %s", params.series);
		H5Fclose(fid_job);
		FREE_NULL_LIST(tables);
		return rc;
	}

	fields = list_create(_void_free);
	if ((rc = _fields_intersection(fid_job, tables, fields))
	    != SLURM_SUCCESS) {
		error("Failed to intersect fields");
		H5Fclose(fid_job);
		FREE_NULL_LIST(tables);
		FREE_NULL_LIST(fields);
		return rc;
	}

	it = list_iterator_create(fields);
	while ((field = list_next(it))) {
		printf("%s\n", field);
	}
	list_iterator_destroy(it);

	FREE_NULL_LIST(tables);
	FREE_NULL_LIST(fields);

	H5Fclose(fid_job);

	return SLURM_SUCCESS;
}
