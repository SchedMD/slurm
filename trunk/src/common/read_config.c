/*****************************************************************************\
 *  read_config.c - read the overall slurm configuration file
 *****************************************************************************
 *  Copyright (C) 2002 The Regents of the University of California.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Moe Mette <jette1@llnl.gov>.
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

#ifdef HAVE_CONFIG_H
#  include "config.h"
#endif

#include <assert.h>
#include <ctype.h>
#include <errno.h>
#include <netdb.h>
#include <pwd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

#include "src/api/slurm.h"
#include "src/common/slurm_protocol_defs.h"
#include "src/common/hostlist.h"
#include "src/common/log.h"
#include "src/common/macros.h"
#include "src/common/parse_spec.h"
#include "src/common/read_config.h"
#include "src/common/xmalloc.h"
#include "src/common/xstring.h"

#define BUF_SIZE 1024
#define MAX_NAME_LEN	32
#define MULTIPLE_VALUE_MSG "Multiple values for %s, latest one used"
#define FREE_NULL(_X)			\
	do {				\
		if (_X) xfree (_X);	\
		_X	= NULL; 	\
	} while (0)

static int _parse_node_spec (char *in_line);
static int _parse_part_spec (char *in_line);

/* 
 * init_slurm_conf - initialize or re-initialize the slurm configuration 
 *	values.   
 * IN/OUT ctl_conf_ptr - pointer to data structure to be initialized
 */
void
init_slurm_conf (slurm_ctl_conf_t *ctl_conf_ptr)
{
	ctl_conf_ptr->last_update		= time(NULL);
	FREE_NULL (ctl_conf_ptr->backup_addr);
	FREE_NULL (ctl_conf_ptr->backup_controller);
	FREE_NULL (ctl_conf_ptr->control_addr);
	FREE_NULL (ctl_conf_ptr->control_machine);
	FREE_NULL (ctl_conf_ptr->epilog);
	ctl_conf_ptr->fast_schedule		= (uint16_t) NO_VAL;
	ctl_conf_ptr->first_job_id		= (uint32_t) NO_VAL;
	ctl_conf_ptr->hash_base			= (uint16_t) NO_VAL;
	ctl_conf_ptr->heartbeat_interval	= (uint16_t) NO_VAL;
	ctl_conf_ptr->inactive_limit		= (uint16_t) NO_VAL;
	ctl_conf_ptr->kill_wait			= (uint16_t) NO_VAL;
	FREE_NULL (ctl_conf_ptr->prioritize);
	FREE_NULL (ctl_conf_ptr->prolog);
	ctl_conf_ptr->ret2service		= (uint16_t) NO_VAL; 
	FREE_NULL (ctl_conf_ptr->slurmctld_logfile);
	ctl_conf_ptr->slurm_user_id		= (uint16_t) NO_VAL; 
	FREE_NULL (ctl_conf_ptr->slurm_user_name);
	ctl_conf_ptr->slurmctld_port		= (uint32_t) NO_VAL;
	ctl_conf_ptr->slurmctld_timeout		= (uint16_t) NO_VAL;
	FREE_NULL (ctl_conf_ptr->slurmd_logfile);
	ctl_conf_ptr->slurmd_port		= (uint32_t) NO_VAL;
	FREE_NULL (ctl_conf_ptr->slurmd_spooldir);
	ctl_conf_ptr->slurmd_timeout		= (uint16_t) NO_VAL;
	FREE_NULL (ctl_conf_ptr->state_save_location);
	FREE_NULL (ctl_conf_ptr->tmp_fs);
	FREE_NULL (ctl_conf_ptr->job_credential_private_key);
	FREE_NULL (ctl_conf_ptr->job_credential_public_certificate);
	return;
}


/*
 * parse_config_spec - parse the overall configuration specifications, update  
 *	values
 * IN/OUT in_line - input line, parsed info overwritten with white-space
 * IN ctl_conf_ptr - pointer to data structure to be updated
 * RET 0 if no error, otherwise an error code
 *
 * NOTE: slurmctld and slurmd ports are built thus:
 *	if SlurmctldPort/SlurmdPort are set then
 *		get the port number based upon a look-up in /etc/services
 *		if the lookup fails then translate SlurmctldPort/SlurmdPort  
 *		into a number
 *	These port numbers are overridden if set in the configuration file
 */
