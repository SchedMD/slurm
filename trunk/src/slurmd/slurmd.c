/*****************************************************************************\
 *  slurmd.c - main server machine daemon for slurm
 *****************************************************************************
 *  Copyright (C) 2002 The Regents of the University of California.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Kevin Tew <tew1@llnl.gov> et. al.
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
 *  with ConMan; if not, write to the Free Software Foundation, Inc.,
 *  59 Temple Place, Suite 330, Boston, MA  02111-1307  USA.
\*****************************************************************************/

#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <getopt.h>
#include <stdio.h>
#include <time.h>
#include <signal.h>
#include <unistd.h>
#include <pthread.h>
#include <src/common/hostlist.h>
#include <src/common/parse_spec.h>
#include <src/common/xmalloc.h>
#include <src/common/xstring.h>
#include <src/common/list.h>
#include <src/common/slurm_protocol_api.h>
#include <src/common/util_signals.h>
#include <src/common/log.h>

#include <src/slurmd/get_mach_stat.h>
#include <src/slurmd/slurmd.h>
#include <src/slurmd/task_mgr.h>
#include <src/slurmd/shmem_struct.h>
#include <src/common/signature_utils.h>
#include <src/common/credential_utils.h>

#define BUF_SIZE 	1024
#define MAX_NAME_LEN 	1024
#define PTHREAD_IMPL

/* global variables */
typedef struct slurmd_config {
	log_options_t log_opts;
	char *slurm_conf;
	int daemonize;
} slurmd_config_t;

typedef struct connection_arg {
	int newsockfd;
} connection_arg_t;

time_t init_time;
pid_t slurmd_pid;
time_t shutdown_time = (time_t) 0;
slurmd_shmem_t *shmem_seg;
char hostname[MAX_NAME_LEN];
slurm_ssl_key_ctx_t verify_ctx;
List credential_state_list;
slurmd_config_t slurmd_conf;

/* function prototypes */
static char *public_cert_filename();
inline static void reset_cwd(void);
inline static char *state_save_location (void);
static void slurmd_req(slurm_msg_t * msg);
static void *slurmd_msg_engine(void *args);
inline static int send_node_registration_status_msg();

inline static void slurm_rpc_kill_tasks(slurm_msg_t * msg);
inline static void slurm_rpc_launch_tasks(slurm_msg_t * msg);
inline static void slurm_rpc_ping(slurm_msg_t * msg);
inline static void slurm_rpc_reattach_tasks_streams(slurm_msg_t * msg);
inline static void slurm_rpc_revoke_credential(slurm_msg_t * msg);
inline static void slurmd_rpc_shutdown_slurmd(slurm_msg_t * msg);

inline static int
fill_in_node_registration_status_msg(slurm_node_registration_status_msg_t * node_reg_msg);
static void *service_connection(void *arg);
static void *slurmd_handle_signals(void *args);
inline int slurmd_init();
inline int slurmd_destroy();

inline static int parse_commandline_args(int argc, char **argv,
					 slurmd_config_t * slurmd_config);
inline static int slurmd_shutdown();

int main(int argc, char *argv[])
{
	int rc;
	pthread_t sigthr;
	char node_name[MAX_NAME_LEN];
	log_options_t log_opts_def = LOG_OPTS_STDERR_ONLY;

	init_time = time(NULL);
	slurmd_conf.log_opts = log_opts_def;
	slurmd_conf.daemonize = false;


	if (parse_commandline_args(argc, argv, &slurmd_conf))
		exit (1);
	log_init(argv[0], slurmd_conf.log_opts, SYSLOG_FACILITY_DAEMON, NULL);

	if (slurmd_conf.daemonize == true) {
		daemon(false, true);
		reset_cwd();
	}

	/* shared memory init */
	slurmd_init();

	if ((rc = getnodename(node_name, MAX_NAME_LEN)))
		fatal("getnodename: %m");

	strncpy(hostname, node_name, MAX_NAME_LEN);

	/* send registration message to slurmctld */
	send_node_registration_status_msg();

	/* block SIGHUP, SIGTERM, and SIGINT in all threads */
	/* block_some_signals(); */

	/* create attached thread to handle signals */
	if (pthread_create(&sigthr, NULL, &slurmd_handle_signals, 
			   (void *)NULL) != 0)
		fatal("pthread_create: %m");

	slurmd_msg_engine((void *)NULL);

	slurmd_destroy();
	return SLURM_SUCCESS;
}

