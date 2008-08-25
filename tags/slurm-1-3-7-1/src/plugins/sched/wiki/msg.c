/*****************************************************************************\
 *  msg.c - Message/communcation manager for Wiki plugin
 *****************************************************************************
 *  Copyright (C) 2006 The Regents of the University of California.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Morris Jette <jette1@llnl.gov>
 *  LLNL-CODE-402394.
 *  
 *  This file is part of SLURM, a resource management program.
 *  For details, see <http://www.llnl.gov/linux/slurm/>.
 *  
 *  SLURM is free software; you can redistribute it and/or modify it under
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
 *  SLURM is distributed in the hope that it will be useful, but WITHOUT ANY
 *  WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 *  FOR A PARTICULAR PURPOSE.  See the GNU General Public License for more
 *  details.
 *  
 *  You should have received a copy of the GNU General Public License along
 *  with SLURM; if not, write to the Free Software Foundation, Inc.,
 *  51 Franklin Street, Fifth Floor, Boston, MA 02110-1301  USA.
\*****************************************************************************/

#include "./crypto.h"
#include "./msg.h"
#include "src/common/uid.h"
#include "src/slurmctld/locks.h"

#define _DEBUG 0

static bool thread_running = false;
static bool thread_shutdown = false;
static pthread_mutex_t thread_flag_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_t msg_thread_id;
static char *err_msg;
static int   err_code;
static uint16_t sched_port;

/* Global configuration parameters */
char     auth_key[KEY_SIZE] = "";
char     e_host[E_HOST_SIZE] = "";
char     e_host_bu[E_HOST_SIZE] = "";
uint16_t e_port = 0;
struct   part_record *exclude_part_ptr[EXC_PART_CNT];
struct   part_record *hide_part_ptr[HIDE_PART_CNT];
uint16_t job_aggregation_time = 10;	/* Default value is 10 seconds */
int      init_prio_mode = PRIO_HOLD;
uint16_t kill_wait;
uint16_t use_host_exp = 0;

static char *	_get_wiki_conf_path(void);
static void *	_msg_thread(void *no_data);
static int	_parse_msg(char *msg, char **req);
static void	_proc_msg(slurm_fd new_fd, char *msg);
static size_t	_read_bytes(int fd, char *buf, const size_t size);
static char *	_recv_msg(slurm_fd new_fd);
static size_t	_send_msg(slurm_fd new_fd, char *buf, size_t size);
static void	_send_reply(slurm_fd new_fd, char *response);
static size_t	_write_bytes(int fd, char *buf, const size_t size);

/*****************************************************************************\
 * spawn message hander thread
\*****************************************************************************/
extern int spawn_msg_thread(void)
{
	pthread_attr_t thread_attr_msg;

	pthread_mutex_lock( &thread_flag_mutex );
	if (thread_running) {
		error("Wiki thread already running, not starting another");
		pthread_mutex_unlock(&thread_flag_mutex);
		return SLURM_ERROR;
	}

	parse_wiki_config();
	slurm_attr_init(&thread_attr_msg);
	if (pthread_create(&msg_thread_id, &thread_attr_msg, 
			_msg_thread, NULL))
		fatal("pthread_create %m");

	slurm_attr_destroy(&thread_attr_msg);
	thread_running = true;
	pthread_mutex_unlock(&thread_flag_mutex);
	return SLURM_SUCCESS;
}

/*****************************************************************************\
 * terminate message hander thread
\*****************************************************************************/
extern void term_msg_thread(void)
{
	pthread_mutex_lock(&thread_flag_mutex);
	if (thread_running) {
		int fd;
                slurm_addr addr;

		thread_shutdown = true;

                /* Open and close a connection to the wiki listening port.
                 * Allows slurm_accept_msg_conn() to return in 
                 * _msg_thread() so that it can check the thread_shutdown
                 * flag.
                 */
                slurm_set_addr(&addr, sched_port, "localhost");
                fd = slurm_open_stream(&addr);
                if (fd != -1) {
                        /* we don't care if the open failed */
                        slurm_close_stream(fd);
                }

                debug2("waiting for sched/wiki thread to exit");
                pthread_join(msg_thread_id, NULL);
                msg_thread_id = 0;
                thread_shutdown = false;
                thread_running = false;
                debug2("join of sched/wiki thread was successful");
	}
	pthread_mutex_unlock(&thread_flag_mutex);
}

