/*****************************************************************************\
 *  slurm_protocol_api.c - high-level slurm communication functions
 *  $Id$
 *****************************************************************************
 *  Copyright (C) 2002 The Regents of the University of California.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Kevin Tew <tew1@llnl.gov>, et. al.
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

/* GLOBAL INCLUDES */

#ifdef WITH_PTHREADS
#  include <pthread.h>
#endif /* WITH_PTHREADS */

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

/* PROJECT INCLUDES */
#include "src/common/macros.h"
#include "src/common/pack.h"
#include "src/common/parse_spec.h"
#include "src/common/read_config.h"
#include "src/common/slurm_auth.h"
#include "src/common/slurm_protocol_interface.h"
#include "src/common/slurm_protocol_api.h"
#include "src/common/slurm_protocol_common.h"
#include "src/common/slurm_protocol_pack.h"
#include "src/common/xmalloc.h"
#include "src/common/xstring.h"
#include "src/common/log.h"

/* STRUCTURES */

typedef struct forward_message {
	header_t header;
	char *buf;
	int buf_len;
	slurm_addr addr;
	int timeout;
	List ret_list;
	pthread_mutex_t *forward_mutex;
	pthread_cond_t *notify;
	char node_name[MAX_NAME_LEN];
} forward_msg_t;

typedef struct forward_struct {
	int timeout;
	int forward;
	pthread_mutex_t forward_mutex;
	pthread_cond_t notify;
	forward_msg_t *forward_msg;
	header_t header;
	Buf buffer;
} forward_struct_t;



/* EXTERNAL VARIABLES */

/* #DEFINES */
#define _DEBUG	0
#define MAX_SHUTDOWN_RETRY 5
#define BUF_SIZE 4096
#define MAX_RETRIES 3

/* STATIC VARIABLES */
static pthread_mutex_t config_lock = PTHREAD_MUTEX_INITIALIZER;
static slurm_protocol_config_t proto_conf_default;
static slurm_protocol_config_t *proto_conf = &proto_conf_default;
static slurm_ctl_conf_t slurmctld_conf;

/* STATIC FUNCTIONS */
static void _remap_slurmctld_errno(void);
static int _forward_msg(forward_struct_t *forward_struct, header_t *header);

/**********************************************************************\
 * protocol configuration functions
\**********************************************************************/
/* slurm_set_api_config
 * sets the slurm_protocol_config object
 * NOT THREAD SAFE
 * IN protocol_conf		-  slurm_protocol_config object
 *
 * XXX: Why isn't the "config_lock" mutex used here?
 */
int slurm_set_api_config(slurm_protocol_config_t * protocol_conf)
{
	proto_conf = protocol_conf;
	return SLURM_SUCCESS;
}

/* slurm_get_api_config
 * returns a pointer to the current slurm_protocol_config object
 * RET slurm_protocol_config_t  - current slurm_protocol_config object
 */
slurm_protocol_config_t *slurm_get_api_config()
{
	return proto_conf;
}

/* slurm_api_set_conf_file
 *      set slurm configuration file to a non-default value
 * pathname IN - pathname of slurm configuration file to be used
 */
extern void  slurm_api_set_conf_file(char *pathname)
{
	if (pathname == NULL)
		return;
	xfree(slurmctld_conf.slurm_conf);
	slurmctld_conf.slurm_conf = xstrdup(pathname);
	return;
}

/* slurm_api_set_default_config
 *      called by the send_controller_msg function to insure that at least 
 *	the compiled in default slurm_protocol_config object is initialized
 * RET int		 - return code
 */
int slurm_api_set_default_config()
{
	int rc = SLURM_SUCCESS;
	struct stat config_stat;
	static time_t last_config_update = (time_t) 0;

	slurm_mutex_lock(&config_lock);
	if (slurmctld_conf.slurm_conf
	&&  (stat(slurmctld_conf.slurm_conf, &config_stat) < 0)) {
		error("Can't stat %s: %m", slurmctld_conf.slurm_conf);
		rc = SLURM_ERROR;
		goto cleanup;
	}

	if (last_config_update
	&&  (slurmctld_conf.slurm_conf
	&&   (last_config_update == config_stat.st_mtime))
	&&  slurmctld_conf.control_addr
	&&  slurmctld_conf.slurmctld_port)
  		goto cleanup;

	last_config_update = config_stat.st_mtime;
	init_slurm_conf(&slurmctld_conf);
	read_slurm_conf_ctl(&slurmctld_conf, false);

	if ((slurmctld_conf.control_addr == NULL) ||
	    (slurmctld_conf.slurmctld_port == 0)) {
		error("Unable to establish control machine or port");
		rc = SLURM_ERROR;
		goto cleanup;
	}

	slurm_set_addr(&proto_conf_default.primary_controller,
		       slurmctld_conf.slurmctld_port,
		       slurmctld_conf.control_addr);
	if (proto_conf_default.primary_controller.sin_port == 0) {
		error("Unable to establish control machine address");
		rc = SLURM_ERROR;
		goto cleanup;
	}

	if (slurmctld_conf.backup_addr) {
		slurm_set_addr(&proto_conf_default.secondary_controller,
			       slurmctld_conf.slurmctld_port,
			       slurmctld_conf.backup_addr);
	}
	proto_conf = &proto_conf_default;

      cleanup:
	slurm_mutex_unlock(&config_lock);
	return rc;
}

/* slurm_api_clear_config
 * execute this only at program termination to free all memory */
void slurm_api_clear_config(void)
{
	slurm_mutex_lock(&config_lock);
	slurmctld_conf.slurmd_port = 0;
	free_slurm_conf(&slurmctld_conf);
	slurm_mutex_unlock(&config_lock);
}

/* update internal configuration data structure as needed.
 *	exit with lock set */
static inline void _lock_update_config()
{
	slurm_api_set_default_config();
	slurm_mutex_lock(&config_lock);
}

/* slurm_get_mpi_default
 * get default mpi value from slurmctld_conf object
 * RET char *   - mpi default value from slurm.conf,  MUST be xfreed by caller
 */
char *slurm_get_mpi_default(void)
{
	char *mpi_default;

	_lock_update_config();
	mpi_default = xstrdup(slurmctld_conf.mpi_default);
	slurm_mutex_unlock(&config_lock);
	return mpi_default;
}

/* slurm_get_plugin_dir
 * get plugin directory from slurmctld_conf object
 * RET char *   - plugin directory, MUST be xfreed by caller
 */
char *slurm_get_plugin_dir(void)
{
	char *plugin_dir;

	_lock_update_config();
	plugin_dir = xstrdup(slurmctld_conf.plugindir);
	slurm_mutex_unlock(&config_lock);
	return plugin_dir;
}

/* slurm_get_auth_type
 * returns the authentication type from slurmctld_conf object
 * RET char *    - auth type, MUST be xfreed by caller
 */