void *slurmd_handle_signals(void *args)
{
	sigset_t set;
	int error_code;
	int sig;

	/* just watch for select signals */
	if (sigemptyset(&set))
		error("sigemptyset error: %m");
	if (sigaddset(&set, SIGHUP))
		error("sigaddset error on SIGHUP: %m");
	if (sigaddset(&set, SIGINT))
		error("sigaddset error on SIGINT: %m");
	if (sigaddset(&set, SIGTERM))
		error("sigaddset error on SIGTERM: %m");
        if (sigaddset(&set, SIGABRT))
                error("sigaddset error on SIGABRT: %m");

	if (sigprocmask(SIG_BLOCK, &set, NULL) != 0)
		fatal("sigprocmask error: %m");

	while (1) {
		if ((error_code = sigwait(&set, &sig)))
			error("sigwait errno %d\n", error_code);

		switch (sig) {
		case SIGINT:	/* kill -2  or <CTRL-C> */
		case SIGTERM:	/* kill -15 */
			info("Terminate signal (SIGINT or SIGTERM) received\n");
			shutdown_time = time(NULL);
			/* send REQUEST_SHUTDOWN_IMMEDIATE RPC */
			slurmd_shutdown();
			/* pthread_join(thread_id_rpc, NULL); */
			pthread_exit((void *) 0);
			break;
		case SIGHUP:	/* kill -1 */
			info("Reconfigure signal (SIGHUP) received\n");
			if (slurmd_conf.daemonize == true)
				reset_cwd();
			break;
		default:
			error("Invalid signal (%d) received", sig);
		}
	}
}

int slurmd_init()
{
	slurmd_pid = getpid();
	shmem_seg = get_shmem();
	init_shmem(shmem_seg);
	slurm_ssl_init();
	slurm_init_verifier(&verify_ctx, public_cert_filename());
	initialize_credential_state_list(&credential_state_list);
	return SLURM_SUCCESS;
}

static char *public_cert_filename()
{
	int i, j, error_code, line_num = 0;
	FILE *slurm_spec_file;	/* pointer to input data file */
	char in_line[BUF_SIZE];	/* input line */
	char *job_credential_public_certificate = NULL;

	slurm_spec_file = fopen(SLURM_CONFIG_FILE, "r");
	if (slurm_spec_file == NULL)
		fatal("public_cert_filename error %d opening file %s",
		      errno, SLURM_CONFIG_FILE);

	while (fgets(in_line, BUF_SIZE, slurm_spec_file) != NULL) {
		line_num++;
		if (strlen(in_line) >= (BUF_SIZE - 1)) {
			fatal("public_cert_filename line %d, "
			      "of input file %s too long", line_num, 
			      SLURM_CONFIG_FILE);
		}

		/* everything after a non-escaped "#" is a comment */
		/* replace comment flag "#" with an end of string (NULL) */
		for (i = 0; i < BUF_SIZE; i++) {
			if (in_line[i] == (char) NULL)
				break;
			if (in_line[i] != '#')
				continue;
			if ((i > 0) && (in_line[i - 1] == '\\')) {	/* escaped "#" */
				for (j = i; j < BUF_SIZE; j++) {
					in_line[j - 1] = in_line[j];
				}
				continue;
			}
			in_line[i] = (char) NULL;
			break;
		}

		/* parse what is left */
		error_code = slurm_parser(in_line,
					  "JobCredentialPublicCertificate=",
					  's',
					  &job_credential_public_certificate,
					  "END");
		if (error_code || job_credential_public_certificate)
			break;
	}
	fclose(slurm_spec_file);
	return job_credential_public_certificate;
}