/*****************************************************************************\
 * message hander thread
\*****************************************************************************/
static void *_msg_thread(void *no_data)
{
	slurm_fd sock_fd = -1, new_fd;
	slurm_addr cli_addr;
	char *msg;
	slurm_ctl_conf_t *conf;
	int i;
	/* Locks: Write configuration, job, node, and partition */
	slurmctld_lock_t config_write_lock = {
		WRITE_LOCK, WRITE_LOCK, WRITE_LOCK, WRITE_LOCK };

	conf = slurm_conf_lock();
	sched_port = conf->schedport;
	slurm_conf_unlock();

	/* Wait until configuration is completely loaded */
	lock_slurmctld(config_write_lock);
	unlock_slurmctld(config_write_lock);

	/* If SchedulerPort is already taken, keep trying to open it
	 * once per minute. Slurmctld will continue to function
	 * during this interval even if nothing can be scheduled. */
	for (i=0; (!thread_shutdown); i++) {
		if (i > 0)
			sleep(60);
		sock_fd = slurm_init_msg_engine_port(sched_port);
		if (sock_fd != SLURM_SOCKET_ERROR)
			break;
		error("wiki: slurm_init_msg_engine_port %u %m",
			sched_port);
		error("wiki: Unable to communicate with Moab");
	}

	/* Process incoming RPCs until told to shutdown */
	while (!thread_shutdown) {
		if ((new_fd = slurm_accept_msg_conn(sock_fd, &cli_addr))
				== SLURM_SOCKET_ERROR) {
			if (errno != EINTR)
				error("wiki: slurm_accept_msg_conn %m");
			continue;
		}
                if (thread_shutdown) {
                        close(new_fd);
                        break;
                }
		/* It would be nice to create a pthread for each new 
		 * RPC, but that leaks memory on some systems when 
		 * done from a plugin.
		 * FIXME: Maintain a pool of and reuse them. */
		err_code = 0;
		err_msg = "";
		msg = _recv_msg(new_fd);
		if (msg) {
			_proc_msg(new_fd, msg);
			xfree(msg);
		}
		slurm_close_accepted_conn(new_fd);
	}
	if (sock_fd > 0)
		(void) slurm_shutdown_msg_engine(sock_fd);
	pthread_exit((void *) 0);
	return NULL;
}

/*****************************************************************************\
 * _get_wiki_conf_path - return the pathname of the wiki.conf file
 * return value must be xfreed
\*****************************************************************************/
static char * _get_wiki_conf_path(void)
{
	char *val = getenv("SLURM_CONF");
	char *path = NULL;
	int i;

	if (!val)
		val = default_slurm_config_file;

	/* Replace file name on end of path */
	i = strlen(val) + 10;
	path = xmalloc(i);
	strcpy(path, val);
	val = strrchr(path, (int)'/');
	if (val)	/* absolute path */
		val++;
	else		/* not absolute path */
		val = path;
	strcpy(val, "wiki.conf");

	return path;
}