char *slurm_get_auth_type(void)
{
	char *auth_type;

	_lock_update_config();
	auth_type = xstrdup(slurmctld_conf.authtype);
	slurm_mutex_unlock(&config_lock);
	return auth_type;
}

/* slurm_get_fast_schedule
 * returns the value of fast_schedule in slurmctld_conf object
 */
extern uint16_t slurm_get_fast_schedule(void)
{
	uint16_t fast_val;

	_lock_update_config();
	fast_val = slurmctld_conf.fast_schedule;
	slurm_mutex_unlock(&config_lock);
	return fast_val;
}

/* slurm_set_auth_type
 * set the authentication type in slurmctld_conf object
 * used for security testing purposes
 * RET 0 or error code
 */
extern int slurm_set_auth_type(char *auth_type)
{
	_lock_update_config();
	xfree(slurmctld_conf.authtype);
	slurmctld_conf.authtype = xstrdup(auth_type);
	slurm_mutex_unlock(&config_lock);
	return 0;
}

/* slurm_get_jobacct_loc
 * returns the job accounting loc from the slurmctld_conf object
 * RET char *    - job accounting loc,  MUST be xfreed by caller
 */
char *slurm_get_jobacct_loc(void)
{
	char *jobacct_loc;

	_lock_update_config();
	jobacct_loc = xstrdup(slurmctld_conf.job_acct_loc);
	slurm_mutex_unlock(&config_lock);
	return jobacct_loc;
}

/* slurm_get_jobacct_parameters
 * returns the job accounting parameters from the slurmctld_conf object
 * RET char *    - job accounting parameters,  MUST be xfreed by caller
 */
char *slurm_get_jobacct_parameters(void)
{
	char *jobacct_parameters;

	_lock_update_config();
	jobacct_parameters = xstrdup(slurmctld_conf.job_acct_parameters);
	slurm_mutex_unlock(&config_lock);
	return jobacct_parameters;
}

/* slurm_get_jobacct_type
 * returns the job accounting type from the slurmctld_conf object
 * RET char *    - job accounting type,  MUST be xfreed by caller
 */
char *slurm_get_jobacct_type(void)
{
	char *jobacct_type;

	_lock_update_config();
	jobacct_type = xstrdup(slurmctld_conf.job_acct_type);
	slurm_mutex_unlock(&config_lock);
	return jobacct_type;
}

/* slurm_get_jobcomp_type
 * returns the job completion logger type from slurmctld_conf object
 * RET char *    - job completion type,  MUST be xfreed by caller
 */
char *slurm_get_jobcomp_type(void)
{
	char *jobcomp_type;

	_lock_update_config();
	jobcomp_type = xstrdup(slurmctld_conf.job_comp_type);
	slurm_mutex_unlock(&config_lock);
	return jobcomp_type;
}

/* slurm_get_proctrack_type
 * get ProctrackType from slurmctld_conf object
 * RET char *   - proctrack type, MUST be xfreed by caller
 */
char *slurm_get_proctrack_type(void)
{
	char *proctrack_type;

	_lock_update_config();
	proctrack_type = xstrdup(slurmctld_conf.proctrack_type);
	slurm_mutex_unlock(&config_lock);
	return proctrack_type;
}

/* slurm_get_slurmd_port
 * returns slurmd port from slurmctld_conf object
 * RET uint16_t	- slurmd port
 */
uint16_t slurm_get_slurmd_port(void)
{
	uint16_t slurmd_port;

	_lock_update_config();
	slurmd_port = slurmctld_conf.slurmd_port;
	slurm_mutex_unlock(&config_lock);
	return slurmd_port;
}

/* slurm_get_slurm_user_id
 * returns slurmd uid from slurmctld_conf object
 * RET uint32_t	- slurm user id
 */
uint32_t slurm_get_slurm_user_id(void)
{
	uint32_t slurm_uid;

	_lock_update_config();
	slurm_uid = slurmctld_conf.slurm_user_id;
	slurm_mutex_unlock(&config_lock);
	return slurm_uid;
}

/* slurm_get_sched_type
 * get sched type from slurmctld_conf object
 * RET char *   - sched type, MUST be xfreed by caller
 */
char *slurm_get_sched_type(void)
{
	char *sched_type;

	_lock_update_config();
	sched_type = xstrdup(slurmctld_conf.schedtype);
	slurm_mutex_unlock(&config_lock);
	return sched_type;
}

/* slurm_get_select_type
 * get select_type from slurmctld_conf object
 * RET char *   - select_type, MUST be xfreed by caller
 */
char *slurm_get_select_type(void)
{
	char *select_type;

	_lock_update_config();
	select_type = xstrdup(slurmctld_conf.select_type);
	slurm_mutex_unlock(&config_lock);
	return select_type;
}

/* slurm_get_switch_type
 * get switch type from slurmctld_conf object
 * RET char *   - switch type, MUST be xfreed by caller
 */
char *slurm_get_switch_type(void)
{
	char *switch_type;

	_lock_update_config();
	switch_type = xstrdup(slurmctld_conf.switch_type);
	slurm_mutex_unlock(&config_lock);
	return switch_type;
}

/* slurm_get_wait_time
 * returns wait_time from slurmctld_conf object
 * RET uint16_t	- wait_time
 */
uint16_t slurm_get_wait_time(void)
{
	uint16_t wait_time;

	_lock_update_config();
	wait_time = slurmctld_conf.wait_time;
	slurm_mutex_unlock(&config_lock);
	return wait_time;
}

/* slurm_get_srun_prolog
 * return the name of the srun prolog program
 * RET char *   - name of prolog program, must be xfreed by caller
 */
char *slurm_get_srun_prolog(void)
{
	char *prolog;

	_lock_update_config();
	prolog = xstrdup(slurmctld_conf.srun_prolog);
	slurm_mutex_unlock(&config_lock);
	return prolog;
}

/* slurm_get_srun_epilog
 * return the name of the srun epilog program
 * RET char *   - name of epilog program, must be xfreed by caller
 */
char *slurm_get_srun_epilog(void)
{
	char *epilog;

	_lock_update_config();
	epilog = xstrdup(slurmctld_conf.srun_epilog);
	slurm_mutex_unlock(&config_lock);
	return epilog;
}

/* slurm_get_task_epilog
 * RET task_epilog name, must be xfreed by caller */
char *slurm_get_task_epilog(void)
{
        char *task_epilog;

        _lock_update_config();
        task_epilog = xstrdup(slurmctld_conf.task_epilog);
        slurm_mutex_unlock(&config_lock);
        return task_epilog;
}

/* slurm_get_task_prolog
 * RET task_prolog name, must be xfreed by caller */
char *slurm_get_task_prolog(void)
{
        char *task_prolog;
        
	_lock_update_config();
        task_prolog = xstrdup(slurmctld_conf.task_prolog);
        slurm_mutex_unlock(&config_lock);
        return task_prolog;
}

/* slurm_get_task_plugin
 * RET task_plugin name, must be xfreed by caller */
