/*****************************************************************************\
 *  Function:  ll_parse_string
 *
 *  Description: This function is used to parse a character string (JCL format)
 *  which contains information needed by a job to run via LL. A corresponding
 *  job object handle is returned and should be used for any subsequent
 *  ll_request calls.
 *
 *  Arguments:
 *    IN jobmgmtObj :  Pointer to the LL_element handle returned by
 *    			the ll_init_job function.
 *    IN jobstring : Character string representing the Job Command
 *    			File to be parsed, must be in JCL format.
 *    IN job_version : Integer indicating the version of llsubmit to
 *    			be used during the parse.
 *    IN llpp_parms : Array representing the pre-processor parameters
 *    			to be used during the jobstring parse.
 *    OUT job : LL_element handle containing the address of
 *    			the job object created during the parse.
 *    OUT error_object : LL_element handle containing the address of the
 *    			error object which contains any error
 *    			information from the parse.
 *    RET Success: 0
 *        Failure: -1: Invalid jobmgmtObj.
 *                 -2: Schedd not available.
 *                 -5: System error.
 *
 *****************************************************************************
 *  Copyright (C) 2004 The Regents of the University of California.
 *  Produced at Lawrence Livermore National Laboratory.
 *  Written by Morris Jette <jette1@llnl.gov>
 * 
 *  This file is part of slurm_ll_api, a collection of LoadLeveler-compatable
 *  interfaces to Simple Linux Utility for Resource Managment (SLURM).  These 
 *  interfaces are used by POE (IBM's Parallel Operating Environment) to 
 *  initiated SLURM jobs. For details, see <http://www.llnl.gov/linux/slurm/>.
 *
 *  This notice is required to be provided under our contract with the U.S.
 *  Department of Energy (DOE).  This work was produced at the University
 *  of California, Lawrence Livermore National Laboratory under Contract
 *  No. W-7405-ENG-48 with the DOE.
 * 
 *  Neither the United States Government nor the University of California
 *  nor any of their employees, makes any warranty, express or implied, or
 *  assumes any liability or responsibility for the accuracy, completeness,
 *  or usefulness of any information, apparatus, product, or process
 *  disclosed, or represents that its use would not infringe
 *  privately-owned rights.
 *
 *  Also, reference herein to any specific commercial products, process, or
 *  services by trade name, trademark, manufacturer or otherwise does not
 *  necessarily constitute or imply its endorsement, recommendation, or
 *  favoring by the United States Government or the University of
 *  California.  The views and opinions of authors expressed herein do not
 *  necessarily state or reflect those of the United States Government or
 *  the University of California, and shall not be used for advertising or
 *  product endorsement purposes.
 * 
 *  The precise terms and conditions for copying, distribution and
 *  modification are specified in the file "COPYING".
\*****************************************************************************/

#include <ctype.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>

#include "common.h"
#include "config.h"
#include "llapi.h"

#define SEP_STRING "===================="

enum    cmd_keys { EMPTY_KEY, INVALID_KEY,
		ACCOUNT_NO_KEY, ARGUMENTS_KEY, BLOCKING_KEY, 
		CHECKPOINT_KEY, CLASS_KEY, COMMENT_KEY, 
		CORE_LIMIT_KEY, CPU_LIMIT_KEY, DATA_LIMIT_KEY,
		DEPENDENCY_KEY, ENVIRONMENT_KEY, ERROR_KEY, 
		EXECUTABLE_KEY, FILE_LIMIT_KEY, GROUP_KEY, 
		HOLD_KEY, IMAGE_SIZE_KEY, INITIAL_DIR_KEY,
		INPUT_KEY , JOB_CPU_LIMIT_KEY, JOB_NAME_KEY, 
		JOB_TYPE_KEY, MAX_PROCESSORS_KEY, MIN_PROCESSORS_KEY, 
		NETWORK_KEYM, NETWORK_KEYL, NETWORK_KEYML ,NODE_KEY,
		NODE_USAGE_KEY, NOTIFICATION_KEY, 
		NOTIFY_USER_KEY, OUTPUT_KEY, PARALLEL_PATH_KEY, 
		PREFERENCES_KEY, QUEUE_KEY, REQUIREMENTS_KEY, 
		RESOURCES_KEY, RESTART_KEY, RSS_LIMIT_KEY, SHELL_KEY, 
		STACK_LIMIT_KEY, START_DATE_KEY, STEP_NAME_KEY, 
		TASK_GEOMETRY_KEY, TASKS_PER_NODE_KEY, TOTAL_TASKS_KEY, 
		USER_PRIORITY_KEY, WALL_CLOCK_LIMIT_KEY, BULKXFER_KEY };
