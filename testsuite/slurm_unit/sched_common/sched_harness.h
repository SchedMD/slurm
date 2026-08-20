/*****************************************************************************\
 *  sched_harness.h - shared scaffolding for in-process scheduler tests
 *****************************************************************************
 *  Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
\*****************************************************************************/

#ifndef _SCHED_HARNESS_H
#define _SCHED_HARNESS_H

#include "src/slurmctld/slurmctld.h"

/*
 * Bring up enough of a slurmctld to run the scheduler in process: read the
 * config, build the node table, initialize the select and topology plugins
 * and create a single "test" partition spanning every node.
 *
 * The config directory comes from -c, else $srcdir, else the current
 * directory. Call once from main() before running any tests.
 */
extern void sched_harness_init(char *log_name, int argc, char **argv);

/* Release what sched_harness_init() allocated. */
extern void sched_harness_fini(void);

/* True if -t was given, meaning run as an emulator rather than a test suite. */
extern bool sched_harness_emulating(void);

/*
 * Add a pending job to job_list. Returns the record so callers can set fields
 * these arguments do not cover, such as details->contiguous.
 */
extern job_record_t *__add_job(uint32_t job_id, uint32_t priority,
			       uint32_t nodes, uint32_t num_tasks,
			       uint16_t segment_size, uint32_t time_limit,
			       char *licenses);

/* Add one job per line of the -t job file. */
extern void sched_harness_load_test(void);

/* list_for_each() callback printing a job's placement. arg is a uint32_t *now. */
extern int sched_harness_print_job(void *x, void *arg);

#endif /* _SCHED_HARNESS_H */