char *slurm_get_task_plugin(void)
{
        char *task_plugin;

        _lock_update_config();
        task_plugin = xstrdup(slurmctld_conf.task_plugin);
        slurm_mutex_unlock(&config_lock);
        return task_plugin;
}
/* Change general slurm communication errors to slurmctld specific errors */
static void _remap_slurmctld_errno(void)
{
	int err = slurm_get_errno();

	if (err == SLURM_COMMUNICATIONS_CONNECTION_ERROR)
		slurm_seterrno(SLURMCTLD_COMMUNICATIONS_CONNECTION_ERROR);
	else if (err ==  SLURM_COMMUNICATIONS_SEND_ERROR)
		slurm_seterrno(SLURMCTLD_COMMUNICATIONS_SEND_ERROR);
	else if (err == SLURM_COMMUNICATIONS_RECEIVE_ERROR)
		slurm_seterrno(SLURMCTLD_COMMUNICATIONS_RECEIVE_ERROR);
	else if (err == SLURM_COMMUNICATIONS_SHUTDOWN_ERROR)
		slurm_seterrno(SLURMCTLD_COMMUNICATIONS_SHUTDOWN_ERROR);
}

void *_forward_thread(void *arg)
{
	forward_msg_t *fwd_msg = (forward_msg_t *)arg;
	unsigned int tmplen, msglen;
	Buf buffer = init_buf(0);
	int retry = 0;
	List ret_list = NULL;
	ret_types_t *type = NULL;
	ret_types_t *returned_type = NULL;
	slurm_msg_t *msg = xmalloc(sizeof(slurm_msg_t));
	slurm_fd fd;
	char *name;
	ListIterator itr;
	
	if ((fd = slurm_open_msg_conn(&fwd_msg->addr)) < 0) {
		error("forward_thread: can't open msg conn");
		goto cleanup;
	}
	pack_header(&fwd_msg->header, buffer);
	
	tmplen = get_buf_offset(buffer);

	/* add forward data to buffer */
	if (remaining_buf(buffer) < fwd_msg->buf_len) {
		buffer->size += (fwd_msg->buf_len + BUF_SIZE);
		xrealloc(buffer->head, buffer->size);
	}
	if (fwd_msg->buf_len) {
		memcpy(&buffer->head[buffer->processed], 
		       fwd_msg->buf, fwd_msg->buf_len);
		buffer->processed += fwd_msg->buf_len;
	}
	
	/*
	 * forward message
	 */
	if((_slurm_msg_sendto(fd, 
			      get_buf_data(buffer), 
			      get_buf_offset(buffer),
			      SLURM_PROTOCOL_NO_SEND_RECV_FLAGS ) < 0)) {
		error("slurm_msg_sendto: %m");
	}

	msg->forward_cnt = fwd_msg->header.forward_cnt;
	msg->forward_addr = fwd_msg->header.forward_addr;
	msg->ret_list = fwd_msg->header.ret_list;

	ret_list = slurm_receive_msg(fd, msg, fwd_msg->timeout);
	
	info("got %d returns back", list_count(ret_list));
	
	type = xmalloc(sizeof(ret_types_t));
	list_push(ret_list, type);
	type->type = msg->msg_type;
	type->msg_rc = ((return_code_msg_t *)msg->data)->return_code;
	type->err = errno;
	type->names = list_create(destroy_names);
	name = xstrdup(fwd_msg->node_name);
	list_push(type->names, name);
		
	
	if ((fd >= 0) && slurm_close_accepted_conn(fd) < 0)
		error ("close(%d): %m", fd);

	info("now %d returns",list_count(ret_list));
	
	while((returned_type = list_pop(ret_list)) != NULL) {
		pthread_mutex_lock(fwd_msg->forward_mutex);
		itr = list_iterator_create(fwd_msg->ret_list);	
		while((type = (ret_types_t *) 
		       list_next(itr)) != NULL) {
			if(type->msg_rc == returned_type->msg_rc) {
				while(name = list_pop(returned_type->names)) 
					list_push(type->names, name);
				list_destroy(returned_type->names);
				break;
			}
		}
		if(!type) {
			type = xmalloc(sizeof(ret_types_t));
			list_push(fwd_msg->ret_list, type);
			type->type = returned_type->type;
			type->msg_rc = returned_type->msg_rc;
			type->err = returned_type->err;
			type->names = list_create(destroy_names);
			while(name = list_pop(returned_type->names)) 
				list_push(type->names, name);
			
		}
		list_iterator_destroy(itr);
		destroy_ret_types(returned_type);
		pthread_mutex_unlock(fwd_msg->forward_mutex);
	}
	pthread_cond_signal(fwd_msg->notify);

	list_destroy(ret_list);
cleanup:
	xfree(fwd_msg->buf);
	//xfree(fwd_msg->addr);
	free_buf(buffer);	
}

static int _forward_msg(forward_struct_t *forward_struct, header_t *header)
{
	header_t forward_header;
	int i;
	int retries = 0;
	forward_msg_t *forward_msg;
	Buf buffer = forward_struct->buffer;

	for(i=0; i<forward_struct->forward; i++) {
		pthread_attr_t attr_agent;
		pthread_t thread_agent;
		
		slurm_attr_init(&attr_agent);
		if (pthread_attr_setdetachstate
		    (&attr_agent, PTHREAD_CREATE_DETACHED))
			error("pthread_attr_setdetachstate error %m");
		
		forward_msg = &forward_struct->forward_msg[i];
		forward_msg->header = forward_struct->header;
			
		forward_msg->addr = header->forward_addr[i];
		forward_msg->buf_len = remaining_buf(buffer);
		forward_msg->buf = 
			xmalloc(sizeof(char)*forward_msg->buf_len);
		memcpy(forward_msg->buf, 
		       &buffer->head[buffer->processed], 
		       forward_msg->buf_len);
			
		while(pthread_create(&thread_agent, &attr_agent,
				   _forward_thread, 
				   (void *)forward_msg)) {
			error("pthread_create error %m");
			if (++retries > MAX_RETRIES)
				fatal("Can't create pthread");
			sleep(1);	/* sleep and try again */
		}
	}
	return SLURM_SUCCESS;
}
/**********************************************************************\
 * general message management functions used by slurmctld, slurmd
\**********************************************************************/

/* 
 *  Initialize a slurm server at port "port"
 * 
 * IN  port     - port to bind the msg server to
 * RET slurm_fd - file descriptor of the connection created
 */
slurm_fd slurm_init_msg_engine_port(uint16_t port)
{
	slurm_addr addr;

	slurm_set_addr_any(&addr, port);
	return _slurm_init_msg_engine(&addr);
}

/* 
 *  Same as above, but initialize using a slurm address "addr"
 *
 * IN  addr     - slurm_addr to bind the msg server to 
 * RET slurm_fd - file descriptor of the connection created
 */