int slurmd_destroy()
{
	destroy_credential_state_list(credential_state_list);
	rel_shmem(shmem_seg);
	slurm_destroy_ssl_key_ctx(&verify_ctx);
	slurm_ssl_destroy();
	return SLURM_SUCCESS;
}

/* sends a node_registration_status_msg to the slurmctld upon boot
 * announcing availibility for computation */
int send_node_registration_status_msg()
{
	slurm_msg_t request_msg;
	slurm_msg_t response_msg;
	slurm_node_registration_status_msg_t node_reg_msg;

	fill_in_node_registration_status_msg(&node_reg_msg);

	request_msg.msg_type = MESSAGE_NODE_REGISTRATION_STATUS;
	request_msg.data = &node_reg_msg;

	slurm_send_recv_controller_msg(&request_msg, &response_msg);
	return SLURM_SUCCESS;
}

/* calls machine dependent system info calls to fill structure
 * node_reg_msg - structure to fill with system info
 * returns - return code
 */
int
fill_in_node_registration_status_msg(slurm_node_registration_status_msg_t *
				     node_reg_msg)
{
	/* fill in data structure */
	node_reg_msg->timestamp = time(NULL);
	node_reg_msg->node_name = xstrdup(hostname);
	get_procs(&node_reg_msg->cpus);
	get_memory(&node_reg_msg->real_memory_size);
	get_tmp_disk(&node_reg_msg->temporary_disk_space);
/* FIXME: Need to set correct count of currently running job stepss and their ID's below */
/* This is needed to more reliably recover from restarts of daemons */
	node_reg_msg->job_count = 0;
	node_reg_msg->job_id = NULL;
	node_reg_msg->step_id = NULL;
	info("Configuration name=%s cpus=%u real_memory=%u, tmp_disk=%u, job_count=%u",
	     hostname, node_reg_msg->cpus,
	     node_reg_msg->real_memory_size,
	     node_reg_msg->temporary_disk_space,
	     node_reg_msg->job_count);
	return SLURM_SUCCESS;
}

/* accept thread for incomming slurm messages 
 * args - do nothing right now */
void *slurmd_msg_engine(void *args)
{
	int rc;
	slurm_fd newsockfd;
	slurm_fd sockfd;
	slurm_addr cli_addr;
	pthread_t request_thread_id;
	pthread_attr_t thread_attr;

	if ((rc = read_slurm_port_config()) != 0)
		fatal("error code %d reading config file", rc);

	if ((sockfd = slurm_init_msg_engine_port(slurm_get_slurmd_port()))
	    == SLURM_SOCKET_ERROR)
		fatal("slurm_init_msg_engine_port: %m");

	if ((rc = pthread_attr_init(&thread_attr)))
		error("pthread_attr_init returned %d", rc);

	if ((rc = pthread_attr_setdetachstate(&thread_attr, 
			                      PTHREAD_CREATE_DETACHED))) {
		error("pthread_attr_setdetachstate return %d", rc);
	}

	while (true) {
		connection_arg_t *conn_arg =
		    xmalloc(sizeof(connection_arg_t));

		/* accept needed for stream implementation 
		 * is a no-op in mongo implementation that just passes sockfd to newsockfd
		 */
		if ((newsockfd = slurm_accept_msg_conn(sockfd, &cli_addr)) == 
				SLURM_SOCKET_ERROR) {
			error("slurmd_msg_engine: accept: %m");
			continue;
		}

		/* receive message call that must occur before thread spawn because in message 
		 * implementation their is no connection and the message is the sign of a new connection */
		conn_arg->newsockfd = newsockfd;

		if (shutdown_time) {
			service_connection((void *) conn_arg);
			pthread_exit((void *) 0);

		}

		if ((rc = pthread_create(&request_thread_id, 
					 &thread_attr,
				         service_connection, 
					 (void *) conn_arg  ))) {
			/* Do without threads on failure */
			error("slurmd_msg_engine: pthread_create: %m");
			service_connection((void *) conn_arg);
		}
	}
	slurm_shutdown_msg_engine(sockfd);
	return NULL;
}