int 
parse_config_spec (char *in_line, slurm_ctl_conf_t *ctl_conf_ptr) 
{
	int error_code;
	int fast_schedule = -1, hash_base = -1, heartbeat_interval = -1;
	int inactive_limit = -1, kill_wait = -1;
	int ret2service = -1, slurmctld_timeout = -1, slurmd_timeout = -1;
	char *backup_addr = NULL, *backup_controller = NULL;
	char *control_addr = NULL, *control_machine = NULL, *epilog = NULL;
	char *prioritize = NULL, *prolog = NULL;
	char *state_save_location = NULL, *tmp_fs = NULL;
	char *slurm_user = NULL;
	char *slurmctld_logfile = NULL, *slurmctld_port = NULL;
	char *slurmd_logfile = NULL, *slurmd_port = NULL;
	char *slurmd_spooldir = NULL, *slurmd_pidfile = NULL;
	char *job_credential_private_key = NULL;
	char *job_credential_public_certificate = NULL;
	long first_job_id = -1;
	struct servent *servent;

	error_code = slurm_parser (in_line,
		"BackupAddr=", 's', &backup_addr, 
		"BackupController=", 's', &backup_controller, 
		"ControlAddr=", 's', &control_addr, 
		"ControlMachine=", 's', &control_machine, 
		"Epilog=", 's', &epilog, 
		"FastSchedule=", 'd', &fast_schedule,
		"FirstJobId=", 'l', &first_job_id,
		"HashBase=", 'd', &hash_base,
		"HeartbeatInterval=", 'd', &heartbeat_interval,
		"InactiveLimit=", 'd', &inactive_limit,
		"JobCredentialPrivateKey=", 's', &job_credential_private_key,
		"JobCredentialPublicCertificate=", 's', 
					&job_credential_public_certificate,
		"KillWait=", 'd', &kill_wait,
		"Prioritize=", 's', &prioritize,
		"Prolog=", 's', &prolog,
		"ReturnToService=", 'd', &ret2service,
		"SlurmUser=", 's', &slurm_user,
		"SlurmctldLogFile=", 's', &slurmctld_logfile,
		"SlurmctldPort=", 's', &slurmctld_port,
		"SlurmctldTimeout=", 'd', &slurmctld_timeout,
		"SlurmdLogFile=", 's', &slurmd_logfile,
		"SlurmdPidFile=",  's', &slurmd_pidfile,
		"SlurmdPort=", 's', &slurmd_port,
		"SlurmdSpoolDir=", 's', &slurmd_spooldir,
		"SlurmdTimeout=", 'd', &slurmd_timeout,
		"StateSaveLocation=", 's', &state_save_location, 
		"TmpFS=", 's', &tmp_fs,
		"END");

	if (error_code)
		return error_code;

	if ( backup_addr ) {
		if ( ctl_conf_ptr->backup_addr ) {
			error (MULTIPLE_VALUE_MSG, "BackupAddr");
			xfree (ctl_conf_ptr->backup_addr);
		}
		ctl_conf_ptr->backup_addr = backup_addr;
	}

	if ( backup_controller ) {
		if ( ctl_conf_ptr->backup_controller ) {
			error (MULTIPLE_VALUE_MSG, "BackupController");
			xfree (ctl_conf_ptr->backup_controller);
		}
		ctl_conf_ptr->backup_controller = backup_controller;
	}

	if ( control_addr ) {
		if ( ctl_conf_ptr->control_addr ) {
			error (MULTIPLE_VALUE_MSG, "ControlAddr");
			xfree (ctl_conf_ptr->control_addr);
		}
		ctl_conf_ptr->control_addr = control_addr;
	}

	if ( control_machine ) {
		if ( ctl_conf_ptr->control_machine ) {
			error (MULTIPLE_VALUE_MSG, "ControlMachine");
			xfree (ctl_conf_ptr->control_machine);
		}
		ctl_conf_ptr->control_machine = control_machine;
	}

	if ( epilog ) {
		if ( ctl_conf_ptr->epilog ) {
			error (MULTIPLE_VALUE_MSG, "Epilog");
			xfree (ctl_conf_ptr->epilog);
		}
		ctl_conf_ptr->epilog = epilog;
	}

	if ( fast_schedule != -1) {
		if ( ctl_conf_ptr->fast_schedule != (uint16_t) NO_VAL)
			error (MULTIPLE_VALUE_MSG, "FastSchedule");
		ctl_conf_ptr->fast_schedule = fast_schedule;
	}

	if ( first_job_id != -1) {
		if ( ctl_conf_ptr->first_job_id != (uint32_t) NO_VAL)
			error (MULTIPLE_VALUE_MSG, "FirstJobId");
		ctl_conf_ptr->first_job_id = first_job_id;
	}

	if ( hash_base != -1) {
		if ( ctl_conf_ptr->hash_base != (uint16_t) NO_VAL)
			error (MULTIPLE_VALUE_MSG, "HashBase");
		ctl_conf_ptr->hash_base = hash_base;
	}

	if ( heartbeat_interval != -1) {
		if ( ctl_conf_ptr->heartbeat_interval != (uint16_t) NO_VAL)
			error (MULTIPLE_VALUE_MSG, "HeartbeatInterval");
		ctl_conf_ptr->heartbeat_interval = heartbeat_interval;
	}

	if ( inactive_limit != -1) {
		if ( ctl_conf_ptr->inactive_limit != (uint16_t) NO_VAL)
			error (MULTIPLE_VALUE_MSG, "InactiveLimit");
		ctl_conf_ptr->inactive_limit = inactive_limit;
	}

	if ( kill_wait != -1) {
		if ( ctl_conf_ptr->kill_wait != (uint16_t) NO_VAL)
			error (MULTIPLE_VALUE_MSG, "KillWait");
		ctl_conf_ptr->kill_wait = kill_wait;
	}

	if ( prioritize ) {
		if ( ctl_conf_ptr->prioritize ) {
			error (MULTIPLE_VALUE_MSG, "Prioritize");
			xfree (ctl_conf_ptr->prioritize);
		}
		ctl_conf_ptr->prioritize = prioritize;
	}

	if ( prolog ) {
		if ( ctl_conf_ptr->prolog ) {
			error (MULTIPLE_VALUE_MSG, "Prolog");
			xfree (ctl_conf_ptr->prolog);
		}
		ctl_conf_ptr->prolog = prolog;
	}

	if ( ret2service != -1) {
		if ( ctl_conf_ptr->ret2service != (uint16_t) NO_VAL)
			error (MULTIPLE_VALUE_MSG, "ReturnToService");
		ctl_conf_ptr->ret2service = ret2service;
	}

	if ( slurm_user ) {
		struct passwd *slurm_passwd;
		slurm_passwd = getpwnam(slurm_user);
		if (slurm_passwd == NULL) {
			error ("Invalid user for SlurmUser %s, ignored",
			       slurm_user);
		} else {
			if ( ctl_conf_ptr->slurm_user_name ) {
				error (MULTIPLE_VALUE_MSG, "SlurmUser");
				xfree (ctl_conf_ptr->slurm_user_name);
			}
			ctl_conf_ptr->slurm_user_name = slurm_user;
			ctl_conf_ptr->slurm_user_id = slurm_passwd->pw_uid;
		}
	}

	if ( slurmctld_logfile ) {
		if ( ctl_conf_ptr->slurmctld_logfile ) {
			error (MULTIPLE_VALUE_MSG, "SlurmctldLogFile");
			xfree (ctl_conf_ptr->slurmctld_logfile);
		}
		ctl_conf_ptr->slurmctld_logfile = slurmctld_logfile;
	}

	if ( slurmctld_port ) {
		if ( ctl_conf_ptr->slurmctld_port != (uint32_t) NO_VAL)
			error (MULTIPLE_VALUE_MSG, "SlurmctldPort");
		servent = getservbyname (slurmctld_port, NULL);
		if (servent)
			ctl_conf_ptr->slurmctld_port = servent -> s_port;
		else
			ctl_conf_ptr->slurmctld_port = strtol (slurmctld_port, 
							(char **) NULL, 10);
		endservent ();
	}

	if ( slurmctld_timeout != -1) {
		if ( ctl_conf_ptr->slurmctld_timeout != (uint16_t) NO_VAL)
			error (MULTIPLE_VALUE_MSG, "SlurmctldTimeout");
		ctl_conf_ptr->slurmctld_timeout = slurmctld_timeout;
	}

	if ( slurmd_logfile ) {
		if ( ctl_conf_ptr->slurmd_logfile ) {
			error (MULTIPLE_VALUE_MSG, "SlurmdLogFile");
			xfree (ctl_conf_ptr->slurmd_logfile);
		}
		ctl_conf_ptr->slurmd_logfile = slurmd_logfile;
	}

	if ( slurmd_port ) {
		if ( ctl_conf_ptr->slurmd_port != (uint32_t) NO_VAL)
			error (MULTIPLE_VALUE_MSG, "SlurmdPort");
		servent = getservbyname (slurmd_port, NULL);
		if (servent)
			ctl_conf_ptr->slurmd_port = servent -> s_port;
		else
			ctl_conf_ptr->slurmd_port = strtol (slurmd_port,  
							(char **) NULL, 10);
		endservent ();
	}

	if ( slurmd_spooldir ) {
		if ( ctl_conf_ptr->slurmd_spooldir ) {
			error (MULTIPLE_VALUE_MSG, "SlurmdSpoolDir");
			xfree (ctl_conf_ptr->slurmd_spooldir);
		}
		ctl_conf_ptr->slurmd_spooldir = slurmd_spooldir;
	}

	if ( slurmd_pidfile ) {
		if ( ctl_conf_ptr->slurmd_pidfile ) {
			error (MULTIPLE_VALUE_MSG, "SlurmdPidFile");
			xfree (ctl_conf_ptr->slurmd_pidfile);
		}
		ctl_conf_ptr->slurmd_pidfile = slurmd_pidfile;
	}

	if ( slurmd_timeout != -1) {
		if ( ctl_conf_ptr->slurmd_timeout != (uint16_t) NO_VAL)
			error (MULTIPLE_VALUE_MSG, "SlurmdTimeout");
		ctl_conf_ptr->slurmd_timeout = slurmd_timeout;
	}

	if ( state_save_location ) {
		if ( ctl_conf_ptr->state_save_location ) {
			error (MULTIPLE_VALUE_MSG, "StateSaveLocation");
			xfree (ctl_conf_ptr->state_save_location);
		}
		ctl_conf_ptr->state_save_location = state_save_location;
	}

	if ( tmp_fs ) {
		if ( ctl_conf_ptr->tmp_fs ) {
			error (MULTIPLE_VALUE_MSG, "TmpFS");
			xfree (ctl_conf_ptr->tmp_fs);
		}
		ctl_conf_ptr->tmp_fs = tmp_fs;
	}

	if ( job_credential_private_key ) {
		if ( ctl_conf_ptr->job_credential_private_key ) {
			error (MULTIPLE_VALUE_MSG, "JobCredentialPrivateKey");
			xfree (ctl_conf_ptr->job_credential_private_key);
		}
		ctl_conf_ptr->job_credential_private_key = 
						job_credential_private_key;
	}

	if ( job_credential_public_certificate ) {
		if ( ctl_conf_ptr->job_credential_public_certificate ) {
			error (MULTIPLE_VALUE_MSG, 
			       "JobCredentialPublicCertificate");
			xfree (ctl_conf_ptr->
			       job_credential_public_certificate);
		}
		ctl_conf_ptr->job_credential_public_certificate = 
					job_credential_public_certificate;
	}

	return 0;
}