slurm_fd slurm_init_msg_engine(slurm_addr *addr)
{
	return _slurm_init_msg_engine(addr);
}

/* 
 *  Close an established message engine.
 *    Returns SLURM_SUCCESS or SLURM_FAILURE.
 *
 * IN  fd  - an open file descriptor to close
 * RET int - the return code
 */
int slurm_shutdown_msg_engine(slurm_fd fd)
{
	int rc = _slurm_close(fd);
	if (rc)
		slurm_seterrno(SLURM_COMMUNICATIONS_SHUTDOWN_ERROR);
	return rc;
}

/* 
 *   Close an established message connection.
 *     Returns SLURM_SUCCESS or SLURM_FAILURE.
 *
 * IN  fd  - an open file descriptor to close
 * RET int - the return code
 */
int slurm_shutdown_msg_conn(slurm_fd fd)
{
	return _slurm_close(fd);
}

/**********************************************************************\
 * msg connection establishment functions used by msg clients
\**********************************************************************/

/* In the bsd socket implementation it creates a SOCK_STREAM socket  
 *	and calls connect on it a SOCK_DGRAM socket called with connect   
 *	is defined to only receive messages from the address/port pair  
 *	argument of the connect call slurm_address - for now it is  
 *	really just a sockaddr_in
 * IN slurm_address     - slurm_addr of the connection destination
 * RET slurm_fd         - file descriptor of the connection created
 */
slurm_fd slurm_open_msg_conn(slurm_addr * slurm_address)
{
	return _slurm_open_msg_conn(slurm_address);
}

/* calls connect to make a connection-less datagram connection to the 
 *	primary or secondary slurmctld message engine
 * RET slurm_fd	- file descriptor of the connection created
 */
slurm_fd slurm_open_controller_conn()
{
	slurm_fd fd;

	if (slurm_api_set_default_config() < 0)
		return SLURM_FAILURE;

	if ((fd = slurm_open_msg_conn(&proto_conf->primary_controller)) >= 0)
		return fd;
	
	debug("Failed to contact primary controller: %m");

	if (!slurmctld_conf.backup_controller) 
		goto fail;

	if ((fd = slurm_open_msg_conn(&proto_conf->secondary_controller)) >= 0)
		return fd;

	debug("Failed to contact secondary controller: %m");

    fail:
	slurm_seterrno_ret(SLURMCTLD_COMMUNICATIONS_CONNECTION_ERROR);
}

/* calls connect to make a connection-less datagram connection to the 
 *	primary or secondary slurmctld message engine
 * RET slurm_fd - file descriptor of the connection created
 * IN dest      - controller to contact, primary or secondary
 */
slurm_fd slurm_open_controller_conn_spec(enum controller_id dest)
{
	slurm_addr *addr;
	slurm_fd rc;

	if (slurm_api_set_default_config() < 0) {
		debug3("Error: Unable to set default config");
		return SLURM_ERROR;
	}
		
	addr = (dest == PRIMARY_CONTROLLER) ? 
		  &proto_conf->primary_controller : 
		  &proto_conf->secondary_controller;

	if (!addr) return SLURM_ERROR;

	rc = slurm_open_msg_conn(addr);
	if (rc == -1)
		_remap_slurmctld_errno();
	return rc;
}

/* In the bsd implmentation maps directly to a accept call 
 * IN open_fd		- file descriptor to accept connection on
 * OUT slurm_address	- slurm_addr of the accepted connection
 * RET slurm_fd		- file descriptor of the connection created
 */
slurm_fd slurm_accept_msg_conn(slurm_fd open_fd,
			       slurm_addr * slurm_address)
{
	return _slurm_accept_msg_conn(open_fd, slurm_address);
}

/* In the bsd implmentation maps directly to a close call, to close 
 *	the socket that was accepted
 * IN open_fd		- an open file descriptor to close
 * RET int		- the return code
 */
int slurm_close_accepted_conn(slurm_fd open_fd)
{
	return _slurm_close_accepted_conn(open_fd);
}

/**********************************************************************\
 * receive message functions
\**********************************************************************/

/*
 * NOTE: memory is allocated for the returned msg and must be freed at 
 *	some point using the slurm_free_functions
 * IN open_fd	- file descriptor to receive msg on
 * OUT msg	- a slurm_msg struct to be filled in by the function
 * RET int	- size of msg received in bytes before being unpacked
 */