typedef struct {
	enum    cmd_keys key_number;
	char *           key_string;
	int              key_len;
} key_table_t;

static key_table_t key_table[] = {
	{ACCOUNT_NO_KEY,	"account_no",		10},
	{ARGUMENTS_KEY,		"arguments",		9},
	{BLOCKING_KEY,		"blocking",		8},
	{CHECKPOINT_KEY,	"checkpoint",		10},
	{CLASS_KEY,		"class",		5},
	{COMMENT_KEY,		"comment",		7},
	{CORE_LIMIT_KEY,	"core_limit",		10},
	{CPU_LIMIT_KEY,		"cpu_limit",		9},
	{DATA_LIMIT_KEY,	"data_limit",		10},
	{DEPENDENCY_KEY,	"dependency",		10},
	{ENVIRONMENT_KEY,	"environment",		11},
	{ERROR_KEY,		"error",		5},
	{EXECUTABLE_KEY,	"executable",		10},
	{FILE_LIMIT_KEY,	"file_limit",		10},
	{GROUP_KEY,		"group",		5},
	{HOLD_KEY,		"hold",			4},
	{IMAGE_SIZE_KEY,	"image_size",		10},
	{INITIAL_DIR_KEY,	"initialdir",		10},
	{INPUT_KEY,		"input",		5},
	{JOB_CPU_LIMIT_KEY,	"job_cpu_limit",	13},
	{JOB_NAME_KEY,		"job_name",		8},
	{JOB_TYPE_KEY,		"job_type",		8},
	{MAX_PROCESSORS_KEY,	"max_processors",	14},
	{MIN_PROCESSORS_KEY,	"min_processors",	14},
	{NETWORK_KEYM,		"network.mpi",		11},
	{NETWORK_KEYL,		"network.lapi",		11},
	{NETWORK_KEYML,		"network.mpi_lapi",	11},
	{NODE_KEY,		"node",			4},
	{NODE_USAGE_KEY,	"node_usage",		10},
	{NOTIFICATION_KEY,	"notification",		12},
	{NOTIFY_USER_KEY,	"notify_user",		11},
	{OUTPUT_KEY,		"output",		6},
	{PARALLEL_PATH_KEY,	"parallel_path",	13},
	{PREFERENCES_KEY,	"preferences",		11},
	{QUEUE_KEY,		"queue",		5},
	{REQUIREMENTS_KEY,	"requirements",		12},
	{RESOURCES_KEY,		"resources",		9},
	{RESTART_KEY,		"restart",		7},
	{RSS_LIMIT_KEY,		"rss_limit",		9},
	{SHELL_KEY,		"shell",		5},
	{STACK_LIMIT_KEY,	"stack_limit",		11},
	{START_DATE_KEY,	"startdate",		9},
	{STEP_NAME_KEY,		"step_name",		9},
	{TASK_GEOMETRY_KEY,	"task_geometry",	13},
	{TASKS_PER_NODE_KEY,	"tasks_per_node",	14},
	{TOTAL_TASKS_KEY,	"total_tasks",		11},
	{USER_PRIORITY_KEY,	"user_priority",	13},
	{WALL_CLOCK_LIMIT_KEY,	"wall_clock_limit",	16},
	{BULKXFER_KEY,	        "bulkxfer",	        16},
	{INVALID_KEY,		NULL,			0}
};

static enum cmd_keys  _get_key_enum(char *key_str);
static char *         _key_to_str(enum cmd_keys key);
static void           _parse_requirements(job_desc_msg_t *slurm_job_ptr, char *val);
static int            _parse_string(char *ll_str, enum cmd_keys *key, 
				char **val);
static int            _process_key(slurm_elem_t * elem, enum cmd_keys key, char *val);
static int            _validate_job(slurm_elem_t *elem);