/* worker thread method for accepted message connections
 * arg - a slurm_msg_t representing the accepted incomming message
 * returns - nothing, void * because of pthread def
 */
void *service_connection(void *arg)
{
	int rc;
	slurm_fd newsockfd = ((connection_arg_t *) arg)->newsockfd;
	slurm_msg_t *msg = NULL;

	msg = xmalloc(sizeof(slurm_msg_t));

	if ((rc = slurm_receive_msg(newsockfd, msg)) == SLURM_SOCKET_ERROR) {
		error("service_connection: accept: %m");
		slurm_free_msg(msg);
	} else {
		msg->conn_fd = newsockfd;
		slurmd_req(msg);	/* process the request */
	}

	/* close should only be called when the stream implementation is being used
	 * the following call will be a no-op in the message implementation */
	slurm_close_accepted_conn(newsockfd);	/* close the new socket */
	xfree(arg);
	return NULL;
}

/* multiplexing message handler
 * msg - incomming request message 
 */
void slurmd_req(slurm_msg_t * msg)
{

	switch (msg->msg_type) {
	case REQUEST_LAUNCH_TASKS:
		slurm_rpc_launch_tasks(msg);
		slurm_free_launch_tasks_request_msg(msg->data);
		break;
	case REQUEST_KILL_TASKS:
		slurm_rpc_kill_tasks(msg);
		slurm_free_kill_tasks_msg(msg->data);
		break;
	case REQUEST_REATTACH_TASKS_STREAMS:
		slurm_rpc_reattach_tasks_streams(msg);
		slurm_free_reattach_tasks_streams_msg(msg->data);
		break;
	case REQUEST_REVOKE_JOB_CREDENTIAL:
		slurm_rpc_revoke_credential(msg);
		slurm_free_revoke_credential_msg(msg->data);
		break;
	case REQUEST_SHUTDOWN:
	case REQUEST_SHUTDOWN_IMMEDIATE:
		slurmd_rpc_shutdown_slurmd(msg);
		break;
	case REQUEST_NODE_REGISTRATION_STATUS:
		/* Treat as ping (for slurmctld agent) */
		slurm_rpc_ping(msg);
		/* Then initiate a separate node registration */
		send_node_registration_status_msg();
		break;
	case REQUEST_PING:
		slurm_rpc_ping(msg);
		break;
	default:
		error("slurmd_req: invalid request msg type %d\n",
		      msg->msg_type);
		slurm_send_rc_msg(msg, EINVAL);
		break;
	}
	slurm_free_msg(msg);
}


/******************************/
/* rpc methods */
/******************************/

/* Launches tasks */
void slurm_rpc_launch_tasks(slurm_msg_t * msg)
{
	/* init */
	int rc = SLURM_SUCCESS;
	clock_t start_time;
	launch_tasks_request_msg_t *task_desc =
	    (launch_tasks_request_msg_t *) msg->data;
	slurm_msg_t resp_msg;
	launch_tasks_response_msg_t task_resp;

	start_time = clock();
	info("slurmd_req: launch tasks message received");

	slurm_print_launch_task_msg(task_desc);

	/* do RPC call */
	/* test credentials */
	/* rc =  */ verify_credential(&verify_ctx, task_desc->credential, 
			       credential_state_list);

	task_resp.node_name = hostname;
	task_resp.srun_node_id = task_desc->srun_node_id;

	resp_msg.address = task_desc->response_addr;
	resp_msg.data = &task_resp;
	resp_msg.msg_type = RESPONSE_LAUNCH_TASKS;

	
	task_resp.return_code = rc; 

	/* return result */
	if (rc) {
		error("slurmd_req: launch tasks error %d", rc);
		slurm_send_only_node_msg(&resp_msg);
	} else {
		info("slurmd_req: launch authorization completed "
		     "successfully, time=%ld", (long) (clock() - start_time));
		slurm_send_only_node_msg(&resp_msg);
		launch_tasks(task_desc);
	}
}

