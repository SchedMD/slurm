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
#include <pthread.h>
#include <sys/types.h>

#include "src/common/log.h"
#include "src/common/list.h"
#include "src/common/slurm_protocol_api.h"
#include "src/common/slurm_cred.h"

#ifndef __USE_XOPEN_EXTENDED
extern pid_t getsid(pid_t pid);		/* missing from <unistd.h> */
extern pid_t getpgid(pid_t pid);
#endif

extern int devnull;
extern bool get_reg_resp;

/*
 * Message aggregation types
 */
typedef enum {
	WINDOW_TIME,
	WINDOW_MSGS
} msg_aggr_param_type_t;

/*
 * Global config type
 */
typedef struct slurmd_config {
	char         *prog;		/* Program basename		   */
	char         ***argv;           /* pointer to argument vector      */
	int          *argc;             /* pointer to argument count       */
	char         *auth_info;	/* AuthInfo for msg authentication */ 
	char         *cluster_name; 	/* conf ClusterName		   */
	char         *hostname;	 	/* local hostname		   */
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
	uint64_t     real_memory_size;  /* amount of real memory	   */
	uint32_t     tmp_disk_space;    /* size of temporary disk	   */
	uint32_t     up_time;		/* seconds since last boot time    */
	uint16_t     block_map_size;	/* size of block map               */
	uint16_t     *block_map;	/* abstract->machine block map     */
	uint16_t     *block_map_inv;	/* machine->abstract (inverse) map */
	uint16_t      cr_type;		/* Consumable Resource Type:       *
					 * CR_SOCKET, CR_CORE, CR_MEMORY,  *
					 * CR_DEFAULT, etc.                */
	char         *hwloc_xml;	/* path of hwloc xml file if using */
	time_t        last_update;	/* last update time of the
					 * build parameters */
	uint16_t      mem_limit_enforce; /* enforce mem limit on running job */
	int           nice;		/* command line nice value spec    */
	char         *node_name;	/* node name                       */
	char         *node_addr;	/* node's address                  */
	char         *node_topo_addr;   /* node's topology address         */
	char         *node_topo_pattern;/* node's topology address pattern */
	char         *conffile;		/* config filename                 */
	char         *logfile;		/* slurmd logfile, if any          */
	int          syslog_debug;	/* send output to both logfile and
					 * syslog */
	char         *spooldir;		/* SlurmdSpoolDir		   */
	char         *pidfile;		/* PidFile location		   */
	char         *health_check_program; /* run on RPC request or at start */
	uint64_t     health_check_interval; /* Interval between runs       */
	char         *tmpfs;		/* directory of tmp FS             */
	char         *pubkey;		/* location of job cred public key */
	char         *epilog;		/* Path to Epilog script	   */
	char         *prolog;		/* Path to prolog script           */
	char         *select_type;	/* SelectType                      */
	char         *stepd_loc;	/* slurmstepd path                 */
	char         *task_prolog;	/* per-task prolog script          */
	char         *task_epilog;	/* per-task epilog script          */
	int           port;		/* local slurmd port               */
	int           lfd;		/* slurmd listen file descriptor   */
	pid_t         pid;		/* server pid                      */
	log_options_t log_opts;         /* current logging options         */
	uint16_t      log_fmt;          /* Log file timestamp format flag  */
	int           debug_level;	/* logging detail level            */
	uint16_t      debug_level_set;	/* debug_level set on command line */
	uint64_t      debug_flags;	/* DebugFlags configured           */
	int	      boot_time:1;      /* Report node boot time now (-b)  */
	int           daemonize:1;	/* daemonize flag (-D)		   */
	bool          def_config;       /* We haven't read in the config yet */
	int	      cleanstart:1;     /* clean start requested (-c)      */
	int           mlock_pages:1;	/* mlock() slurmd  */

	slurm_cred_ctx_t vctx;          /* slurm_cred_t verifier context   */

	uint16_t	slurmd_timeout;	/* SlurmdTimeout                   */
	uid_t           slurm_user_id;	/* UID that slurmctld runs as      */
	pthread_mutex_t config_mutex;	/* lock for slurmd_config access   */
	uint16_t        acct_freq_task;
	char           *job_acct_gather_freq;
	char           *job_acct_gather_type; /* job accounting gather type */
	char           *job_acct_gather_params; /* job accounting gather params */
	char           *acct_gather_energy_type; /*  */
	char           *acct_gather_filesystem_type; /*  */
	char           *acct_gather_interconnect_type; /*  */
	char           *acct_gather_profile_type; /*  */
	char           *msg_aggr_params;      /* message aggregation params */
	uint64_t        msg_aggr_window_msgs; /* msg aggr window size in msgs */
	uint64_t        msg_aggr_window_time; /* msg aggr window size in time */
	uint16_t	use_pam;
	uint32_t	task_plugin_param; /* TaskPluginParams, expressed
					 * using cpu_bind_type_t flags */
	uint16_t	propagate_prio;	/* PropagatePrioProcess flag       */

	List		starting_steps; /* steps that are starting but cannot
					   receive RPCs yet */
	pthread_cond_t	starting_steps_cond;
	List		prolog_running_jobs;
	pthread_cond_t	prolog_running_cond;
	char         *plugstack;	/* path to SPANK config file	*/
	uint16_t      kill_wait;	/* seconds between SIGXCPU to SIGKILL
					 * on job termination */
	char           *x11_params;	/* X11Parameters */
} slurmd_conf_t;

extern slurmd_conf_t * conf;
extern int fini_job_cnt;
extern uint32_t *fini_job_id;
extern pthread_mutex_t fini_job_mutex;
extern pthread_mutex_t tres_mutex;
extern pthread_cond_t  tres_cond;

/* Send node registration message with status to controller
 * IN status - same values slurm error codes (for node shutdown)
 * IN startup - non-zero if slurmd just restarted
 */
int send_registration_msg(uint32_t status, bool startup);

/*
 * save_cred_state - save the current credential list to a file
 * IN list - list of credentials
 * RET int - zero or error code
 */
int save_cred_state(slurm_cred_ctx_t vctx);

/* Run the health check program if configured */
int run_script_health_check(void);

/* Handler for SIGTERM; can also be called to shutdown the slurmd. */
void slurmd_shutdown(int signum);

#endif /* !_SLURMD_H */