/*****************************************************************************\
 * parse_wiki_config - Results go into global variables
 * RET SLURM_SUCESS or error code
 * 
 * wiki_conf options
 * JobPriority=hold|run
 * AuthKey=number
\*****************************************************************************/
extern int parse_wiki_config(void)
{
	s_p_options_t options[] = {
		{"AuthKey", S_P_STRING},
		{"EHost", S_P_STRING},
		{"EHostBackup", S_P_STRING},
		{"EPort", S_P_UINT16},
		{"ExcludePartitions", S_P_STRING},
		{"HidePartitionJobs", S_P_STRING},
		{"JobAggregationTime", S_P_UINT16},
		{"JobPriority", S_P_STRING}, 
		{NULL} };
	s_p_hashtbl_t *tbl;
	char *exclude_partitions, *hide_partitions;
	char *key = NULL, *priority_mode = NULL, *wiki_conf;
	struct stat buf;
	slurm_ctl_conf_t *conf;
	int i;

	/* Set default values */
	for (i=0; i<EXC_PART_CNT; i++)
		exclude_part_ptr[i] = NULL;
	for (i=0; i<HIDE_PART_CNT; i++)
		hide_part_ptr[i] = NULL;
	conf = slurm_conf_lock();
	strncpy(e_host, conf->control_addr, sizeof(e_host));
	if (conf->backup_addr) {
		strncpy(e_host_bu, conf->backup_addr,
			sizeof(e_host));
	} 
	kill_wait = conf->kill_wait;
	slurm_conf_unlock();

	wiki_conf = _get_wiki_conf_path();
	if ((wiki_conf == NULL) || (stat(wiki_conf, &buf) == -1)) {
		debug("No wiki.conf file (%s)", wiki_conf);
		xfree(wiki_conf);
		return SLURM_SUCCESS;
	}

	debug("Reading wiki.conf file (%s)",wiki_conf);
	tbl = s_p_hashtbl_create(options);
	if (s_p_parse_file(tbl, wiki_conf) == SLURM_ERROR)
		fatal("something wrong with opening/reading wiki.conf file");

	if (! s_p_get_string(&key, "AuthKey", tbl))
		debug("Warning: No wiki_conf AuthKey specified");
	else {
		strncpy(auth_key, key, sizeof(auth_key));
		xfree(key);
	}
	if ( s_p_get_string(&key, "EHost", tbl)) {
		strncpy(e_host, key, sizeof(e_host));
		xfree(key);
	} else
		debug("wiki: Using ControlAddr for EHost value");
	if ( s_p_get_string(&key, "EHostBackup", tbl)) {
		strncpy(e_host_bu, key, sizeof(e_host_bu));
		xfree(key);
	}
	s_p_get_uint16(&e_port, "EPort", tbl);
	s_p_get_uint16(&job_aggregation_time, "JobAggregationTime", tbl); 

	if (s_p_get_string(&exclude_partitions, "ExcludePartitions", tbl)) {
		char *tok = NULL, *tok_p = NULL;
		tok = strtok_r(exclude_partitions, ",", &tok_p);
		i = 0;
		while (tok) {
			if (i >= EXC_PART_CNT) {
				error("ExcludePartitions has too many entries "
				      "skipping %s and later entries");
				break;
			}	
			exclude_part_ptr[i] = find_part_record(tok);
			if (exclude_part_ptr[i])
				i++;
			else
				error("ExcludePartitions %s not found", tok);
			tok = strtok_r(NULL, ",", &tok_p);
		}
	}

	if (s_p_get_string(&hide_partitions, "HidePartitionJobs", tbl)) {
		char *tok = NULL, *tok_p = NULL;
		tok = strtok_r(hide_partitions, ",", &tok_p);
		i = 0;
		while (tok) {
			if (i >= HIDE_PART_CNT) {
				error("HidePartitionJobs has too many entries "
				      "skipping %s and later entries");
				break;
			}	
			hide_part_ptr[i] = find_part_record(tok);
			if (hide_part_ptr[i])
				i++;
			else
				error("HidePartitionJobs %s not found", tok);
			tok = strtok_r(NULL, ",", &tok_p);
		}
	}

	if (s_p_get_string(&priority_mode, "JobPriority", tbl)) {
		if (strcasecmp(priority_mode, "hold") == 0)
			init_prio_mode = PRIO_HOLD;
		else if (strcasecmp(priority_mode, "run") == 0)
			init_prio_mode = PRIO_DECREMENT;
		else
			error("Invalid value for JobPriority in wiki.conf");	
		xfree(priority_mode);
	}
	s_p_hashtbl_destroy(tbl);
	xfree(wiki_conf);

#if _DEBUG
	info("AuthKey            = %s", auth_key);
	info("EHost              = %s", e_host);
	info("EHostBackup        = %s", e_host_bu);
	info("EPort              = %u", e_port);
	info("JobAggregationTime = %u sec", job_aggregation_time);
	info("JobPriority        = %s", init_prio_mode ? "run" : "hold");
	info("KillWait           = %u sec", kill_wait);      
	for (i=0; i<EXC_PART_CNT; i++) {
		if (!exclude_part_ptr[i])
			continue;
		info("ExcludePartitions  = %s", exclude_part_ptr[i]->name);
	}
	for (i=0; i<HIDE_PART_CNT; i++) {
		if (!hide_part_ptr[i])
			continue;
		info("HidePartitionJobs  = %s", hide_ptr_ptr[i]->name);
	}
#endif
	return SLURM_SUCCESS;
}

extern char *	get_wiki_conf(void)
{
	int i, first;
	char buf[20], *conf = NULL;

	snprintf(buf, sizeof(buf), "HostFormat=%u", use_host_exp);
	xstrcat(conf, buf);

	first = 1;
	for (i=0; i<EXC_PART_CNT; i++) {
		if (!exclude_part_ptr[i])
			continue;
		if (first) {
			xstrcat(conf, ";ExcludePartitions=");
			first = 0;
		} else
			xstrcat(conf, ",");
		xstrcat(conf, exclude_part_ptr[i]->name);
	}

	first = 1;
	for (i=0; i<HIDE_PART_CNT; i++) {
		if (!hide_part_ptr[i])
			continue;
		if (first) {
			xstrcat(conf, ";HidePartitionJobs=");
			first = 0;
		} else
			xstrcat(conf, ",");
		xstrcat(conf, hide_part_ptr[i]->name);
	}

	return conf;
}