/* Just respond to ping */
void slurm_rpc_ping(slurm_msg_t * msg)
{
	slurm_send_rc_msg(msg, SLURM_SUCCESS);
}

/* Kills Launched Tasks */
void slurm_rpc_kill_tasks(slurm_msg_t * msg)
{
	/* init */
	int error_code;
	clock_t start_time;
	kill_tasks_msg_t *kill_tasks_msg = (kill_tasks_msg_t *) msg->data;

	start_time = clock();

	/* do RPC call */
	error_code = kill_tasks(kill_tasks_msg);

	/* return result */
	if (error_code) {
		error("slurmd_req: kill tasks error %d, time=%ld",
		      error_code, (long) (clock() - start_time));
		slurm_send_rc_msg(msg, error_code);
	} else {
		info("slurmd_req: kill tasks completed successfully, time=%ld", (long) (clock() - start_time));
		slurm_send_rc_msg(msg, SLURM_SUCCESS);
	}
}

void slurm_rpc_reattach_tasks_streams(slurm_msg_t * msg)
{
	/* init */
	int error_code = SLURM_SUCCESS;
	clock_t start_time;
	reattach_tasks_streams_msg_t *reattach_tasks_steams_msg =
	    (reattach_tasks_streams_msg_t *) msg->data;

	start_time = clock();

	/* do RPC call */
	error_code = reattach_tasks_streams(reattach_tasks_steams_msg);

	/* return result */
	if (error_code) {
		error("slurmd_req: reattach streams error %d, time=%ld",
		      error_code, (long) (clock() - start_time));
		slurm_send_rc_msg(msg, error_code);
	} else {
		info("slurmd_req: reattach_streams completed successfully, time=%ld", (long) (clock() - start_time));
		slurm_send_rc_msg(msg, SLURM_SUCCESS);
	}

}

void slurm_rpc_revoke_credential(slurm_msg_t * msg)
{
	/* init */
	int error_code = SLURM_SUCCESS;
	clock_t start_time;
	revoke_credential_msg_t *revoke_credential_msg =
	    (revoke_credential_msg_t *) msg->data;

	start_time = clock();

	/* do RPC call */
	error_code =
	    revoke_credential(revoke_credential_msg,
			      credential_state_list);

	/* return result */
	if (error_code) {
		error("slurmd_req:  error %m errno %d, time=%ld",
		      error_code, (long) (clock() - start_time));
		slurm_send_rc_msg(msg, errno);
	} else {
		info("slurmd_req:  completed successfully, time=%ld",
		     (long) (clock() - start_time));
		slurm_send_rc_msg(msg, SLURM_SUCCESS);
	}

}

/* slurmd_rpc_shutdown_slurmd - process RPC to shutdown slurmd */
void slurmd_rpc_shutdown_slurmd(slurm_msg_t * msg)
{
	/* do RPC call */
	/* must be user root */
	if (shutdown_time)
		debug3("slurm_rpc_shutdown_controller again");
	else {
		kill(slurmd_pid, SIGTERM);	/* tell master to clean-up */
		info("slurm_rpc_shutdown_controller completed successfully");
	}

	/* return result */
	slurm_send_rc_msg(msg, SLURM_SUCCESS);
}


