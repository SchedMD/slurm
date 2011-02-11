/*
 * Interface between lower-level ALPS XML-RPC library functions and SLURM.
 *
 * Copyright (c) 2010-11 Centro Svizzero di Calcolo Scientifico (CSCS)
 * Licensed under GPLv2.
 */
#ifndef __CRAY_BASIL_INTERFACE_H
#define __CRAY_BASIL_INTERFACE_H

#if HAVE_CONFIG_H
# include "config.h"
#endif	/* HAVE_CONFIG_H */

#include <slurm/slurm.h>
#include <slurm/slurm_errno.h>

#include "src/common/log.h"
#include "src/common/uid.h"
#include "src/common/xstring.h"
#include "src/common/node_select.h"
#include "src/slurmctld/slurmctld.h"

extern int dim_size[3];

struct select_jobinfo {
	uint16_t		magic;		/* magic number */
	uint32_t		reservation_id;	/* BASIL reservation ID */
	select_jobinfo_t	*other_jobinfo;
};
#define JOBINFO_MAGIC		0x8cb3

struct select_nodeinfo {
	uint16_t		magic;		/* magic number */
	select_nodeinfo_t	*other_nodeinfo;
};
#define NODEINFO_MAGIC		0x82a3

#ifdef HAVE_CRAY
extern int basil_node_ranking(struct node_record *node_array, int node_cnt);
extern int basil_inventory(void);
extern int basil_geometry(struct node_record *node_ptr_array, int node_cnt);
extern int do_basil_reserve(struct job_record *job_ptr);
extern int do_basil_confirm(struct job_record *job_ptr);
extern int do_basil_release(struct job_record *job_ptr);
#else	/* !HAVE_CRAY */
static inline int basil_node_ranking(struct node_record *ig, int nore)
{
	return SLURM_SUCCESS;
}
static inline int basil_inventory(void)
{
	return SLURM_SUCCESS;
}

static inline int basil_geometry(struct node_record *ig, int nore)
{
	return SLURM_SUCCESS;
}

static inline int do_basil_reserve(struct job_record *job_ptr)
{
	return SLURM_SUCCESS;
}

static inline int do_basil_confirm(struct job_record *job_ptr)
{
	return SLURM_SUCCESS;
}

static inline int do_basil_release(struct job_record *job_ptr)
{
	return SLURM_SUCCESS;
}
#endif	/* HAVE_CRAY */
#endif	/* __CRAY_BASIL_INTERFACE_H */