static size_t	_read_bytes(int fd, char *buf, const size_t size)
{
	size_t bytes_remaining, bytes_read;
	char *ptr;

	bytes_remaining = size;
	ptr = buf;
	while (bytes_remaining > 0) {
		bytes_read = read(fd, ptr, bytes_remaining);
		if (bytes_read <= 0)
			return 0;
		bytes_remaining -= bytes_read;
		ptr += bytes_read;
	}
	
	return size;
}

static size_t	_write_bytes(int fd, char *buf, const size_t size)
{
	size_t bytes_remaining, bytes_written;
	char *ptr;

	bytes_remaining = size;
	ptr = buf;
	while (bytes_remaining > 0) {
		bytes_written = write(fd, ptr, bytes_remaining);
		if (bytes_written <= 0)
			return (size - bytes_remaining);
		bytes_remaining -= bytes_written;
		ptr += bytes_written;
	}
	return size;
}

/*****************************************************************************\
 * Read a message (request) from specified file descriptor 
 *
 * RET - The message which must be xfreed or 
 *       NULL on error
\*****************************************************************************/
static char *	_recv_msg(slurm_fd new_fd)
{
	char header[10];
	unsigned long size;
	char *buf;

	if (_read_bytes((int) new_fd, header, 9) != 9) {
		err_code = -240;
		err_msg = "failed to read message header";
		error("wiki: failed to read message header %m");
		return NULL;
	}

	if (sscanf(header, "%lu", &size) != 1) {
		err_code = -244;
		err_msg = "malformed message header";
		error("wiki: malformed message header (%s)", header);
		return NULL;
	}

	buf = xmalloc(size + 1);	/* need '\0' on end to print */
	if (_read_bytes((int) new_fd, buf, size) != size) {
		err_code = -246;
		err_msg = "unable to read all message data";
		error("wiki: unable to read data message");
		xfree(buf);
		return NULL;
	}

	debug("wiki msg recv:%s", buf);
	return buf;
}

/*****************************************************************************\
 * Send a message (response) to specified file descriptor 
 *
 * RET - Number of data bytes written (excludes header)
\*****************************************************************************/
static size_t	_send_msg(slurm_fd new_fd, char *buf, size_t size)
{
	char header[10];
	size_t data_sent;

	debug("wiki msg send:%s", buf);

	(void) sprintf(header, "%08lu\n", (unsigned long) size);
	if (_write_bytes((int) new_fd, header, 9) != 9) {
		error("wiki: failed to write message header %m");
		return 0;
	}

	data_sent = _write_bytes((int) new_fd, buf, size);
	if (data_sent != size) {
		error("wiki: unable to write data message (%lu of %lu) %m",
			data_sent, size);
	}

	return data_sent;
}

/*****************************************************************************\
 * Parse and checksum a wiki request
 * msg IN - message to parse
 * req OUT - pointer the request portion of the message
 * RET 0 on success, -1 on error
\*****************************************************************************/
static int	_parse_msg(char *msg, char **req)
{
	char *auth_ptr = strstr(msg, "AUTH=");
	char *dt_ptr = strstr(msg, "DT=");
	char *ts_ptr = strstr(msg, "TS=");
	char *cmd_ptr = strstr(msg, "CMD=");
	time_t ts, now = time(NULL);
	uint32_t delta_t;
	
	if ((auth_key[0] == '\0') && cmd_ptr) {
		/* No authentication required */
		*req = cmd_ptr;
		return 0;
	}

	if (!auth_ptr) {
		err_code = -300;
		err_msg = "request lacks AUTH";
		error("wiki: request lacks AUTH=");
		return -1;
	}

	if (!dt_ptr) {
		err_code = -300;
		err_msg = "request lacks DT";
		error("wiki: request lacks DT=");
		return -1;
	}

	if (!ts_ptr) {
		err_code = -300;
		err_msg = "request lacks TS";
		error("wiki: request lacks TS=");
		return -1;
	}
	ts = strtoul((ts_ptr+3), NULL, 10); 
	if (ts < now)
		delta_t = (uint32_t) difftime(now, ts);
	else
		delta_t = (uint32_t) difftime(ts, now);
	if (delta_t > 300) {
		err_code = -350;
		err_msg = "TS value too far from NOW";
		error("wiki: TimeStamp too far from NOW (%u secs)", 
			delta_t);
		return -1;
	}

#if 0
	/* Old wiki interface does not require checksum
	 * (actually a cryptographic signature) on incomming 
	 * messages.  NOTE: This is not secure! */
	if (auth_key[0] != '\0') {
		char sum[20];	/* format is "CK=%08x08x" */
		checksum(sum, auth_key, ts_ptr);
		if (strncmp(sum, msg, 19) != 0) {
			err_code = -422;
			err_msg = "bad checksum";
			error("wiki: message checksum error");
			return -1;
		}
	}
#endif

	*req = dt_ptr + 3;
	return 0;
}