/*
 * _parse_node_spec - just overwrite node specifications (toss the results)
 * IN/OUT in_line - input line, parsed info overwritten with white-space
 * RET 0 if no error, otherwise an error code
 */
static int 
_parse_node_spec (char *in_line) 
{
	int error_code;
	char *feature = NULL, *node_addr = NULL, *node_name = NULL;
	char *state = NULL;
	int cpus_val, real_memory_val, tmp_disk_val, weight_val;

	error_code = slurm_parser (in_line,
		"Feature=", 's', &feature, 
		"NodeAddr=", 's', &node_addr, 
		"NodeName=", 's', &node_name, 
		"Procs=", 'd', &cpus_val, 
		"RealMemory=", 'd', &real_memory_val, 
		"State=", 's', &state, 
		"TmpDisk=", 'd', &tmp_disk_val, 
		"Weight=", 'd', &weight_val, 
		"END");

	if (error_code)
		return error_code;

	if (feature)
		xfree (feature);
	if (node_addr)
		xfree (node_addr);
	if (node_name)
		xfree (node_name);
	if (state)
		xfree (state);

	return 0;
}

/*
 * _parse_part_spec - just overwrite partition specifications (toss the  
 *	results)
 * IN/OUT in_line - input line, parsed info overwritten with white-space
 * RET 0 if no error, otherwise an error code
 */
