/*****************************************************************************\
 *  ll_query.c - Initialize query object, return a structure containin info 
 *  for all JOBS or MACHINES. The MACHINES information is not presently 
 *  supported.
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
#include <slurm/slurm.h>
#include <slurm/slurm_errno.h>

#include "common.h"
#include "config.h"
#include "llapi.h"

static LL_element *	_query_cluster(void);
static LL_element *	_query_jobs(void);
static LL_element *	_query_machines(void);

extern LL_element *ll_query(enum QueryType query_type)
{
	LL_element *rc;

	VERBOSE(stderr, "++++++++++++++++++++++++++++++++++++++++++++++++++\n");
	VERBOSE(stderr, "ll_query: type=%s\n", 
			query_type_str(query_type));

	switch (query_type) {
		case CLUSTERS:
			rc = _query_cluster();
			break;
		case JOBS:
			rc = _query_jobs();
			break;
		case MACHINES:
			rc = _query_machines();
			break;
		default:
			rc = NULL;
			ERROR(stderr, "ERROR: ll_query type bad: %d\n", 
					query_type);
			break;
	}
	VERBOSE(stderr, "--------------------------------------------------\n");
	return rc;
}

static LL_element *_query_cluster(void)
{
	slurm_elem_t      *slurm_elem;
	slurm_cluster_query_t *slurm_cluster_query;

	slurm_elem = calloc(1, sizeof(slurm_elem_t));
	if (slurm_elem == NULL) {
		ERROR(stderr, "ERROR: calloc failure\n");
		return (LL_element *) NULL;
	}
	slurm_cluster_query = calloc(1, sizeof(slurm_cluster_query_t));
	if (slurm_cluster_query == NULL) {
		ERROR(stderr, "ERROR: calloc failure\n");
		free(slurm_elem);
		return (LL_element *) NULL;
	}

	slurm_elem->type = CLUSTER_QUERY;
	slurm_elem->data = slurm_cluster_query;
	return (LL_element *) slurm_elem;
}

static LL_element *_query_jobs(void)
{
	slurm_elem_t      *slurm_elem;

	slurm_elem = calloc(1, sizeof(slurm_elem_t));
	if (slurm_elem == NULL) {
		ERROR(stderr, "ERROR: calloc failure\n");
		return (LL_element *) NULL;
	}

	slurm_elem->type = JOB_QUERY;
	return (LL_element *) slurm_elem;
}

static LL_element *_query_machines(void)
{
	ERROR(stderr, "ERROR: ll_query(MACHINES) not supported\n");
	return (LL_element *) NULL;
}