/*****************************************************************************\
 * Parse, process and respond to a request
\*****************************************************************************/
static void	_proc_msg(slurm_fd new_fd, char *msg)
{
	DEF_TIMERS;
	char *req, *cmd_ptr, *msg_type = NULL;
	char response[128];

	if (new_fd < 0)
		return;

	START_TIMER;
	if (!msg) {
		err_code = -300;
		err_msg = "NULL request message";
		error("wiki: NULL request message");
		goto resp_msg;
	}

	if (_parse_msg(msg, &req) != 0)
		goto resp_msg;

	cmd_ptr = strstr(req, "CMD=");
	if (cmd_ptr == NULL) {
		err_code = -300;
		err_msg = "request lacks CMD"; 
		error("wiki: request lacks CMD");
		goto resp_msg;
	}
	cmd_ptr +=4;
	err_code = 0;
	if        (strncmp(cmd_ptr, "GETJOBS", 7) == 0) {
		msg_type = "wiki:GETJOBS";
		if (!get_jobs(cmd_ptr, &err_code, &err_msg))
			goto free_resp_msg;
	} else if (strncmp(cmd_ptr, "GETNODES", 8) == 0) {
		msg_type = "wiki:GETNODES";
		if (!get_nodes(cmd_ptr, &err_code, &err_msg))
			goto free_resp_msg;
	} else if (strncmp(cmd_ptr, "STARTJOB", 8) == 0) {
		msg_type = "wiki:STARTJOB";
		start_job(cmd_ptr, &err_code, &err_msg);
	} else if (strncmp(cmd_ptr, "CANCELJOB", 9) == 0) {
		msg_type = "wiki:CANCELJOB";
		cancel_job(cmd_ptr, &err_code, &err_msg);
	} else if (strncmp(cmd_ptr, "SUSPENDJOB", 10) == 0) {
		msg_type = "wiki:SUSPENDJOB";
		suspend_job(cmd_ptr, &err_code, &err_msg);
	} else if (strncmp(cmd_ptr, "RESUMEJOB", 9) == 0) {
		msg_type = "wiki:RESUMEJOB";
		resume_job(cmd_ptr, &err_code, &err_msg);
	} else if (strncmp(cmd_ptr, "MODIFYJOB", 9) == 0) {
		msg_type = "wiki:MODIFYJOB";
		job_modify_wiki(cmd_ptr, &err_code, &err_msg);
	} else {
		err_code = -300;
		err_msg = "unsupported request type";
		error("wiki: unrecognized request type: %s", req);
	}
	END_TIMER2(msg_type);

 resp_msg:
	snprintf(response, sizeof(response),
		"SC=%d RESPONSE=%s", err_code, err_msg);
	_send_reply(new_fd, response);
	return;

 free_resp_msg:
	/* Message is pre-formatted by get_jobs and get_nodes
	 * ONLY if no error. Send message and xfree the buffer. */
	_send_reply(new_fd, err_msg);
	xfree(err_msg);
	return;
}

static void	_send_reply(slurm_fd new_fd, char *response)
{
	size_t i;
	char *buf, sum[20], *tmp;
	static char uname[64] = "";

	i = strlen(response);
	i += 100;	/* leave room for header */
	buf = xmalloc(i);

	if (uname[0] == '\0') {
		tmp = uid_to_string(getuid());
		strncpy(uname, tmp, sizeof(uname));
		uname[sizeof(uname) - 1] = '\0';
		xfree(tmp);
	}

	snprintf(buf, i, "CK=dummy67890123456 TS=%u AUTH=%s DT=%s", 
		(uint32_t) time(NULL), uname, response);
	checksum(sum, auth_key, (buf+20));   /* overwrite "CK=dummy..." above */
	memcpy(buf, sum, 19);

	(void) _send_msg(new_fd, buf, i);
	xfree(buf);
}