/* slurm_shutdown - issue RPC to have slurmctld shutdown, knocks loose an accept() */
int slurmd_shutdown()
{
	int rc;
	slurm_msg_t request_msg;
	slurm_msg_t response_msg;
	return_code_msg_t *slurm_rc_msg;
	slurm_addr slurmd_addr;

	kill_all_tasks();

	/* init message connection for message communication with controller */
	slurm_set_addr_char(&slurmd_addr, slurm_get_slurmd_port(),
			    "localhost");

	/* send request message */
	request_msg.address = slurmd_addr;
	request_msg.msg_type = REQUEST_SHUTDOWN_IMMEDIATE;

	if ((rc =
	     slurm_send_recv_node_msg(&request_msg,
				      &response_msg)) ==
	    SLURM_SOCKET_ERROR) {
		error("slurm_send_recv_node_only_msg error");
		return SLURM_SOCKET_ERROR;
	}

	switch (response_msg.msg_type) {
	case RESPONSE_SLURM_RC:
		slurm_rc_msg = (return_code_msg_t *) response_msg.data;
		rc = slurm_rc_msg->return_code;
		slurm_free_return_code_msg(slurm_rc_msg);
		if (rc) {
			error("slurm_shutdown_msg_conn error (%d)", rc);
			return SLURM_PROTOCOL_ERROR;
		}
		break;
	default:
		error("slurm_shutdown_msg_conn type bad (%d)",
		      response_msg.msg_type);
		return SLURM_UNEXPECTED_MSG_ERROR;
		break;
	}

	return SLURM_PROTOCOL_SUCCESS;
}

void slurm_rpc_slurmd_template(slurm_msg_t * msg)
{
	/* init */
	int error_code = SLURM_SUCCESS;
	clock_t start_time;
	/*_msg_t * _msg = ( _msg_t * ) msg->data ; */

	start_time = clock();

	/* do RPC call */

	/*error_code = (); */

	/* return result */
	if (error_code) {
		error("slurmd_req:  error %d, time=%ld",
		      error_code, (long) (clock() - start_time));
		slurm_send_rc_msg(msg, error_code);
	} else {
		info("slurmd_req:  completed successfully, time=%ld",
		     (long) (clock() - start_time));
		slurm_send_rc_msg(msg, SLURM_SUCCESS);
	}

}

void usage(char *prog_name)
{
	printf("%s [OPTIONS]\n", prog_name);
	printf("  -e <errlev>  Set stderr logging to the specified level\n");
	printf("  -f <file>    Use specified configuration file name\n");
	printf("  -d           daemonize\n");
	printf("  -h           Print a help message describing usage\n");
	printf("  -l <errlev>  Set logfile logging to the specified level\n");
	printf("  -s <errlev>  Set syslog logging to the specified level\n");
	printf("<errlev> is an integer between 0 and 7 with higher numbers providing more detail.\n");
}

int parse_commandline_args(int argc, char **argv,
			   slurmd_config_t * slurmd_config)
{
	int c;
	int digit_optind = 0;
	int errlev;
	opterr = 0;

	while (1) {
		int this_option_optind = optind ? optind : 1;
		int option_index = 0;
		static struct option long_options[] = {
			{"error_level", 1, 0, 'e'},
			{"help", 0, 0, 'h'},
			{"daemonize", 0, 0, 'd'},
			{"config_file", 1, 0, 'f'},
			{"log_level", 1, 0, 'l'},
			{"syslog_level", 1, 0, 's'},
			{0, 0, 0, 0}
		};

		c = getopt_long(argc, argv, "de:hf:l:s:", long_options,
				&option_index);
		if (c == -1)
			break;


		switch (c) {
		case 'e':
			errlev = strtol(optarg, (char **) NULL, 10);
			if ((errlev < LOG_LEVEL_QUIET) ||
			    (errlev > LOG_LEVEL_DEBUG3)) {
				fprintf(stderr,
					"invalid errlev argument\n");
				usage(argv[0]);
				exit(1);
			}
			slurmd_config->log_opts.stderr_level = errlev;
			break;
		case 'd':
			slurmd_config->daemonize = true;
			break;
		case 'h':
			usage(argv[0]);
			exit(0);
			break;
		case 'f':
			slurmd_config->slurm_conf = optarg;
			printf("slurmctrld.slurm_conf = %s\n",
			       slurmd_config->slurm_conf);
			break;
		case 'l':
			errlev = strtol(optarg, (char **) NULL, 10);
			if ((errlev < LOG_LEVEL_QUIET) ||
			    (errlev > LOG_LEVEL_DEBUG3)) {
				fprintf(stderr,
					"invalid errlev argument\n");
				usage(argv[0]);
				exit(1);
			}
			slurmd_config->log_opts.logfile_level = errlev;
			break;
		case 's':
			errlev = strtol(optarg, (char **) NULL, 10);
			if ((errlev < LOG_LEVEL_QUIET) ||
			    (errlev > LOG_LEVEL_DEBUG3)) {
				fprintf(stderr,
					"invalid errlev argument\n");
				usage(argv[0]);
				exit(1);
			}
			slurmd_config->log_opts.syslog_level = errlev;
			break;
		case 0:
			info("option %s", long_options[option_index].name);
			if (optarg) {
				info(" with arg %s", optarg);
			}
			break;

		case '0':
		case '1':
		case '2':
			if (digit_optind != 0
			    && digit_optind != this_option_optind) {
				info("digits occur in two different argv-elements.");
			}
			digit_optind = this_option_optind;
			info("option %c\n", c);
			break;
		case '?':
			info("?? getopt returned character code 0%o ??",
			     c);
			break;

		default:
			info("?? getopt returned character code 0%o ??",
			     c);
			usage(argv[0]);
			exit(1);
		}

		if (optind < argc) {
			printf("non-option ARGV-elements: ");
			while (optind < argc) {
				printf("%s ", argv[optind++]);
			}
			printf("\n");
			usage(argv[0]);
			exit(1);
		}
	}
	return SLURM_SUCCESS;
}