extern int ll_parse_string(LL_element *jobmgmtObj, char *jobstring, 
		LL_element ** job, int job_version, char *llpp_parms, 
		LL_element **error_object)
{
	slurm_elem_t *slurm_elem = (slurm_elem_t *) jobmgmtObj;
	enum cmd_keys key;
	char *jcl_file = NULL, *line_beg, *line_end, *val;
	int rc = 0;
	bool network_string = false;

	VERBOSE("++++++++++++++++++++++++++++++++++++++++++++++++++\n");
	VERBOSE("ll_parse_string\n");

	if (jobmgmtObj == NULL) {
		ERROR("jobmgmtObj == NULL\n");
		rc = -5;
		goto done;
	}
	if (jobstring == NULL) {
		ERROR("jobstring == NULL\n");
		rc = -5;
		goto done;
	}
	if (job == NULL) {
		ERROR("job == NULL\n");
		rc = -5;
		goto done;
	}

	/* llpp_parms may be NULL. Not an error */

	if (error_object == NULL) {
		ERROR("error_object == NULL\n");
		rc = -5;
		goto done;
	}

	if (slurm_elem->type != JOB_INIT) {
		ERROR("invalid elem type = %s\n",
			elem_name(slurm_elem->type));
		rc = -5;
		goto done;
	}

	VERBOSE("elem=%s spec=\n%s\n%s%s\n", 
		elem_name(slurm_elem->type), 
			SEP_STRING, jobstring, SEP_STRING);

	/* Process the command deck one line at a time */
	jcl_file = strdup(jobstring);
	line_beg = jcl_file;
	do {
		int i;
		for (i=0; ; i++) {
			if (line_beg[i] == '\0') {
				line_end = NULL;
				break;
			}
			if (line_beg[i] == '\n') {
				line_end = &line_beg[i];
				line_beg[i] = '\0';
				break;
			}
		}

		if (_parse_string(line_beg, &key, &val) < 0) {
			rc = -5;
			goto done;
		}

		if (key == INVALID_KEY) {
			ERROR(
				"invalid jobstring key specified %s\n", 
				line_beg);
			rc = -5;
			goto done;
		}

		if (key == NETWORK_KEYM
		    || key == NETWORK_KEYL
		    || key == NETWORK_KEYML)
			network_string = true;
		
		if (_process_key(slurm_elem, key, val) < 0) {
			ERROR("invalid llpp_parms value "
				"specified %s\n", line_beg);
			rc = -5;
			goto done;
		}
		
		line_beg = line_end + 1;
	} while (line_end && (line_beg[0] != '\0'));

	/* Handle when poe fails to specify a network string */
	if (!network_string) {
		_process_key(slurm_elem, NETWORK_KEYM, "not specified, ,ip");
	}

	if (_validate_job(slurm_elem) < 0) {
		rc = -5;
		goto done;
	}

	/* Normal exit through here */
	*job = (LL_element *) slurm_elem;

done:	if (jcl_file)
		free(jcl_file);
	VERBOSE("--------------------------------------------------\n");
	return rc;
}

/* return 0 if job element is valid, <0 otherwise */
static int _validate_job(slurm_elem_t *elem)
{
	slurm_job_init_t *slurm_job_data = (slurm_job_init_t *) elem->data;
	job_desc_msg_t *slurm_job_ptr = slurm_job_data->slurm_job_desc;

	VERBOSE("poe_session = %d\n", poe_session);
	if (poe_session != INTERACTIVE_HOSTLIST_SESSION) {
		if (slurm_job_ptr->partition == NULL) {
			ERROR("No pool name specified\n");
			return -1;
		}

		if ((slurm_job_ptr->min_nodes < 1)  &&
		    (slurm_job_ptr->num_tasks < 1)) {
			ERROR("Invalid node/task count specified: "
			      "%d/%d\n", slurm_job_ptr->min_nodes, 
			      slurm_job_ptr->num_tasks);
			return -1;
		}
	}
	return 0;
}
	

/* Given a LL string to parse (ll_str), determine the key (key) and 
 * its value (va). Return 0 if no error. 
 * NOTE: ll_str is modified. */
