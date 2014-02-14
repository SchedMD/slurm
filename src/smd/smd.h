/*****************************************************************************\
 *  smd.h - Structures and definitions for fault tolerant application support
 *****************************************************************************
 *  Copyright (C) 2013 SchedMD LLC
 *  Written by Morris Jette <jette@schedmd.com>
 *  Written by David Bigagli <david@schedmd.com>
 *  All rights reserved
\*****************************************************************************/

#include "slurm/smd_ns.h"

struct nonstop_params {
	uint16_t drain;
	uint16_t drop;
	uint16_t env_vars;
	uint16_t extend;
	uint16_t failed;
	uint16_t jinfo;
	uint32_t job_id;
	char *node;
	char *reason;
	uint16_t replace;
	uint16_t sconfig;
	uint16_t verbose;
	char *handle_failed;
	char *handle_failing;
};

/* Global configuration parameters used
 * for both manual and automatic modes.
 */
extern struct nonstop_params *params;

/* These enums describe the env options
 * set by users to determine what behavior
 * they want when a job experiences a failure.
 */
typedef enum {
	failure_type,
	replace,
	drop,
	time_limit_delay,
	time_limit_extend,
	time_limit_drop,
	exit_job
} kvl_t;

typedef enum {
	failed_hosts,
	failing_hosts
} fail_t;


extern int  set_params(int, char **, struct nonstop_params *);
extern int  check_params(struct nonstop_params *);
extern struct key_val **get_key_val(void);
extern void free_params(struct nonstop_params *);
extern int  manual(void);
extern int  automatic(void);
