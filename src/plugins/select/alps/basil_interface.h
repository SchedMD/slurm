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

#ifdef WITH_PTHREADS
#  include <pthread.h>
#endif				/* WITH_PTHREADS */

#include "slurm/slurm.h"
#include "slurm/slurm_errno.h"

#include "src/common/log.h"
#include "src/common/uid.h"
#include "src/common/xstring.h"
#include "src/common/node_select.h"
#include "src/slurmctld/slurmctld.h"

extern int dim_size[3];
extern int inv_interval;

/**
 * struct select_jobinfo - data specific to Cray node selection plugin
 * @magic:		magic number, must equal %JOBINFO_MAGIC
 * @reservation_id:	ALPS reservation ID, assigned upon creation
 * @confirm_cookie:	cluster-wide unique container identifier to
 *			confirm the ALPS reservation. Should best use
 *			SGI process aggregate IDs since session IDs
 *			are not unique across multiple nodes.
 * @other_jobinfo:	hook into attached, "other" node selection plugin.
 */
struct select_jobinfo {
	uint16_t		magic;
	uint8_t			confirmed;
	uint32_t		reservation_id;
	uint64_t		confirm_cookie;
	select_jobinfo_t	*other_jobinfo;
};
#define JOBINFO_MAGIC		0x8cb3

/**
 * struct select_nodeinfo - data used for node information
 * @magic:		magic number, must equal %NODEINFO_MAGIC
 * @other_nodeinfo:	hook into attached, "other" node selection plugin.
 */
struct select_nodeinfo {
	uint16_t		magic;
	select_nodeinfo_t	*other_nodeinfo;
};
#define NODEINFO_MAGIC		0x82a3

#ifdef HAVE_ALPS_CRAY
extern int basil_node_ranking(struct node_record *node_array, int node_cnt);
extern int basil_inventory(void);
extern int basil_geometry(struct node_record *node_ptr_array, int node_cnt);
extern int do_basil_reserve(struct job_record *job_ptr);
extern int do_basil_confirm(struct job_record *job_ptr);
extern int do_basil_signal(struct job_record *job_ptr, int signal);
extern int do_basil_release(struct job_record *job_ptr);
extern int do_basil_switch(struct job_record *job_ptr, bool suspend);
extern void queue_basil_signal(struct job_record *job_ptr, int signal,
			       uint16_t delay);
#else	/* !HAVE_ALPS_CRAY */
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

static inline int do_basil_signal(struct job_record *job_ptr, int signal)
{
	return SLURM_SUCCESS;
}

static inline void queue_basil_signal(struct job_record *job_ptr, int signal,
				      uint16_t delay)
{
	return;
}

static inline int do_basil_release(struct job_record *job_ptr)
{
	return SLURM_SUCCESS;
}

static inline int do_basil_switch(struct job_record *job_ptr, bool suspend)
{
	return SLURM_SUCCESS;
}

#endif	/* HAVE_ALPS_CRAY */
#endif	/* __CRAY_BASIL_INTERFACE_H */
