/*****************************************************************************\
 *  ll_deallocate.c - Deallocate an LL_element returned by ll_query().
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

#include <stdio.h>
#include <stdlib.h>

#include "common.h"
#include "config.h"
#include "llapi.h"

static void _deallocate_cluster(slurm_elem_t *slurm_elem);
static void _deallocate_job(slurm_elem_t *slurm_elem);

extern int ll_deallocate(LL_element *query_element)
{
	int rc = 0;
	slurm_elem_t	*slurm_elem = (slurm_elem_t *) query_element;

	VERBOSE(stderr, "++++++++++++++++++++++++++++++++++++++++++++++++++\n");
	VERBOSE(stderr, "ll_deallocate\n");

	if (query_element == NULL) {
		ERROR(stderr, "ERROR: element=NULL\n");
		rc = -1;
		goto done;
	}

	VERBOSE(stderr, "element=%s\n",
			elem_name(slurm_elem->type));

	switch (slurm_elem->type) {
		case CLUSTER_QUERY:
			 _deallocate_cluster(slurm_elem);
			break;
		case JOB_QUERY:
			_deallocate_job(slurm_elem);
			break;
		default:
			rc = -1;
			ERROR(stderr, "ERROR: type=%s\n",
					query_type_str(slurm_elem->type));
	}

done:
	VERBOSE(stderr, "--------------------------------------------------\n");
	return rc;
}

static void _deallocate_cluster(slurm_elem_t *slurm_elem)
{
	if (slurm_elem->data) {
		slurm_cluster_query_t *slurm_cluster_query;

		slurm_cluster_query = (slurm_cluster_query_t *) 
				slurm_elem->data;
		if (slurm_cluster_query->cluster_elem)
			free(slurm_cluster_query->cluster_elem);
		free(slurm_cluster_query->cluster_elem);
	}

	free(slurm_elem);
}

static void _deallocate_job(slurm_elem_t *slurm_elem)
{
	if (slurm_elem->data) {
		slurm_job_query_t *job_query;

		job_query = (slurm_job_query_t *)slurm_elem->data;
		if (job_query->filter)
			free(job_query->filter);
		free(job_query);
	}

	free(slurm_elem);
}