/* reset_cwd - reset the current working directory per slurm configuration file 
 *	this makes the core file go to StateSaveLocation if a daemon */
void 
reset_cwd(void)
{
	char *dir;

	dir = state_save_location ();
	if (dir == NULL)
		error ("No state save location specified in configuration file");
	else {
		if (chdir (dir))
			error ("chdir to %s error %m", dir);
debug ("chdir %s", dir);
		xfree (dir);
	}
}

/* state_save_location - returns the value of StateSaveLocation from the slurm configuration file
 *	NOTE: The caller must xfree the return value */
char *
state_save_location (void)
{
	FILE *slurm_spec_file;
	char in_line[BUF_SIZE];	/* input line */
	char *dir = NULL;
	int i, j, error_code, line_num = 0;

        slurm_spec_file = fopen (SLURM_CONFIG_FILE, "r");
	if (slurm_spec_file == NULL) {
		error ( "state_save_location error %d opening file %s: %m",
			errno, SLURM_CONFIG_FILE);
		return NULL ;
	}

	while (fgets (in_line, BUF_SIZE, slurm_spec_file) != NULL) {
		line_num++;
		if (strlen (in_line) >= (BUF_SIZE - 1)) {
			error ("state_save_location line %d, of input file %s too long\n",
				 line_num, SLURM_CONFIG_FILE);
			fclose (slurm_spec_file);
			return NULL;
		}		

		/* everything after a non-escaped "#" is a comment */
		/* replace comment flag "#" with an end of string (NULL) */
		for (i = 0; i < BUF_SIZE; i++) {
			if (in_line[i] == (char) NULL)
				break;
			if (in_line[i] != '#')
				continue;
			if ((i > 0) && (in_line[i - 1] == '\\')) {	/* escaped "#" */
				for (j = i; j < BUF_SIZE; j++) {
					in_line[j - 1] = in_line[j];
				}	
				continue;
			}	
			in_line[i] = (char) NULL;
			break;
		}		

		/* parse what is left */
		/* overall slurm configuration parameters */
		error_code = slurm_parser(in_line,
			"StateSaveLocation=", 's', &dir, 
			"END");
		if (error_code) {
			error ("error parsing configuration file input line %d", line_num);
			fclose (slurm_spec_file);
			return NULL;
		}		

		if ( dir ) {
			fclose (slurm_spec_file);
			return dir;	
		}
	}			
	return NULL;
}