static int 
_parse_part_spec (char *in_line) 
{
	int error_code;
	char *allow_groups = NULL, *default_str = NULL;
	char *partition = NULL, *root_str = NULL;
	char *nodes = NULL, *shared_str = NULL, *state_str = NULL;
	int max_time_val, max_nodes_val;

	error_code = slurm_parser (in_line,
		"AllowGroups=", 's', &allow_groups, 
		"Default=", 's', &default_str, 
		"PartitionName=", 's', &partition, 
		"RootOnly=", 's', &root_str, 
		"MaxTime=", 'd', &max_time_val, 
		"MaxNodes=", 'd', &max_nodes_val, 
		"Nodes=", 's', &nodes, 
		"Shared=", 's', &shared_str, 
		"State=", 's', &state_str, 
		"END");

	if (error_code)
		return error_code;

	if (allow_groups)
		xfree (allow_groups);
	if (default_str)
		xfree (default_str);
	if (partition)
		xfree (partition);
	if (root_str)
		xfree (root_str);
	if (nodes)
		xfree (nodes);
	if (shared_str)
		xfree (shared_str);
	if (state_str)
		xfree (state_str);

	return 0;
}

/*
 * read_slurm_conf_ctl - load the slurm configuration from the configured 
 *	file. 
 * OUT ctl_conf_ptr - pointer to data structure to be filled
 * RET 0 if no error, otherwise an error code
 */
