/*****************************************************************************\
 *  bnr.c - SLURM implemenation of BNR interface, based upon
 *  Interfacing Parallel Jobs to Process Managers
 *  Brian Toonen, et. al.
 *
 *  http://csdl.computer.org/comp/proceedings/hpdc/2001/1296/00/12960431abs.htm
 *  http://www-unix.globus.org/mail_archive/mpich-g/2001/Archive/ps00000.ps
 *****************************************************************************
 *  As this is a derivative work, we include copyright notice for MPICH 
 *  below.  It is followed by the SLURM/LLNL copyright notice for this 
 *  specific implementation of BNR.
 *****************************************************************************
 *                                  COPYRIGHT
 *
 *  The following is a notice of limited availability of the code, and disclaimer
 *  which must be included in the prologue of the code and in all source listings
 *  of the code.
 *
 *  Copyright Notice
 *  + 1993 University of Chicago
 *  + 1993 Mississippi State University
 *
 *  Permission is hereby granted to use, reproduce, prepare derivative works, and
 *  to redistribute to others.  This software was authored by:
 *
 *  Argonne National Laboratory Group
 *  W. Gropp: (630) 252-4318; FAX: (630) 252-5986; e-mail: gropp@mcs.anl.gov
 *  E. Lusk:  (630) 252-7852; FAX: (630) 252-5986; e-mail: lusk@mcs.anl.gov
 *  Mathematics and Computer Science Division
 *  Argonne National Laboratory, Argonne IL 60439
 *
 *  Mississippi State Group
 *  N. Doss:  (601) 325-2565; FAX: (601) 325-7692; e-mail: doss@erc.msstate.edu
 *  A. Skjellum:(601) 325-8435; FAX: (601) 325-8997; e-mail: tony@erc.msstate.edu
 *  Mississippi State University, Computer Science Department &
 *   NSF Engineering Research Center for Computational Field Simulation
 *  P.O. Box 6176, Mississippi State MS 39762
 *
 *                              GOVERNMENT LICENSE
 *
 *  Portions of this material resulted from work developed under a U.S.
 *  Government Contract and are subject to the following license: the Government
 *  is granted for itself and others acting on its behalf a paid-up, nonexclusive,
 *  irrevocable worldwide license in this computer software to reproduce, prepare
 *  derivative works, and perform publicly and display publicly.
 * 
 *                                   DISCLAIMER
 *
 *  This computer code material was prepared, in part, as an account of work
 *  sponsored by an agency of the United States Government.  Neither the United
 *  States, nor the University of Chicago, nor Mississippi State University, nor
 *  any of their employees, makes any warranty express or implied, or assumes any
 *  legal liability or responsibility for the accuracy, completeness, or
 *  usefulness of any information, apparatus, product, or process disclosed, or
 *  represents that its use would not infringe privately owned rights.
 *****************************************************************************
 *  Copyright (C) 2003 The Regents of the University of California.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Moe Jette <jette1@llnl.gov>, et. al
 *  UCRL-CODE-2002-040.
 *
 *  This file is part of SLURM, a resource management program.
 *  For details, see <http://www.llnl.gov/linux/slurm/>.
 *
 *  SLURM is free software; you can redistribute it and/or modify it under
 *  the terms of the GNU General Public License as published by the Free
 *  Software Foundation; either version 2 of the License, or (at your option)
 *  any later version.
 *
 *  SLURM is distributed in the hope that it will be useful, but WITHOUT ANY
 *  WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 *  FOR A PARTICULAR PURPOSE.  See the GNU General Public License for more
 *  details.
 *
 *  You should have received a copy of the GNU General Public License along
 *  with SLURM; if not, write to the Free Software Foundation, Inc.,
 *  59 Temple Place, Suite 330, Boston, MA  02111-1307  USA.
\*****************************************************************************/

#if HAVE_CONFIG_H
#  include "config.h"
#endif

#ifdef WITH_PTHREADS
#  include <pthread.h>
#endif

#include <limits.h>
#include <stdlib.h>
#include <string.h>

