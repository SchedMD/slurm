/*
 * Interface between lower-level ALPS XML-RPC functions and SLURM.
 *
 * Copyright (c) 2010-11 Centro Svizzero di Calcolo Scientifico (CSCS)
 * Licensed under GPLv2.
 */
#ifndef __CRAY_BASIL_INTERFACE_H
#define __CRAY_BASIL_INTERFACE_H

#if HAVE_CONFIG_H
#  include "config.h"
#endif	/* HAVE_CONFIG_H */

#include <slurm/slurm.h>
#include <slurm/slurm_errno.h>

#include "src/common/log.h"
#include "src/common/uid.h"
#include "src/common/xstring.h"
#include "src/common/node_select.h"
#include "src/slurmctld/slurmctld.h"

extern int basil_inventory(void);
extern int do_basil_reserve(struct job_record *job_ptr);
extern int do_basil_confirm(struct job_record *job_ptr);
extern int do_basil_release(struct job_record *job_ptr);

#endif /* __CRAY_BASIL_INTERFACE_H */