static int _parse_string(char *ll_str, enum cmd_keys *key, char **val)
{
	char *key_ptr = NULL, *val_ptr = NULL;
	int i, j;

	if ((ll_str == NULL) || (key == NULL) || (val == NULL)) {
		ERROR("_parse_string argument NULL\n");
		return -1;
	}

	if ((ll_str[0] != '#') || (ll_str[1] != '@')) {
		ERROR("bad argument: %s\n", ll_str);
		return -1;
	}

	/* locate the key */
	for (i=2; ; i++) {
		if (isspace(ll_str[i]))
			continue;
		if (ll_str[i] == '\0') {
			ERROR("bad argument: %s\n", ll_str);
			return -1;
		}
		if (ll_str[i] != '\0')
			key_ptr = &ll_str[i];
		break;
	}
	/* NULL terminate the key and locate its value */
	for ( ; ; i++) {
		if ((ll_str[i] == '\0') || (ll_str[i] == '\n'))
			break;
		if (isspace(ll_str[i])) {
			ll_str[i] = '\0'; 
			continue;
		}
		if (ll_str[i] != '=')
			continue;
		for (j=1; ; j++) {
			if (isspace(ll_str[i+j]))
				continue;
			val_ptr = &ll_str[i+j];
			break;
		}
		break;
	}

	*key = _get_key_enum(key_ptr);
	if (val_ptr) {
		char *tmp = strdup(val_ptr);
		for (i=0; ; i++) {
			if ((tmp[i] != '\n') && (tmp[i] != '\0'))
				continue;
			tmp[i] = '\0';
			break;
		}
		*val = tmp;
	} else
		*val = NULL;

	return 0;
}

enum cmd_keys _get_key_enum(char *key_str)
{
	int i;

	for (i=0; key_table[i].key_string; i++) {
		if (strcasecmp(key_str, key_table[i].key_string) == 0)
			return key_table[i].key_number;
	}

	ERROR("currently unsupported key: %s\n", key_str);
	return INVALID_KEY;
}

/* update job descriptor with JCL info, return 0 on success, <0 on error */
static int _process_key(slurm_elem_t *elem, enum cmd_keys key, char *val)
{
	slurm_job_init_t *slurm_job_data = (slurm_job_init_t *)
		 	elem->data;
	job_desc_msg_t *slurm_job_ptr = slurm_job_data->slurm_job_desc;
	int nodes, tasks;
	static int steps_queued = 0;
	char *endptr;
	
	switch (key) {
	case QUEUE_KEY:
		if (steps_queued++ > 0) {
			ERROR("Multiple steps queued\n");
			return -1;
		}
		return 0;
	case ERROR_KEY:
		slurm_job_ptr->err = strdup(val);
		break;
	case INITIAL_DIR_KEY:
		slurm_job_ptr->work_dir = strdup(val);
		break;
	case INPUT_KEY:
		slurm_job_ptr->in = strdup(val);
		break;
	case JOB_NAME_KEY:
		slurm_job_ptr->name = strdup(val);
		break;
	case MAX_PROCESSORS_KEY:
		nodes = atoi(val);
		if (nodes <= 0) {
			ERROR("invalid node count %d\n",
			      nodes);
			return -1;
		}
		slurm_job_ptr->max_nodes = nodes;
		break;
	case MIN_PROCESSORS_KEY:
		nodes = atoi(val);
		if (nodes <= 0) {
			ERROR("invalid node count %d\n",
			      nodes);
			return -1;
		}
		slurm_job_ptr->min_nodes = nodes;
		break;
	case NODE_KEY:
		/* format is "node = [min][,max]" */
		nodes = strtol(val, &endptr, 10);
		if ((nodes == 0) && (val == endptr))
			nodes = 1;
		else if (nodes <= 0) {
			ERROR("invalid node count %d\n",
			      nodes);
			return -1;
		}
		slurm_job_ptr->min_nodes = nodes;
		break;
	case NODE_USAGE_KEY:
		if (strcasecmp(val, "not_shared") == 0)
			slurm_job_ptr->shared = 0;
		else
			slurm_job_ptr->shared = 1;
		break;
	case OUTPUT_KEY:
		slurm_job_ptr->out = strdup(val);
		break;
	case TOTAL_TASKS_KEY:
		tasks = atoi(val);
		if (tasks <= 0) {
			ERROR("invalid task count %d\n",
			      tasks);
			return -1;
		}
		/* NOTE: The task count is not equivalent to the 
		 * processor count */
		slurm_job_ptr->num_tasks = tasks;
		break;
	case TASKS_PER_NODE_KEY:
		tasks = atoi(val) * slurm_job_ptr->min_nodes;
		if (tasks <= 0) {
			ERROR("invalid task count %d\n",
			      tasks);
			return -1;
		}
		/* NOTE: The task count is not equivalent to the
		 * processor count */
		slurm_job_ptr->num_tasks = tasks;
		break;
	case WALL_CLOCK_LIMIT_KEY:
		ERROR("Need parsing function here\n");
		break;
	case BULKXFER_KEY:
		if (!strcasecmp(val, "yes")) {
			slurm_job_data->bulk_xfer = 1;
			endptr = slurm_job_ptr->network;
			slurm_job_ptr->network =
				(char *) malloc(strlen(endptr) + 11);
			sprintf(slurm_job_ptr->network,"%s,bulk_xfer", endptr);
			free(endptr);
		} else {
			slurm_job_data->bulk_xfer = 0;
		}

	
		break;
	case REQUIREMENTS_KEY:
		_parse_requirements(slurm_job_ptr, val);
		break;
	case ACCOUNT_NO_KEY:	/* SLURM does not support */
		slurm_job_ptr->account = val;
		break;
	case NETWORK_KEYM:
	case NETWORK_KEYL:
	case NETWORK_KEYML:
		slurm_job_ptr->network = (char *) malloc(strlen(_key_to_str(key)) + strlen(val) + 2);
		sprintf(slurm_job_ptr->network,"%s,%s",_key_to_str(key), val);
		break;
	case BLOCKING_KEY:
		if (atoi(val) == 1)
			slurm_job_data->task_dist = SLURM_DIST_CYCLIC;
		else
			slurm_job_data->task_dist = SLURM_DIST_BLOCK;
		break;
	case JOB_TYPE_KEY:	/* SLURM does not support */
	case CLASS_KEY:		/* SLURM does not support */
	case ENVIRONMENT_KEY:	/* SLURM always does COPY_ALL */
	case STEP_NAME_KEY:	/* SLURM names jobs, not steps */
		/* These options are not relevent to SLURM */
		break;
	default:
		ERROR("unsupported job option '%s'\n", 
		      _key_to_str(key));
		return 0;
	}

	VERBOSE("set '%s' to '%s'\n", _key_to_str(key), val);
	return 0;
}