int 
read_slurm_conf_ctl (slurm_ctl_conf_t *ctl_conf_ptr) 
{
	FILE *slurm_spec_file;	/* pointer to input data file */
	int line_num;		/* line number in input file */
	int line_size;		/* bytes in current input line */
	char in_line[BUF_SIZE];	/* input line */
	int error_code, i, j;

	assert (ctl_conf_ptr);
	init_slurm_conf (ctl_conf_ptr);

	if (ctl_conf_ptr->slurm_conf == NULL)
		ctl_conf_ptr->slurm_conf = xstrdup (SLURM_CONFIG_FILE);
	slurm_spec_file = fopen (ctl_conf_ptr->slurm_conf, "r");
	if (slurm_spec_file == NULL) {
		fatal ("read_slurm_conf_ctl error opening file %s, %m", 
			ctl_conf_ptr->slurm_conf);
	}

	/* process the data file */
	line_num = 0;
	while (fgets (in_line, BUF_SIZE, slurm_spec_file) != NULL) {
		line_num++;
		line_size = strlen (in_line);
		if (line_size >= (BUF_SIZE - 1)) {
			error ("read_slurm_conf_ctl line %d, of input file %s too long",
			       line_num, ctl_conf_ptr->slurm_conf);
			fclose (slurm_spec_file);
			return E2BIG;
			break;
		}		

		/* everything after a non-escaped "#" is a comment	*/
		/* replace comment flag "#" with a `\0' (End of string)	*/
		/* an escaped value "\#" is translated to "#"		*/
		/* this permitted embedded "#" in node/partition names	*/
		for (i = 0; i < line_size; i++) {
			if (in_line[i] == '\0')
				break;
			if (in_line[i] != '#')
				continue;
			if ((i > 0) && (in_line[i - 1] == '\\')) {
				for (j = i; j < line_size; j++) {
					in_line[j - 1] = in_line[j];
				}
				line_size--;
				continue;
			}	
			in_line[i] = '\0';
			break;
		}		

		/* parse what is left */
		
		/* overall configuration parameters */
		if ((error_code = parse_config_spec (in_line, ctl_conf_ptr))) {
			fclose (slurm_spec_file);
			return error_code;
		}

		/* node configuration parameters */
		if ((error_code = _parse_node_spec (in_line))) {
			fclose (slurm_spec_file);
			return error_code;
		}		

		/* partition configuration parameters */
		if ((error_code = _parse_part_spec (in_line))) {
			fclose (slurm_spec_file);
			return error_code;
		}		

		/* report any leftover strings on input line */
		report_leftover (in_line, line_num);
	}

	fclose (slurm_spec_file);
	validate_config (ctl_conf_ptr);
	return SLURM_SUCCESS;
}

