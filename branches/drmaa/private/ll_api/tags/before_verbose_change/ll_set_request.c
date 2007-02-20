/*****************************************************************************\
 *  ll_set_request.c - Identify the data to be return in subsequent calls to 
 *  ll_get_objs. This function presently supports JOBS data and the 
 *  identification of a single SLURM JOB_ID. It also supports the CLUSTER 
 *  data and QUERY_ALL option. No other data types or filters are supported. 
 *  Also note the DataFilter argument is ignored.
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

#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include "common.h"
#include "config.h"
#include "llapi.h"

static char *		_get_job_id(char **object_filter);
static char *		_query_flag_str(enum QueryFlags query_flags);
static int		_query_cluster(LL_element *query_element,
				enum QueryFlags query_flags, 
				char **object_filter, 
				enum DataFilter data_filter);
static int		_query_job(LL_element *query_element,
				enum QueryFlags query_flags, 
				char **object_filter, 
				enum DataFilter data_filter);
extern int ll_set_request(LL_element *query_element, 
		enum QueryFlags query_flags,
		char **object_filter, enum DataFilter data_filter)
{
	slurm_elem_t *slurm_elem = query_element;
	int rc = 0;

	VERBOSE(stderr, "++++++++++++++++++++++++++++++++++++++++++++++++++\n");
	if (query_element == NULL) {
		ERROR(stderr, "ERROR: ll_set_request: query_element==NULL\n");
		rc = -1;
		goto done;
	}

	VERBOSE(stderr, "ll_set_request: elem=%s, type=%s, flags=%s\n",
			elem_name(slurm_elem->type), 
			query_type_str(slurm_elem->type),
			_query_flag_str(query_flags));

	switch (slurm_elem->type) {
		case JOB_QUERY:
			rc = _query_job(query_element, query_flags, 
				object_filter, data_filter);
			break;
		case CLUSTER_QUERY:
			rc = _query_cluster(query_element, query_flags, 
				object_filter, data_filter);
			break;
		default:
			ERROR(stderr, "ERROR: ll_set_request: type=%s "
				"unsupported\n",
				query_type_str(slurm_elem->type));
			rc = -1;
	}

done:
	VERBOSE(stderr, "--------------------------------------------------\n");
	return rc;
}

static int _query_cluster(LL_element *query_element,
		enum QueryFlags query_flags,
		char **object_filter, enum DataFilter data_filter)
{
	if (query_flags != QUERY_ALL) {
		ERROR(stderr, "ERROR: ll_set_request: flags=%s unsupported\n",
				_query_flag_str(query_flags));
		return -2;
	}

	return 0;
}

static int _query_job(LL_element *query_element,
		enum QueryFlags query_flags,
		char **object_filter, enum DataFilter data_filter)
{
	slurm_elem_t *slurm_elem = query_element;
	slurm_job_query_t *job_query_elem;
	char * job_id;

	if (slurm_elem->data == NULL) {
		ERROR(stderr, "ERROR: ll_set_request: data== NULL\n");
		return -1;
	}
	if (query_flags != QUERY_JOBID) {
		ERROR(stderr, "ERROR: ll_set_request: flags=%s unsupported\n",
				_query_flag_str(query_flags));
		return -2;
	}

	job_id = _get_job_id(object_filter);
	if (job_id == NULL)
		return -3;

	if (!slurm_elem->data) {
		slurm_elem->data = calloc(1, sizeof(slurm_job_query_t));
		if (slurm_elem->data == NULL) {
			ERROR(stderr, "ERROR: calloc failure\n");
			return -5;	/* system error */
		}
	}
	job_query_elem = (slurm_job_query_t *) slurm_elem->data;
	if (job_query_elem->filter)
		free(job_query_elem->filter);
	job_query_elem->filter = job_id;
	return 0;
}

/* return a string containing the one job id in the filter array of 
 * NULL if invalid (e.g. not exactly one entry). The caller must 
 * free() the returned value to avoid a memory leak. */
static char * _get_job_id(char **object_filter)
{
	long int job_id;
	char *end_ptr;

	if (object_filter == (char **)NULL) {
		ERROR(stderr, "ERROR: ll_set_request.c: filter_object is NULL\n");
		return NULL;
	}

	if (object_filter[0] == NULL) {
		ERROR(stderr, "ERROR: ll_set_request.c: filter_object is empty\n");
		return NULL;
	}

	if (object_filter[1] != NULL) {
		ERROR(stderr, "ERROR: ll_set_request.c: filter_object has too "
			"many  entries\n");
		return NULL;
	}

	job_id = strtol(object_filter[0], &end_ptr, 10);
	if (end_ptr || (job_id <= 0) || (job_id == LONG_MIN) || 
			(job_id == LONG_MAX)) {
		ERROR(stderr, "ERROR: ll_set_request.c: filter_object invalid(%s)\n",
			object_filter[0]);
		return NULL;
	}

	return strdup(object_filter[0]); 
}

/* Convert a query_flags number into a string */
static char * _query_flag_str(enum QueryFlags query_flags)
{
	char *rc;

	switch (query_flags) {
		case QUERY_ALL:
			rc = "QUERY_ALL";
			break;
		case QUERY_JOBID:
			rc = "QUERY_JOBID";
			break;
		case QUERY_STEPID:
			rc =  "QUERY_STEPID";
			break;
		case QUERY_USER:
			rc = "QUERY_USER";
			break;
		case QUERY_GROUP:
			rc = "QUERY_GROUP";
			break;
		case QUERY_CLASS:
			rc = "QUERY_CLASS";
			break;
		case QUERY_HOST:
			rc = "QUERY_HOST";
			break;
		default:
			rc = "INVALID";
			break;
	}
	return rc;
}