List slurm_receive_msg(slurm_fd fd, slurm_msg_t *msg, int timeout)
{
	char *buf = NULL;
	size_t buflen = 0;
	header_t header;
	int rc;
	void *auth_cred = NULL;
	Buf buffer;

	forward_struct_t *forward_struct = NULL;
	ret_types_t *ret_type = NULL;

	int i=0;
	List ret_list = list_create(destroy_ret_types);
	
	xassert(fd >= 0);
	
	if ((timeout*=1000) == 0)
		timeout = SLURM_MESSAGE_TIMEOUT_MSEC_STATIC;

	/*
	 * Receive a msg. slurm_msg_recvfrom() will read the message
	 *  length and allocate space on the heap for a buffer containing
	 *  the message. 
	 */
	if (_slurm_msg_recvfrom_timeout(fd, &buf, &buflen, 0, timeout) < 0) {
		rc = SLURM_ERROR;
		goto total_return;
	}
#if	_DEBUG
	_print_data (buftemp, rc);
#endif
	buffer = create_buf(buf, buflen);

	unpack_header(&header, buffer);
	
	if (check_header_version(&header) < 0) {
		free_buf(buffer);
		rc = SLURM_PROTOCOL_VERSION_ERROR;
		goto total_return;
	}
	info("replies from %d forwards %d", 
	     header.ret_cnt, header.forward_cnt);
	if(header.ret_cnt > 0) {
		while((ret_type = list_pop(header.ret_list)) != NULL)
			list_push(ret_list, ret_type);
		header.ret_cnt = 0;
		list_destroy(header.ret_list);
		header.ret_list = NULL;
	}
	/* Forward message to other nodes */
	if(header.forward_cnt > 0) {
		forward_struct = xmalloc(sizeof(forward_struct_t));
		slurm_mutex_init(&forward_struct->forward_mutex);
		pthread_cond_init(&forward_struct->notify, NULL);
	
		forward_struct->forward_msg = 
			xmalloc(sizeof(forward_msg_t) * header.forward_cnt);
		for(i=0; i< header.forward_cnt; i++) {
			strncpy(forward_struct->forward_msg[i].node_name,
				&header.forward_name[i * MAX_NAME_LEN],
				MAX_NAME_LEN);
			forward_struct->forward_msg[i].ret_list = ret_list;
			forward_struct->forward_msg[i].notify = 
				&forward_struct->notify;
			forward_struct->forward_msg[i].forward_mutex = 
				&forward_struct->forward_mutex;
		}
		xfree(header.forward_name);
		forward_struct->forward = header.forward_cnt;
		forward_struct->header.version = header.version;
		forward_struct->header.flags = header.flags;
		forward_struct->header.msg_type = header.msg_type;
		forward_struct->header.body_length = header.body_length;
		/*FIXME: !!!!! */
		/*find out these if needed */
		/* forward_header.forward_cnt = msg->forward_cnt; */
		/* 	forward_header.forward_addr = msg->forward_addr; */
		forward_struct->header.forward_cnt = 0;
		forward_struct->header.forward_addr = NULL;
		forward_struct->header.ret_cnt = 0;
		forward_struct->header.ret_list = NULL;
		forward_struct->buffer = buffer;
		info("forwarding messages to %d nodes!!!!", 
		     header.forward_cnt);
		
		if(_forward_msg(forward_struct, &header) == SLURM_ERROR) {
			error("problem with forward msg");
		}
	}

	if ((auth_cred = g_slurm_auth_unpack(buffer)) == NULL) {
		error( "authentication: %s ",
			g_slurm_auth_errstr(g_slurm_auth_errno(NULL)));
		free_buf(buffer);
		rc = ESLURM_PROTOCOL_INCOMPLETE_PACKET;
		goto total_return;
	}
	rc = g_slurm_auth_verify( auth_cred, NULL, 2 );
	
	if (rc != SLURM_SUCCESS) {
		error( "authentication: %s ",
		       g_slurm_auth_errstr(g_slurm_auth_errno(auth_cred)));
		(void) g_slurm_auth_destroy(auth_cred);
		free_buf(buffer);
		rc = SLURM_PROTOCOL_AUTHENTICATION_ERROR;
		goto total_return;
	}	

	/*
	 * Unpack message body 
	 */
	msg->msg_type = header.msg_type;
	info("mssage type = %d",msg->msg_type);
	if ( (header.body_length > remaining_buf(buffer)) ||
	     (unpack_msg(msg, buffer) != SLURM_SUCCESS) ) {
		(void) g_slurm_auth_destroy(auth_cred);
		free_buf(buffer);
		rc = ESLURM_PROTOCOL_INCOMPLETE_PACKET;
		goto total_return;
	}

	msg->cred = (void *) auth_cred;

	free_buf(buffer);
	rc = SLURM_SUCCESS;
	if(forward_struct) {
		pthread_mutex_lock(&forward_struct->forward_mutex);
		while(list_count(ret_list) < (header.forward_cnt)) {
			pthread_cond_wait(&forward_struct->notify, 
					  &forward_struct->forward_mutex);
			info("got %d out of %d forwarded messages",
			     list_count(ret_list), 
			     (header.forward_cnt));
		}
		pthread_mutex_unlock(&forward_struct->forward_mutex);
	}
		
total_return:
	
	if(rc != SLURM_SUCCESS) {
		error("slurm_receive_msg: %s", slurm_strerror(rc));
	}
	errno = rc;
	return ret_list;
		
}

/**********************************************************************\
 * send message functions
\**********************************************************************/

/*
 *  Do the wonderful stuff that needs be done to pack msg
 *  and hdr into buffer
 */
static void
_pack_msg(slurm_msg_t *msg, header_t *hdr, Buf buffer)
{
	unsigned int tmplen, msglen;

	tmplen = get_buf_offset(buffer);
	pack_msg(msg, buffer);
	msglen = get_buf_offset(buffer) - tmplen;

	/* update header with correct cred and msg lengths */
	update_header(hdr, msglen);
	
	/* repack updated header */
	tmplen = get_buf_offset(buffer);
	set_buf_offset(buffer, 0);
	pack_header(hdr, buffer);
	set_buf_offset(buffer, tmplen);
}


/* 
 *  Send a slurm message over an open file descriptor `fd'
 *    Returns the size of the message sent in bytes, or -1 on failure.
 */
int slurm_send_node_msg(slurm_fd fd, slurm_msg_t * msg)
{
	header_t header;
	Buf      buffer;
	int      rc;
	void *   auth_cred;

	/* 
	 * Initialize header with Auth credential and message type.
	 */
	auth_cred = g_slurm_auth_create(NULL, 2);
	if (auth_cred == NULL) {
		error("authentication: %s",
		       g_slurm_auth_errstr(g_slurm_auth_errno(NULL)) );
		slurm_seterrno_ret(SLURM_PROTOCOL_AUTHENTICATION_ERROR);
	}

	init_header(&header, msg, SLURM_PROTOCOL_NO_FLAGS);

	/*
	 * Pack header into buffer for transmission
	 */
	buffer = init_buf(0);
	pack_header(&header, buffer);

	/* 
	 * Pack auth credential
	 */
	rc = g_slurm_auth_pack(auth_cred, buffer);
	(void) g_slurm_auth_destroy(auth_cred);
	if (rc) {
		error("authentication: %s",
		       g_slurm_auth_errstr(g_slurm_auth_errno(auth_cred)));
		free_buf(buffer);
		slurm_seterrno_ret(SLURM_PROTOCOL_AUTHENTICATION_ERROR);
	}
	
	/*
	 * Pack message into buffer
	 */
	_pack_msg(msg, &header, buffer);

#if	_DEBUG
	_print_data (get_buf_data(buffer),get_buf_offset(buffer));
#endif
	/*
	 * Send message
	 */
	rc = _slurm_msg_sendto( fd, get_buf_data(buffer), 
				get_buf_offset(buffer),
				SLURM_PROTOCOL_NO_SEND_RECV_FLAGS );

	if (rc < 0) 
		error("slurm_msg_sendto: %m");

	free_buf(buffer);
	return rc;
}

/**********************************************************************\
 * stream functions
\**********************************************************************/

/* slurm_listen_stream
 * opens a stream server and listens on it
 * IN slurm_address	- slurm_addr to bind the server stream to
 * RET slurm_fd		- file descriptor of the stream created
 */
slurm_fd slurm_listen_stream(slurm_addr * slurm_address)
{
	return _slurm_listen_stream(slurm_address);
}

/* slurm_accept_stream
 * accepts a incomming stream connection on a stream server slurm_fd 
 * IN open_fd		- file descriptor to accept connection on
 * OUT slurm_address	- slurm_addr of the accepted connection
 * RET slurm_fd		- file descriptor of the accepted connection 
 */
slurm_fd slurm_accept_stream(slurm_fd open_fd, slurm_addr * slurm_address)
{
	return _slurm_accept_stream(open_fd, slurm_address);
}

/* slurm_open_stream
 * opens a client connection to stream server
 * IN slurm_address     - slurm_addr of the connection destination
 * RET slurm_fd         - file descriptor of the connection created
 * NOTE: Retry with various ports as needed if connection is refused
 */
slurm_fd slurm_open_stream(slurm_addr * slurm_address)
{
	return _slurm_open_stream(slurm_address, true);
}