#include <slurm/bnr.h>

#include "src/common/macros.h"
#include "src/common/xmalloc.h"
#include "src/common/xstring.h"

#define BNR_MAX_GROUPS	256

struct bnr_group_info {
	int	active;
	int	my_rank;
	int	nprocs;
	int	keys;
	char **	attr;
	char **	val;
};
struct bnr_group_info bnr_groups[BNR_MAX_GROUPS]; 

static pthread_mutex_t      bnr_lock = PTHREAD_MUTEX_INITIALIZER;

int BNR_Init(BNR_gid *mygid)
{
	int   group = -1, i;
	char *my_rank_char, *nprocs_char;

	slurm_mutex_lock(&bnr_lock);

	/* get a group id */
	for (i=0; i<BNR_MAX_GROUPS; i++) {
		if (bnr_groups[i].active)
			continue;
		group = i;
		break;
	}
	if (group < 0) {
		slurm_mutex_unlock(&bnr_lock);
		fprintf(stderr, "Exhausted supply of BNR groups\n");
		return BNR_ERROR;
	}

	/* get the rank */
	bnr_groups[group].my_rank = -1;
	my_rank_char = getenv("SLURM_PROCID");
	if (my_rank_char) {
		long my_rank_long;
		char *end_ptr;
		my_rank_long = strtol(my_rank_char, &end_ptr, 10);
		if ( (my_rank_long != LONG_MAX) &&
		     (my_rank_long != LONG_MIN) &&
		     (end_ptr[0] == '\0') )
			bnr_groups[group].my_rank = my_rank_long;
	}
	if (bnr_groups[group].my_rank < 0) {
		slurm_mutex_unlock(&bnr_lock);
		fprintf(stderr, "SLURM_PROCID environment variable not set\n");
		return BNR_ERROR;
	}

	/* get the number of tasks */
	bnr_groups[group].nprocs = -1;
	nprocs_char = getenv("SLURM_NPROCS");
	if (nprocs_char) {
		long nprocs_long;
		char *end_ptr;
		nprocs_long = strtol(nprocs_char, &end_ptr, 10);
		if ( (nprocs_long != LONG_MAX) &&
		     (nprocs_long != LONG_MIN) &&
		     (end_ptr[0] == '\0') )
			bnr_groups[group].nprocs = nprocs_long;
	}
	if (bnr_groups[group].nprocs < 0) {
		slurm_mutex_unlock(&bnr_lock);
		fprintf(stderr, "SLURM_NPROCS environment variable not set\n");
		return BNR_ERROR;
	}

	bnr_groups[group].active = 1;
	*mygid = group;
	slurm_mutex_unlock(&bnr_lock);
	return BNR_SUCCESS;
}

int BNR_Put(BNR_gid gid, char *attr, char *val)
{
	int i;

	slurm_mutex_lock(&bnr_lock);

	if ( (gid < 0) || (gid >= BNR_MAX_GROUPS) ||
	     (bnr_groups[gid].active == 0) ) {
		slurm_mutex_unlock(&bnr_lock);
		fprintf(stderr, "BNR_Put: Invalid group id %d\n", gid);
		return BNR_ERROR;
	}

	if ( (strlen(attr) > BNR_MAXATTRLEN) ||
	     (strlen(val)  > BNR_MAXVALLEN) ) {
		slurm_mutex_unlock(&bnr_lock);
		fprintf(stderr, "BNR_Put: argument too large\n");
		return BNR_ERROR;
	}

	/* look for duplicate attr */
	for (i=0; i<bnr_groups[gid].keys; i++) {
		if (strcmp(bnr_groups[gid].attr[i], attr))
			continue;
		xfree(bnr_groups[gid].val[i]);
		bnr_groups[gid].val[i] = xstrdup(val);
		slurm_mutex_unlock(&bnr_lock);
		return BNR_SUCCESS;
	}

	/* add new record */
	xrealloc(bnr_groups[gid].attr,
			(sizeof(char *) * (bnr_groups[gid].keys+1)));
	xrealloc(bnr_groups[gid].val, 
			(sizeof(char *) * (bnr_groups[gid].keys+1)));
	bnr_groups[gid].attr[bnr_groups[gid].keys] = xstrdup(attr);
	bnr_groups[gid].val [bnr_groups[gid].keys] = xstrdup(val);
	bnr_groups[gid].keys++;	

	slurm_mutex_unlock(&bnr_lock);
	return BNR_SUCCESS;
}

