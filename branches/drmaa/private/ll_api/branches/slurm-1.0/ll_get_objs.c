/*****************************************************************************\
 *  ll_get_objs.c - Create an LL_element for a specific object time as 
 *  identified by ll_set_request. We presently only honor a request for 
 *  one specific job id.
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
#include <slurm/slurm.h>

#include "common.h"
#include "config.h"
#include "llapi.h"

static LL_element *	_build_cluster_elem(slurm_elem_t *cluster_query);

extern LL_element *ll_get_objs(LL_element *query_element, 
		enum LL_Daemon query_daemon, char *hostname,
		int *number_of_objs, int *error_code)
{
	LL_element *rc = (LL_element *) NULL;
	slurm_elem_t *slurm_elem = query_element;

	VERBOSE("++++++++++++++++++++++++++++++++++++++++++++++++++\n");
	if (error_code == NULL) {
		ERROR("ll_get_objs: error_code==NULL\n");
		goto done;
	}
	if (number_of_objs == NULL) {
		ERROR("ll_get_objs: number_of_objs==NULL\n");
		*error_code = -7;
		goto done;
	}
	if (query_element == NULL) {
		ERROR("ll_get_objs: query_element==NULL\n");
		*error_code = -1;
		goto done;
	}
	VERBOSE("ll_get_objs: type=%s\n",
			query_type_str(slurm_elem->type));

	switch (slurm_elem->type) {
		case CLUSTER_QUERY:
			rc = _build_cluster_elem(slurm_elem);
			break;
		default:
			ERROR("ll_get_objs: type=%s unsupported\n",
				query_type_str(slurm_elem->type));
			*error_code = -1;
	}

done:
	VERBOSE("--------------------------------------------------\n");
	return (LL_element *) rc;
}

/* Build a CLUSTER_ELEM element, the CLUSTER_QUERY element is unused */
static LL_element * _build_cluster_elem(slurm_elem_t *cluster_query)
{
	slurm_elem_t      *slurm_elem;
	slurm_cluster_query_t *slurm_cluster_query;

	if (cluster_query->data == NULL) {
		ERROR("cluster_query->data == NULL\n");
		return (LL_element *) NULL;
	}
	slurm_cluster_query = cluster_query->data;

	slurm_elem = calloc(1, sizeof(slurm_elem_t));
	if (slurm_elem == NULL) {
		ERROR("calloc failure\n");
		return (LL_element *) NULL;
	}

	slurm_elem->type = CLUSTER_ELEM;
	slurm_cluster_query->cluster_elem = slurm_elem;
	
	return (LL_element *) slurm_elem;
}