/* slurm_write_stream
 * writes a buffer out a stream file descriptor
 * IN open_fd		- file descriptor to write on
 * IN buffer		- buffer to send
 * IN size		- size of buffer send
 * IN timeout		- how long to wait in milliseconds
 * RET size_t		- bytes sent , or -1 on errror
 */
size_t slurm_write_stream(slurm_fd open_fd, char *buffer, size_t size)
{
	return _slurm_send_timeout(open_fd, buffer, size,
				   SLURM_PROTOCOL_NO_SEND_RECV_FLAGS,
				   SLURM_MESSAGE_TIMEOUT_MSEC_STATIC);
}
size_t slurm_write_stream_timeout(slurm_fd open_fd, char *buffer,
				  size_t size, int timeout)
{
	return _slurm_send_timeout(open_fd, buffer, size,
				   SLURM_PROTOCOL_NO_SEND_RECV_FLAGS,
				   timeout);
}

/* slurm_read_stream
 * read into buffer grom a stream file descriptor
 * IN open_fd	- file descriptor to read from
 * OUT buffer   - buffer to receive into
 * IN size	- size of buffer
 * IN timeout	- how long to wait in milliseconds
 * RET size_t	- bytes read , or -1 on errror
 */
size_t slurm_read_stream(slurm_fd open_fd, char *buffer, size_t size)
{
	return _slurm_recv_timeout(open_fd, buffer, size,
				   SLURM_PROTOCOL_NO_SEND_RECV_FLAGS,
				   SLURM_MESSAGE_TIMEOUT_MSEC_STATIC);
}
size_t slurm_read_stream_timeout(slurm_fd open_fd, char *buffer,
				 size_t size, int timeout)
{
	return _slurm_recv_timeout(open_fd, buffer, size,
				   SLURM_PROTOCOL_NO_SEND_RECV_FLAGS,
				   timeout);
}

/* slurm_get_stream_addr
 * esentially a encapsilated get_sockname  
 * IN open_fd		- file descriptor to retreive slurm_addr for
 * OUT address		- address that open_fd to bound to
 */
int slurm_get_stream_addr(slurm_fd open_fd, slurm_addr * address)
{
	return _slurm_get_stream_addr(open_fd, address);
}

/* slurm_close_stream
 * closes either a server or client stream file_descriptor
 * IN open_fd	- an open file descriptor to close
 * RET int	- the return code
 */
int slurm_close_stream(slurm_fd open_fd)
{
	return _slurm_close_stream(open_fd);
}

/* make an open slurm connection blocking or non-blocking
 *	(i.e. wait or do not wait for i/o completion )
 * IN open_fd	- an open file descriptor to change the effect
 * RET int	- the return code
 */
int slurm_set_stream_non_blocking(slurm_fd open_fd)
{
	return _slurm_set_stream_non_blocking(open_fd);
}
int slurm_set_stream_blocking(slurm_fd open_fd)
{
	return _slurm_set_stream_blocking(open_fd);
}

/**********************************************************************\
 * address conversion and management functions
\**********************************************************************/

/* slurm_set_addr_uint
 * initializes the slurm_address with the supplied port and ip_address
 * OUT slurm_address	- slurm_addr to be filled in
 * IN port		- port in host order
 * IN ip_address	- ipv4 address in uint32 host order form
 */
void slurm_set_addr_uint(slurm_addr * slurm_address, uint16_t port,
			 uint32_t ip_address)
{
	_slurm_set_addr_uint(slurm_address, port, ip_address);
}

/* slurm_set_addr_any
 * initialized the slurm_address with the supplied port on INADDR_ANY
 * OUT slurm_address	- slurm_addr to be filled in
 * IN port		- port in host order
 */
void slurm_set_addr_any(slurm_addr * slurm_address, uint16_t port)
{
	_slurm_set_addr_uint(slurm_address, port, SLURM_INADDR_ANY);
}

/* slurm_set_addr
 * initializes the slurm_address with the supplied port and host name
 * OUT slurm_address	- slurm_addr to be filled in
 * IN port		- port in host order
 * IN host		- hostname or dns name 
 */
void slurm_set_addr(slurm_addr * slurm_address, uint16_t port, char *host)
{
	_slurm_set_addr_char(slurm_address, port, host);
}

/* reset_slurm_addr
 * resets the address field of a slurm_addr, port and family unchanged
 * OUT slurm_address	- slurm_addr to be reset in
 * IN new_address	- source of address to write into slurm_address
 */
void reset_slurm_addr(slurm_addr * slurm_address, slurm_addr new_address)
{
	_reset_slurm_addr(slurm_address, new_address);
}

/* slurm_set_addr_char
 * initializes the slurm_address with the supplied port and host
 * OUT slurm_address	- slurm_addr to be filled in
 * IN port		- port in host order
 * IN host		- hostname or dns name 
 */
void slurm_set_addr_char(slurm_addr * slurm_address, uint16_t port,
			 char *host)
{
	_slurm_set_addr_char(slurm_address, port, host);
}

/* slurm_get_addr 
 * given a slurm_address it returns its port and hostname
 * IN slurm_address	- slurm_addr to be queried
 * OUT port		- port number
 * OUT host		- hostname
 * IN buf_len		- length of hostname buffer
 */
void slurm_get_addr(slurm_addr * slurm_address, uint16_t * port,
		    char *host, unsigned int buf_len)
{
	_slurm_get_addr(slurm_address, port, host, buf_len);
}

/* slurm_get_ip_str 
 * given a slurm_address it returns its port and ip address string
 * IN slurm_address	- slurm_addr to be queried
 * OUT port		- port number
 * OUT ip		- ip address in dotted-quad string form
 * IN buf_len		- length of ip buffer
 */
void slurm_get_ip_str(slurm_addr * slurm_address, uint16_t * port,
		      char *ip, unsigned int buf_len)
{
	unsigned char *uc = (unsigned char *)&slurm_address->sin_addr.s_addr;
	*port = slurm_address->sin_port;
	snprintf(ip, buf_len, "%u.%u.%u.%u", uc[0], uc[1], uc[2], uc[3]);
}

/* slurm_get_peer_addr
 * get the slurm address of the peer connection, similar to getpeeraddr
 * IN fd		- an open connection
 * OUT slurm_address	- place to park the peer's slurm_addr
 */
int slurm_get_peer_addr(slurm_fd fd, slurm_addr * slurm_address)
{
	struct sockaddr name;
	socklen_t namelen = (socklen_t) sizeof(struct sockaddr);
	int rc;

	if ((rc = _slurm_getpeername((int) fd, &name, &namelen)))
		return rc;
	memcpy(slurm_address, &name, sizeof(slurm_addr));
	return 0;
}

/* slurm_print_slurm_addr
 * prints a slurm_addr into a buf
 * IN address		- slurm_addr to print
 * IN buf		- space for string representation of slurm_addr
 * IN n			- max number of bytes to write (including NUL)
 */
void slurm_print_slurm_addr(slurm_addr * address, char *buf, size_t n)
{
	_slurm_print_slurm_addr(address, buf, n);
}