/* 
 * report_leftover - report any un-parsed (non-whitespace) characters on the
 * configuration input line (we over-write parsed characters with whitespace).
 * IN in_line - what is left of the configuration input line.
 * IN line_num - line number of the configuration file.
 */
void
report_leftover (char *in_line, int line_num)
{
	int i;

	for (i = 0; i < strlen (in_line); i++) {
		if (isspace ((int) in_line[i]) || (in_line[i] == '\n'))
			continue;
		error ("Ignored input on line %d of configuration: %s",
			line_num, &in_line[i]);
		break;
	}
}

/* validate configuration
 *
 * IN/OUT ctl_conf_ptr - a configuration as loaded by read_slurm_conf_ctl
 *
 * NOTE: default slurmctld and slurmd ports are built thus:
 *	if SLURMCTLD_PORT/SLURMD_PORT are set then
 *		get the port number based upon a look-up in /etc/services
 *		if the lookup fails then translate SLURMCTLD_PORT/SLURMD_PORT 
 *		into a number
 *	These port numbers are overridden if set in the configuration file
 * NOTE: a backup_controller or control_machine of "localhost" are over-written
 *	with this machine's name.
 * NOTE: if backup_addr is NULL, it is over-written by backup_controller
 * NOTE: if control_addr is NULL, it is over-written by control_machine
 */
void
validate_config (slurm_ctl_conf_t *ctl_conf_ptr)
{
	struct servent *servent;

	if ((ctl_conf_ptr->backup_controller != NULL) &&
	    (strcmp("localhost", ctl_conf_ptr->backup_controller) == 0)) {
		xfree (ctl_conf_ptr->backup_controller);
		ctl_conf_ptr->backup_controller = xmalloc (MAX_NAME_LEN);
		if ( getnodename (ctl_conf_ptr->backup_controller, 
		                  MAX_NAME_LEN) ) 
			fatal ("getnodename: %m");
	}

	if ((ctl_conf_ptr->backup_addr == NULL) && 
	    (ctl_conf_ptr->backup_controller != NULL))
		ctl_conf_ptr->backup_addr = 
				xstrdup (ctl_conf_ptr->backup_controller);

	if ((ctl_conf_ptr->backup_controller == NULL) && 
	    (ctl_conf_ptr->backup_addr != NULL)) {
		error ("BackupAddr specified without BackupController");
		FREE_NULL (ctl_conf_ptr->backup_controller);
	}

	if (ctl_conf_ptr->control_machine == NULL)
		fatal ("read_slurm_conf: ControlMachine not specified.");
	else if (strcmp("localhost", ctl_conf_ptr->control_machine) == 0) {
		xfree (ctl_conf_ptr->control_machine);
		ctl_conf_ptr->control_machine = xmalloc (MAX_NAME_LEN);
		if ( getnodename (ctl_conf_ptr->control_machine, 
		                  MAX_NAME_LEN) ) 
			fatal ("getnodename: %m");
	}

	if ((ctl_conf_ptr->control_addr == NULL) && 
	    (ctl_conf_ptr->control_machine != NULL))
		ctl_conf_ptr->control_addr = 
				xstrdup (ctl_conf_ptr->control_machine);

	if ((ctl_conf_ptr->backup_controller != NULL) && 
	    (strcmp (ctl_conf_ptr->backup_controller, 
	             ctl_conf_ptr->control_machine) == 0)) {
		error ("ControlMachine and BackupController identical");
		FREE_NULL (ctl_conf_ptr->backup_addr);
		FREE_NULL (ctl_conf_ptr->backup_controller);
	}

	if (ctl_conf_ptr->slurmctld_port == (uint32_t) NO_VAL) {
		servent = getservbyname (SLURMCTLD_PORT, NULL);
		if (servent)
			ctl_conf_ptr->slurmctld_port = servent -> s_port;
		else
			ctl_conf_ptr->slurmctld_port = strtol (SLURMCTLD_PORT, 
							(char **) NULL, 10);
		endservent ();
	}

	if (ctl_conf_ptr->slurmd_port == (uint32_t) NO_VAL) {
		servent = getservbyname (SLURMD_PORT, NULL);
		if (servent)
			ctl_conf_ptr->slurmd_port = servent -> s_port;
		else
			ctl_conf_ptr->slurmd_port = strtol (SLURMCTLD_PORT, 
							(char **) NULL, 10);
		endservent ();
	}
}