int BNR_Fence(BNR_gid gid)
{
	slurm_mutex_lock(&bnr_lock);

	if ( (gid < 0) || (gid >= BNR_MAX_GROUPS) ||
	      (bnr_groups[gid].active == 0) ) {
		slurm_mutex_unlock(&bnr_lock);		
		fprintf(stderr, "BNR_Fence: Invalid group id %d\n", gid);
		return BNR_ERROR;
	}

	if (bnr_groups[gid].nprocs == 1) {	/* no other tasks to sync */
		slurm_mutex_unlock(&bnr_lock);
		return BNR_SUCCESS;
	}

	/* upload keypairs to central DB
	 * wait for all tasks to register
	 * download aggregate key pair DB */
	fprintf(stderr, "BNR_Fence code needed here\n");

	slurm_mutex_unlock(&bnr_lock);
	return BNR_SUCCESS;
}

int BNR_Get(BNR_gid gid, char *attr, char *val)
{
	int i;

	slurm_mutex_lock(&bnr_lock);

	if ( (gid < 0) || (gid >= BNR_MAX_GROUPS) ||
	      (bnr_groups[gid].active == 0) ) {
		slurm_mutex_unlock(&bnr_lock);
		fprintf(stderr, "BNR_Get: Invalid group id %d\n", gid);
		return BNR_ERROR;
	}

	for (i=0; i<bnr_groups[gid].keys; i++) {
		if (strcmp(attr, bnr_groups[gid].attr[i]))
			continue;
		strcpy(val, bnr_groups[gid].val[i]);
		slurm_mutex_unlock(&bnr_lock);
		return BNR_SUCCESS;
	}

	slurm_mutex_unlock(&bnr_lock);
	fprintf(stderr, "BNR_GET: No such attr %s\n", attr);
	return BNR_ERROR;
}

int BNR_Finalize()
{
	int i, j;

	slurm_mutex_lock(&bnr_lock);
	for (i=0; i<BNR_MAX_GROUPS; i++) {
		if (bnr_groups[i].active == 0)
			continue;
		for (j=0; j<bnr_groups[i].keys; j++) {
			xfree(bnr_groups[i].attr[i]);
			xfree(bnr_groups[i].val[i]);
		}
		xfree(bnr_groups[i].attr);
		xfree(bnr_groups[i].val);
		bnr_groups[i].active = 0;
	}

	slurm_mutex_unlock(&bnr_lock);
	return BNR_SUCCESS;
}

int BNR_Rank(BNR_gid group, int *myrank)
{
	int rc;

	slurm_mutex_lock(&bnr_lock);
	if ( (group >= 0) && (group < BNR_MAX_GROUPS) &&
	     (bnr_groups[group].active) ) {
		*myrank = bnr_groups[group].my_rank;
		rc = BNR_SUCCESS; 
	} else {
		fprintf(stderr, "BNR_Rank: Invalid group id %d\n", group);
		rc = BNR_ERROR;
	}

	slurm_mutex_unlock(&bnr_lock);
	return rc;
}

int BNR_Nprocs(BNR_gid group, int *nprocs)
{
	int rc;

	slurm_mutex_lock(&bnr_lock);

	if ( (group >= 0) && (group < BNR_MAX_GROUPS) &&
	     (bnr_groups[group].active) ) {
		*nprocs = bnr_groups[group].nprocs;
		rc = BNR_SUCCESS;
	} else {
		fprintf(stderr, "BNR_Nprocsk: Invalid group id %d\n", group);
		rc = BNR_ERROR;
	}

	slurm_mutex_unlock(&bnr_lock);
	return rc;
}