/**********************************************************************\
 * slurm_addr pack routines
\**********************************************************************/

/* slurm_pack_slurm_addr
 * packs a slurm_addr into a buffer to serialization transport
 * IN slurm_address	- slurm_addr to pack
 * IN/OUT buffer	- buffer to pack the slurm_addr into
 */
void slurm_pack_slurm_addr(slurm_addr * slurm_address, Buf buffer)
{
	_slurm_pack_slurm_addr(slurm_address, buffer);
}

/* slurm_pack_slurm_addr
 * unpacks a buffer into a slurm_addr after serialization transport
 * OUT slurm_address	- slurm_addr to unpack to
 * IN/OUT buffer	- buffer to upack the slurm_addr from
 * returns		- SLURM error code
 */
int slurm_unpack_slurm_addr_no_alloc(slurm_addr * slurm_address,
				     Buf buffer)
{
	return _slurm_unpack_slurm_addr_no_alloc(slurm_address, buffer);
}

/**********************************************************************\
 * simplified communication routines 
 * They open a connection do work then close the connection all within 
 * the function
\**********************************************************************/

/* slurm_send_rc_msg
 * given the original request message this function sends a 
 *	slurm_return_code message back to the client that made the request
 * IN request_msg	- slurm_msg the request msg
 * IN rc		- the return_code to send back to the client
 */
int slurm_send_rc_msg(slurm_msg_t *msg, int rc)
{
	slurm_msg_t resp_msg;
	return_code_msg_t rc_msg;
	
	if (msg->conn_fd < 0)
		return (ENOTCONN);
	rc_msg.return_code = rc;

	resp_msg.address  = msg->address;
	resp_msg.msg_type = RESPONSE_SLURM_RC;
	resp_msg.data     = &rc_msg;
	resp_msg.forward_cnt = msg->forward_cnt;
	resp_msg.forward_addr = msg->forward_addr;
	resp_msg.ret_list = msg->ret_list;
	/* send message */
	return slurm_send_node_msg(msg->conn_fd, &resp_msg);
}

/*
 * Send and recv a slurm request and response on the open slurm descriptor
 */
static List 
_send_and_recv_msg(slurm_fd fd, slurm_msg_t *req, 
		   slurm_msg_t *resp, int timeout)
{
	int err = SLURM_SUCCESS;
	int retry = 0;
	List ret_list = NULL;
	ListIterator itr;
	ret_types_t *ret_type = NULL;
	
	if ((slurm_send_node_msg(fd, req) < 0)) 
		err = errno;
	if(err == SLURM_SUCCESS) {
		ret_list = slurm_receive_msg(fd, resp, timeout);
	}
	info("Hey I got this count here %d",list_count(ret_list));
	/* 
	 *  Attempt to close an open connection
	 */
	while ((slurm_shutdown_msg_conn(fd) < 0) && (errno == EINTR) ) {
		if (retry++ > MAX_SHUTDOWN_RETRY) {
			err = errno;
			break;
		}
	}
	info("returned %d state %d",
	     resp->msg_type, 
	     ((return_code_msg_t *) resp->data)->return_code);
	

 	if (err) 
		errno = err; 

	return ret_list;
}

/*
 * slurm_send_recv_controller_msg
 * opens a connection to the controller, sends the controller a message, 
 * listens for the response, then closes the connection
 * IN request_msg	- slurm_msg request
 * OUT response_msg     - slurm_msg response
 * RET int              - return code
 */
int slurm_send_recv_controller_msg(slurm_msg_t *req, slurm_msg_t *resp)
{
	slurm_fd fd = -1;
	int rc = SLURM_SUCCESS;
	time_t start_time = time(NULL);
	List ret_list = NULL;
	int retry = 1;

	if ((fd = slurm_open_controller_conn()) < 0) {
		rc = SLURM_SOCKET_ERROR;
		goto cleanup;
	}
	req->forward_cnt = 0;
	req->ret_list = NULL;
	req->forward_addr = 0;
	resp->forward_cnt = 0;
	resp->forward_addr = 0;
	info("here 2");
	
	while(retry) {
		retry = 0;
		/* If the backup controller is in the process of assuming 
		 * control, we sleep and retry later */
		ret_list = _send_and_recv_msg(fd, req, resp, 0);
		rc = errno;
		if(list_count(ret_list)>0) {
			error("We didn't do things correctly "
			      "missed %d responses",
			      list_count(ret_list));
			list_destroy(ret_list);
		}
		info("here message type = %d",resp->msg_type);
		if((rc == SLURM_SUCCESS) &&
		   (resp->msg_type == RESPONSE_SLURM_RC) &&
		   ((((return_code_msg_t *) resp->data)->return_code) 
		    == ESLURM_IN_STANDBY_MODE) &&
		   (req->msg_type 
		    != MESSAGE_NODE_REGISTRATION_STATUS) && 
		   (slurmctld_conf.backup_controller) &&
		   (difftime(time(NULL), start_time) < 
		    (slurmctld_conf.slurmctld_timeout +
		     slurmctld_conf.heartbeat_interval))) {
			debug("Neither primary nor backup controller responding, "
			      "sleep and retry");
			slurm_free_return_code_msg(resp->data);
			sleep(30);
			if ((fd = slurm_open_controller_conn()) < 0) 
				rc = SLURM_SOCKET_ERROR;
			else 
				retry = 1;
		}
		if(rc != SLURM_SUCCESS)
			break;
	}
			
      cleanup:
	if (rc != SLURM_SUCCESS) 
 		_remap_slurmctld_errno(); 
	return rc;
}

/* slurm_send_recv_node_msg
 * opens a connection to node, sends the node a message, listens 
 * for the response, then closes the connection
 * IN request_msg	- slurm_msg request
 * OUT response_msg	- slurm_msg response
 * RET List		- return list from multiple nodes
 */
List slurm_send_recv_node_msg(slurm_msg_t *req, slurm_msg_t *resp, int timeout)
{
	slurm_fd fd = -1;

	if ((fd = slurm_open_msg_conn(&req->address)) < 0)
		return NULL; //SLURM_SOCKET_ERROR;
	info("here 3");
	
	return _send_and_recv_msg(fd, req, resp, timeout);

}

/* slurm_send_only_controller_msg
 * opens a connection to the controller, sends the controller a 
 * message then, closes the connection
 * IN request_msg	- slurm_msg request
 * RET int		- return code
 */
int slurm_send_only_controller_msg(slurm_msg_t *req)
{
	int      rc = SLURM_SUCCESS;
	int      retry = 0;
	slurm_fd fd = -1;

	/*
	 *  Open connection to SLURM controller:
	 */
	if ((fd = slurm_open_controller_conn()) < 0) {
		rc = SLURM_SOCKET_ERROR;
		goto cleanup;
	}

	rc = slurm_send_node_msg(fd, req);

	/* 
	 *  Attempt to close an open connection
	 */
	while ( (slurm_shutdown_msg_conn(fd) < 0) && (errno == EINTR) ) {
		if (retry++ > MAX_SHUTDOWN_RETRY) {
			rc = SLURM_SOCKET_ERROR;
			goto cleanup;
		}
	}

      cleanup:
	if (rc != SLURM_SUCCESS)
		_remap_slurmctld_errno();
	return rc;
}

