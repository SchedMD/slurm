/*****************************************************************************\
 * src/slurmd/slurmd/slurmd.h - header for slurmd
 *****************************************************************************
 *  Copyright (C) 2002-2007 The Regents of the University of California.
 *  Copyright (C) 2008-2010 Lawrence Livermore National Security.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Mark Grondona <mgrondona@llnl.gov>.
 *  CODE-OCEC-09-009. All rights reserved.
 *
 *  This file is part of Slurm, a resource management program.
 *  For details, see <https://slurm.schedmd.com/>.
 *  Please also read the included file: DISCLAIMER.
 *
 *  Slurm is free software; you can redistribute it and/or modify it under
 *  the terms of the GNU General Public License as published by the Free
 *  Software Foundation; either version 2 of the License, or (at your option)
 *  any later version.
 *
 *  In addition, as a special exception, the copyright holders give permission
 *  to link the code of portions of this program with the OpenSSL library under
 *  certain conditions as described in each individual source file, and
 *  distribute linked combinations including the two. You must obey the GNU
 *  General Public License in all respects for all of the code used other than
 *  OpenSSL. If you modify file(s) with this exception, you may extend this
 *  exception to your version of the file(s), but you are not obligated to do
 *  so. If you do not wish to do so, delete this exception statement from your
 *  version.  If you delete this exception statement from all source files in
 *  the program, then also delete it here.
 *
 *  Slurm is distributed in the hope that it will be useful, but WITHOUT ANY
 *  WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 *  FOR A PARTICULAR PURPOSE.  See the GNU General Public License for more
 *  details.
 *
 *  You should have received a copy of the GNU General Public License along
 *  with Slurm; if not, write to the Free Software Foundation, Inc.,
 *  51 Franklin Street, Fifth Floor, Boston, MA 02110-1301  USA.
\*****************************************************************************/
#ifndef _SLURMD_H
#define _SLURMD_H

#include <inttypes.h>
#include <limits.h>
#include <pthread.h>
#include <sys/types.h>

#include "src/common/log.h"
#include "src/common/list.h"
#include "src/common/slurm_protocol_api.h"
#include "src/interfaces/cred.h"

#define MAX_CPU_CNT 1024
#define STEPD_OOM_ADJ -999

extern int devnull;
extern bool get_reg_resp;
extern bool refresh_cached_features;
extern pthread_mutex_t cached_features_mutex;

typedef struct {
	slurm_step_id_t step_id;
	uint32_t derived_ec;
	uint32_t exit_code;
	char **gres_job_env;
	uint32_t het_job_id;
	char *node_aliases;
	char *node_list;
	char *partition;
	char **spank_job_env;
	uint32_t spank_job_env_size;
	uid_t uid;
	gid_t gid;
	char *work_dir;
} job_env_t;

/*
 * Global config type
 */