static char *_find_feature(char *val)
{
	int i, flag = 0;
	char *tmp = NULL;

	/* Find "Feature" */
	for (i=0; val[i] ;i++) {
		if (strncmp(&val[i], "Feature", 7))
			continue;
		flag = 1;
		i += 7;
		break;
	}
	if (flag < 1)
		return NULL;

	/* Find "==" */
	for ( ; val[i]; i++) {
		if (isspace(val[i]))
			continue;
		if (strncmp(&val[i], "==", 2))
			break;
		flag = 2;
		i += 2;
		break;
	}
	if (flag < 2)
		return NULL;

	/* Find first quote */
	for ( ; val[i]; i++) {
		if (isspace(val[i]))
			continue;
		if (val[i] != '"')
			break;
		flag = 3;
		i += 1;
		break;
	}
	if (flag < 3)
		return NULL;

	/* Copy the partition name */
	tmp = strdup(&val[i]);

	/* Remove trailing quote */
	for (i=0; tmp[i]; i++) {
		if (tmp[i] != '"')
			continue;
		tmp[i] = '\0';
		break;
	}
	return tmp;
}

static char *_find_pool(char *val)
{
	int i, flag = 0;
	char *tmp = NULL;

	/* Find "Feature" */
	for (i=0; val[i] ;i++) {
		if (strncmp(&val[i], "Pool", 4))
			continue;
		flag = 1;
		i += 4;
		break;
	}
	if (flag < 1)
		return NULL;

	/* Find "==" */
	for ( ; val[i]; i++) {
		if (isspace(val[i]))
			continue;
		if (strncmp(&val[i], "==", 2))
			break;
		flag = 2;
		i += 2;
		break;
	}
	if (flag < 2)
		return NULL;

	/* Skip whitespace */
	for ( ; val[i]; i++) {
		if (isspace(val[i]))
			continue;
		break;
	}

	/* Copy the partition name */
	tmp = strdup(&val[i]);

	/* Remove trailing parenthesis */
	for (i=0; tmp[i]; i++) {
		if (tmp[i] != ')')
			continue;
		tmp[i] = '\0';
		break;
	}
	return tmp;
}

/* Get a partition name, "Feature" if available. 
   Ignore other options for now. */
static void _parse_requirements(job_desc_msg_t *slurm_job_ptr, char *val)
{
	char *tmp = NULL;

	tmp = _find_feature(val);
	if (tmp == NULL)
		tmp = _find_pool(val);
	if (tmp == NULL)
		return;
	
	VERBOSE("setting partition to '%s'\n", tmp);
	slurm_job_ptr->partition = tmp;
}

static char *_key_to_str(enum cmd_keys key)
{
	int i;

	for (i=0; key_table[i].key_string; i++) {
		if (key == key_table[i].key_number)
			return key_table[i].key_string;
	}
	return "UNKNOWN";
}