/* 
 *  Open a connection to the "address" specified in the slurm msg `req'
 *   Then, immediately close the connection w/out waiting for a reply.
 *
 *   Returns SLURM_SUCCESS on success SLURM_FAILURE (< 0) for failure.
 */
int slurm_send_only_node_msg(slurm_msg_t *req)
{
	int      rc = SLURM_SUCCESS;
	int      retry = 0;
	slurm_fd fd = -1;
	req->forward_cnt = 0;
	req->ret_list = NULL;
	req->forward_addr = NULL;
	
	if ((fd = slurm_open_msg_conn(&req->address)) < 0)
		return SLURM_SOCKET_ERROR;

	rc = slurm_send_node_msg(fd, req);

	/* 
	 *  Attempt to close an open connection
	 */
	while ( (slurm_shutdown_msg_conn(fd) < 0) && (errno == EINTR) ) {
		if (retry++ > MAX_SHUTDOWN_RETRY)
			return SLURM_SOCKET_ERROR;
	}

	return rc;
}


/*
 *  Send message and recv "return code" message on an already open
 *    slurm file descriptor
 */
static List _send_recv_rc_msg(slurm_fd fd, slurm_msg_t *req, int timeout)
{
	int		retval = SLURM_SUCCESS;
	slurm_msg_t	msg;
	List ret_list = NULL;
	ListIterator itr = NULL;
	ret_types_t *ret_type = NULL;
	char *name = xstrdup("localhost");
	int msg_rc;
	int set = 0;
	int err;
	info("here 1");
	
	ret_list = _send_and_recv_msg(fd, req, &msg, timeout);
	if(!ret_list) {
		return ret_list;
	}
	info("received %d types",list_count(ret_list));
	err = errno;
	msg_rc = ((return_code_msg_t *)msg.data)->return_code;
	
	info("got errno of %d",errno);
	itr = list_iterator_create(ret_list);		
	while((ret_type = list_next(itr)) != NULL) {
		if(ret_type->msg_rc == msg_rc) {
			list_push(ret_type->names, name);
			set = 1;
		}
		if (ret_type->err != SLURM_SUCCESS) 
			continue;

		if (ret_type->type != RESPONSE_SLURM_RC) 
			ret_type->err = SLURM_UNEXPECTED_MSG_ERROR;
	}
	list_iterator_destroy(itr);
	if(!set) {
		ret_type = xmalloc(sizeof(ret_types_t));
		list_push(ret_list, ret_type);
		ret_type->type = msg.msg_type;
		ret_type->msg_rc = msg_rc;
		ret_type->err = err;
		ret_type->names = list_create(destroy_names);
		list_push(ret_type->names, name);
	}
	
	return ret_list;
}

/*
 *  Open a connection to the "address" specified in the the slurm msg "req"
 *    Then read back an "rc" message returning the "return_code" specified
 *    in the response in the "rc" parameter.
 */
List slurm_send_recv_rc_msg(slurm_msg_t *req, int timeout)
{
	slurm_fd fd = -1;
	
	if ((fd = slurm_open_msg_conn(&req->address)) < 0) {
		return NULL; //SLURM_SOCKET_ERROR
	}
	return _send_recv_rc_msg(fd, req, timeout);
}
/*
 *  Open a connection to the "address" specified in the the slurm msg "req"
 *    Then read back an "rc" message returning the "return_code" specified
 *    in the response in the "rc" parameter.
 */
int slurm_send_recv_rc_msg_only_one(slurm_msg_t *req, int *rc, int timeout)
{
	slurm_fd fd = -1;
	List ret_list = NULL;
	ret_types_t *ret_type = NULL;
	int ret_c = SLURM_SUCCESS;

	if ((fd = slurm_open_msg_conn(&req->address)) < 0) {
		return SLURM_SOCKET_ERROR;
	}
	
	ret_list = _send_recv_rc_msg(fd, req, timeout);
	if(ret_list) {
		if(list_count(ret_list)>1) 
			error("Got %d, expecting 1 from message recieving",
			      list_count(ret_list));

		ret_type = list_pop(ret_list);
	
		if(ret_type) {
			*rc = ret_type->msg_rc;
			ret_c = ret_type->err;
			destroy_ret_types(ret_type);
		}
		list_destroy(ret_list);
	} else 
		ret_c = SLURM_ERROR;
	return ret_c;
}

/*
 *  Same as above, but send message to controller
 */
int slurm_send_recv_controller_rc_msg(slurm_msg_t *req, int *rc)
{
	slurm_fd fd = -1;
	List ret_list = NULL;
	ret_types_t *ret_type = NULL;
	int ret_val = SLURM_ERROR;

	req->forward_cnt = 0;
	req->ret_list = NULL;
	req->forward_addr = NULL;
	if ((fd = slurm_open_controller_conn()) < 0)
		return SLURM_SOCKET_ERROR;
	ret_list = _send_recv_rc_msg(fd, req, 0);
	
	if(ret_list) {
		if(list_count(ret_list)>1)
			error("controller_rc_msg: Got %d instead of 1 back",
			      list_count(ret_list));
		ret_type = list_pop(ret_list);
		
		if(ret_type) {
			*rc = ret_type->msg_rc;
			ret_val = ret_type->err;
		}
		list_destroy(ret_list);
	} 
	return ret_val;
}

/*
 * Free a slurm message
 */
void slurm_free_msg(slurm_msg_t * msg)
{
	(void) g_slurm_auth_destroy(msg->cred);
	if(msg->ret_list)
		list_destroy(msg->ret_list);
	xfree(msg);
}

/* 
 * Free just the credential of a message
 */
void slurm_free_cred(void *cred)
{
	(void) g_slurm_auth_destroy(cred);
}

void destroy_names(void *object)
{
	char *name = (char *)object;
	xfree(name);	       
}
/* 
 * Free just the list from forwarded messages
 */
void destroy_ret_types(void *object)
{
	ret_types_t *ret_type = (ret_types_t *)object;
	if(ret_type) {
		if(ret_type->names)
			list_destroy(ret_type->names);
		xfree(ret_type);
	}
}

#if _DEBUG

static void _print_data(char *data, int len)
{
	int i;
	for (i = 0; i < len; i++) {
		if ((i % 10 == 0) && (i != 0))
			printf("\n");
		printf("%2.2x ", ((int) data[i] & 0xff));
		if (i >= 200)
			break;
	}
	printf("\n\n");
}

#endif


/*
 * vi: shiftwidth=8 tabstop=8 expandtab
 */