typedef struct slurmd_config {
	char         *prog;		/* Program basename		   */
	char         **argv;            /* argument vector                 */
	int          argc;              /* argument count                  */
	char binary[PATH_MAX];		/* path to slurmd                  */
	buf_t        *buf;              /* packed version of this lite config */
	char         *hostname;	 	/* local hostname		   */
	char         *conf_server;	/* slurmctld to fetch config from  */
	char         *conf_cache;	/* cache of slurm configs          */
					/* (if running configless)         */
	uint16_t     cpus;              /* lowest-level logical processors */
	uint16_t     boards;            /* total boards count              */
	uint16_t     sockets;           /* total sockets count             */
	uint16_t     threads;           /* thread per core count           */
	char         *cpu_spec_list;    /* cpu specialization list         */
	uint16_t     core_spec_cnt;     /* core specialization count       */
	uint64_t     mem_spec_limit;    /* memory specialization limit     */
	uint16_t     cores;             /* core per socket  count          */
	uint16_t     conf_cpus;         /* conf file logical processors    */
	uint16_t     conf_boards;       /* conf file boards count          */
	uint16_t     conf_sockets;      /* conf file sockets count         */
	uint16_t     conf_cores;        /* conf file core count            */
	uint16_t     conf_threads;      /* conf file thread per core count */
	uint16_t     actual_cpus;       /* actual logical processors       */
	uint16_t     actual_boards;     /* actual boards count             */
	uint16_t     actual_sockets;    /* actual sockets count            */
	uint16_t     actual_cores;      /* actual core count               */
	uint16_t     actual_threads;    /* actual thread per core count    */
	uint64_t     conf_memory_size;  /* amount of configured memory from
					 * slurm.conf */
	uint64_t     physical_memory_size; /* amount of physical memory
					    * on the node */
	uint32_t     tmp_disk_space;    /* size of temporary disk	   */
	uint32_t     up_time;		/* seconds since last boot time    */
	uint16_t     block_map_size;	/* size of block map               */
	uint16_t     *block_map;	/* abstract->machine block map     */
	uint16_t     *block_map_inv;	/* machine->abstract (inverse) map */
	int           nice;		/* command line nice value spec    */
	char         *node_name;	/* node name                       */
	char         *node_topo_addr;   /* node's topology address         */
	char         *node_topo_pattern;/* node's topology address pattern */
	char         *extra;		/* arbiratry string		   */
	char         *conffile;		/* config filename                 */
	char         *instance_id;	/* cloud instance id		   */
	char         *instance_type;	/* cloud instance type		   */
	char         *logfile;		/* slurmd logfile, if any          */
	char 	     *parameters;	/* additions to SlurmdParameters */
	char         *pidfile;		/* slurmd pidfile, if any          */
	char         *tmp_fs;		/* TmpFS                           */
	uint32_t     syslog_debug;	/* send output to both logfile and
					 * syslog */
	char         *spooldir;		/* SlurmdSpoolDir		   */
	char         *stepd_loc;	/* slurmstepd path                 */
	uint16_t      port;		/* local slurmd port               */
	int           lfd;		/* slurmd listen file descriptor   */
	pid_t         pid;		/* server pid                      */
	log_options_t log_opts;         /* current logging options         */
	uint32_t      debug_level;	/* logging detail level            */
	uint16_t      debug_level_set;	/* debug_level set on command line */
	time_t	      boot_time;	/* Use this as node boot time if set */
	bool	      daemonize;	/* daemonize flag (-D)		   */
	bool          setwd;		/* setwd flag (-s)		   */
	bool	      cleanstart;	/* clean start requested (-c)      */
	bool	      mlock_pages;	/* mlock() slurmd  */

	pthread_mutex_t config_mutex;	/* lock for slurmd_config access   */
	uint16_t        acct_freq_task;

	list_t *starting_steps;		/* steps that are starting but cannot
					   receive RPCs yet */
	pthread_cond_t	starting_steps_cond;
	list_t *prolog_running_jobs;
	pthread_cond_t	prolog_running_cond;
	bool		print_gres;	/* Print gres info (-G) and exit */

	uint8_t dynamic_type;		/* Dynamic node type */
	char *dynamic_conf;		/* Dynamic configuration */
	char *dynamic_feature;		/* Dynamic feature to register with */
} slurmd_conf_t;

extern slurmd_conf_t * conf;
extern int fini_job_cnt;
extern uint32_t *fini_job_id;
extern pthread_mutex_t fini_job_mutex;
extern pthread_mutex_t tres_mutex;
extern pthread_cond_t  tres_cond;
extern bool tres_packed;

/* Send node registration message with status to controller
 * IN status - same values slurm error codes (for node shutdown)
 */
int send_registration_msg(uint32_t status);

/* Run the health check program if configured */
int run_script_health_check(void);

/* shutdown the slurmd daemon */
extern void slurmd_shutdown(void);

/* Handler for debug level update */
extern void update_slurmd_logging(log_level_t log_lvl);
extern void update_stepd_logging(bool reconfig);

/*
 * Build a slurmd configuration buffer _once_ for sending to slurmstepd
 * This must happen after all configuration is available, including topology
 */
extern void build_conf_buf(void);

/* True if listener is quiesced */
extern bool listener_quiesced(void);

#endif /* !_SLURMD_H */
