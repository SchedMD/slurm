/*****************************************************************************\
 *  jobcomp_kafka_message.c - Kafka message helper for jobcomp/kafka.
 *****************************************************************************
 *  Copyright (C) 2022 SchedMD LLC.
 *  Written by Alejandro Sanchez <alex@schedmd.com>
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

#include "src/common/data.h"
#include "src/common/list.h"
#include "src/common/log.h"
#include "src/common/pack.h"
#include "src/common/read_config.h"
#include "src/common/slurm_protocol_pack.h"
#include "src/common/timers.h"
#include "src/common/xassert.h"
#include "src/common/xmalloc.h"
#include "src/common/xstring.h"
#include "src/plugins/jobcomp/common/jobcomp_common.h"
#include "src/plugins/jobcomp/kafka/jobcomp_kafka_conf.h"
#include "src/plugins/jobcomp/kafka/jobcomp_kafka_message.h"

#define KAFKA_STATE_FILE "jobcomp_kafka_state"


static pthread_mutex_t poll_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t poll_stop_cond = PTHREAD_COND_INITIALIZER;
static pthread_t poll_thread;

static list_t *state_msg_list = NULL;
static bool terminate = false;
/*
 * The librdkafka API documents it is completely thread-safe.
 * Not adding mutex locks around rd_kafka_t *rk instance due to this.
 * _destroy_rd_kafka_handle() is called on plugin termination, so other
 * operations should not be called by then, and poll thread should
 * also be already joined.
 */
static rd_kafka_t *rk = NULL;

static void _add_kafka_msg_to_state(uint32_t job_id, char *payload);
static int _configure_rd_kafka_handle(void);
static int _create_rd_kafka_handle(rd_kafka_conf_t *conf);
static void _destroy_kafka_msg(void *arg);
static void _destroy_rd_kafka_conf(rd_kafka_conf_t **conf);
static void _destroy_rd_kafka_handle(void);
static void _dr_msg_cb(rd_kafka_t *rk, const rd_kafka_message_t *rkmessage,
		       void *opaque);
static void _dump_rd_kafka_conf(rd_kafka_conf_t *conf);
static void _flush_rd_kafka_msgs(void);
static int _foreach_conf_pair(void *x, void *arg);
static kafka_msg_t *_init_kafka_msg(uint32_t job_id, char *payload);
static void _load_jobcomp_kafka_state(void);
static void _pack_jobcomp_kafka_state(buf_t *buffer);
static int _pack_kafka_msg(void *object, void *arg);
static void *_poll_handler(void *no_data);
static void _purge_rd_kafka_msgs(void);
static void _terminate_poll_handler(void);
static void _save_jobcomp_kafka_state(void);
static rd_kafka_conf_t *_set_rd_kafka_conf(void);
static int _unpack_jobcomp_kafka_msg(uint16_t protocol_version, buf_t *buffer);
static void _unpack_jobcomp_kafka_state(buf_t *buffer);

/*
 * Allocate memory for a kafka_msg_t* and initialize it with arguments.
 * Append to state_msg_list.
 *
 * IN: uint32_t job_id
 * IN: char *payload
 */
static void _add_kafka_msg_to_state(uint32_t job_id, char *payload)
{
	kafka_msg_t *kafka_msg = NULL;

	kafka_msg = _init_kafka_msg(job_id, payload);
	list_append(state_msg_list, kafka_msg);
}

/*
 * Log rd_kafka_conf_t
 *
 * IN: rd_kafka_conf_t *conf
 */
static void _dump_rd_kafka_conf(rd_kafka_conf_t *conf)
{
	const char **array;
	size_t array_cnt;

	xassert(conf);

	/*
	 * Dump the configuration properties and values of conf to an array
	 * with "key", "value" pairs.
	 *
	 * The number of entries in the array is returned in *cntp.
	 *
	 * The dump must be freed with `rd_kafka_conf_dump_free()`.
	 */
	array = rd_kafka_conf_dump(conf, &array_cnt);

	for (int i = 0; ((i + 1) < array_cnt); i += 2)
		log_flag(JOBCOMP, "%s=%s", array[i], array[i + 1]);

	rd_kafka_conf_dump_free(array, array_cnt);
}

/*
 * 1. Set rd_kafka_conf_t options
 * 2. Create librdkafka handle with conf object
 *
 * RET: SLURM_SUCCESS or SLURM_ERROR
 */
static int _configure_rd_kafka_handle(void)
{
	int rc = SLURM_SUCCESS;
	rd_kafka_conf_t *conf = NULL;

	if (!(conf = _set_rd_kafka_conf()))
		return SLURM_ERROR;

	if (slurm_conf.debug_flags & DEBUG_FLAG_JOBCOMP)
		_dump_rd_kafka_conf(conf);

	rc = _create_rd_kafka_handle(conf);

	return rc;
}

/*
 * Message delivery report callback.
 *
 * This callback is called exactly once per message, indicating if the message
 * was succesfully delivered:
 * (rkmessage->err == RD_KAFKA_RESP_ERR_NO_ERROR)
 * or permanently failed delivery:
 * (rkmessage->err != RD_KAFKA_RESP_ERR_NO_ERROR).
 *
 * The callback is triggered from rd_kafka_poll() and executes on the
 * application's thread.
 */
static void _dr_msg_cb(rd_kafka_t *rk, const rd_kafka_message_t *rkmessage,
		       void *opaque)
{
	bool requeue;
	uint32_t job_id = *(uint32_t *) rkmessage->_private;
	char *topic = (char *) rd_kafka_topic_name(rkmessage->rkt);
	char *err_str = (char *) rd_kafka_err2str(rkmessage->err);
	char *payload = rkmessage->payload;
	char *action_str = NULL;

	switch (rkmessage->err) {
	case RD_KAFKA_RESP_ERR_NO_ERROR:
		/* Success */
		log_flag(JOBCOMP,
			 "Message for JobId=%u delivered to topic '%s'",
			 job_id, topic);
		break;
	case RD_KAFKA_RESP_ERR__MSG_TIMED_OUT:
		/*
		 * The message could not be successfully transmitted before
		 * message.timeout.ms expired.
		 */
		slurm_rwlock_rdlock(&kafka_conf_rwlock);
		requeue = (kafka_conf->flags &
			   KAFKA_CONF_FLAG_REQUEUE_ON_MSG_TIMEOUT);
		slurm_rwlock_unlock(&kafka_conf_rwlock);

		if (requeue) {
			if (!terminate) {
				jobcomp_kafka_message_produce(job_id, payload);
				xstrfmtcat(action_str,
					"Attempting to produce message again");
			} else {
				_add_kafka_msg_to_state(job_id,
							xstrdup(payload));
				xstrfmtcat(action_str,
					"Saving message to plugin state file.");
			}
		} else {
			xstrfmtcat(action_str, "Message discarded");
		}

		error("%s: Message delivery for JobId=%u failed: %s. %s.",
		      plugin_type, job_id, err_str, action_str);
		xfree(action_str);

		break;
#if RD_KAFKA_VERSION >= 0x010000ff
	case RD_KAFKA_RESP_ERR__PURGE_QUEUE:
		/* Purged in-queue. Always requeue in this case. */
		log_flag(JOBCOMP, "Message delivery for JobId=%u failed: %s. Saving message to plugin state file.",
			 job_id, err_str);
		_add_kafka_msg_to_state(job_id, xstrdup(payload));

		break;
	case RD_KAFKA_RESP_ERR__PURGE_INFLIGHT:
		/* Purged in-flight. */
		slurm_rwlock_rdlock(&kafka_conf_rwlock);
		requeue = (kafka_conf->flags &
			   KAFKA_CONF_FLAG_REQUEUE_PURGE_IN_FLIGHT);
		slurm_rwlock_unlock(&kafka_conf_rwlock);

		error("%s: Message delivery for JobId=%u failed: %s. %s.",
		      plugin_type, job_id, err_str,
		      requeue ?
		      "Saving message to plugin state file" : "Message discarded");

		if (requeue)
			_add_kafka_msg_to_state(job_id, xstrdup(payload));

		break;
#endif
	default:
		error("%s: Message delivery for JobId=%u failed: %s. Message discarded.",
		      plugin_type, job_id, err_str);
		break;
	}

	xfree(rkmessage->_private);
	/* The rkmessage is destroyed automatically by librdkafka */
}

/*
 * rd_kafka_conf_list listForF.
 *
 * IN: x: config_key_pair_t *conf_pair
 * IN: arg: rd_kafka_conf_t *conf
 * RET: -1 if failed to rd_kafka_conf_set() a conf pair, 1 otherwise.
 */
static int _foreach_conf_pair(void *x, void *arg)
{
	char errstr[512];
	config_key_pair_t *conf_pair = x;
	rd_kafka_conf_t *conf = arg;

	if (rd_kafka_conf_set(conf, conf_pair->name, conf_pair->value, errstr,
			      sizeof(errstr)) != RD_KAFKA_CONF_OK) {
		error("%s: rd_kafka_conf_set() failed to set '%s'->'%s': %s",
		      plugin_type, conf_pair->name, conf_pair->value, errstr);
		return -1;
	}

	return 1;
}

static void _destroy_rd_kafka_conf(rd_kafka_conf_t **conf)
{
	if (conf && *conf) {
		rd_kafka_conf_destroy(*conf);
		*conf = NULL;
	}
}

/*
 * 1. Create Kafka configuration handle.
 * 2. Set configuration properties
 *
 * RET: Kafka configuration handle or NULL on error.
 */
static rd_kafka_conf_t *_set_rd_kafka_conf(void)
{
	rd_kafka_conf_t *conf = NULL;

	conf = rd_kafka_conf_new();

	if (list_for_each(rd_kafka_conf_list, _foreach_conf_pair, conf) < 0)
		goto fail;

	/*
	 * The default is to print to stderr, but the rd_kafka_log_syslog()
	 * logger is also available as a builtin alternative.
	 */
	rd_kafka_conf_set_log_cb(conf, &rd_kafka_log_syslog);

	/*
	 * Set the delivery report callback in provided conf object.
	 *
	 * The delivery report callback will be called once for each message
	 * accepted by rd_kafka_produce() (et.al) with err set to indicate
	 * the result of the produce request.
	 *
	 * The callback is called when a message is succesfully produced or
	 * if librdkafka encountered a permanent failure.
	 *
	 * An application must call rd_kafka_poll() at regular intervals to
	 * serve queued delivery report callbacks.
	 */
	rd_kafka_conf_set_dr_msg_cb(conf, _dr_msg_cb);

	return conf;
fail:
	_destroy_rd_kafka_conf(&conf);
	return conf;
}

/*
 * Creates a new Kafka handle and starts its operation according to the
 * specified type (RD_KAFKA_CONSUMER or RD_KAFKA_PRODUCER).
 *
 * conf is an optional struct created with `rd_kafka_conf_new()` that will be
 * used instead of the default configuration.
 * The conf object is freed by this function on success and must not be used
 * or destroyed by the application subsequently.
 *
 * errstr must be a pointer to memory of at least size errstr_size where
 * `rd_kafka_new()` may write a human readable error message in case the
 * creation of a new handle fails. In which case the function returns NULL.
 */
static int _create_rd_kafka_handle(rd_kafka_conf_t *conf)
{
	int rc = SLURM_SUCCESS;
	char errstr[512];

	xassert(conf);

	if (!(rk = rd_kafka_new(RD_KAFKA_PRODUCER, conf, errstr,
				sizeof(errstr)))) {
		error("%s: Failed to create Kafka handle: %s",
		      plugin_type, errstr);
		_destroy_rd_kafka_conf(&conf);
		rc = SLURM_ERROR;
	}

	return rc;
}

static void _destroy_rd_kafka_handle(void)
{
	if (rk) {
		rd_kafka_destroy(rk);
		rk = NULL;
	}
}

/*
 * Wait until all outstanding produce requests, et.al, are completed.
 * This should typically be done prior to destroying a producer instance to make
 * sure all queued and in-flight produce requests are completed before
 * terminating.
 *
 * rd_kafka_flush() is an abstraction over rd_kafka_poll().
 *
 * The timeout specifies the maximum amount of time (in milliseconds) that the
 * call will block waiting for events.
 * For non-blocking calls, provide 0 as timeout.
 * To wait indefinitely for an event, provide -1.
 */
static void _flush_rd_kafka_msgs(void)
{
	int timeout;

	if (!rk)
		return;

	slurm_rwlock_rdlock(&kafka_conf_rwlock);
	timeout = kafka_conf->flush_timeout;
	slurm_rwlock_unlock(&kafka_conf_rwlock);
	log_flag(JOBCOMP, "Flushing with timeout of %d milliseconds", timeout);
	if ((rd_kafka_flush(rk, timeout) != RD_KAFKA_RESP_ERR_NO_ERROR) &&
	    (rd_kafka_outq_len(rk) > 0))
		error("%s: %d messages still in out queue after waiting for %d milliseconds",
		      plugin_type, rd_kafka_outq_len(rk), timeout);
}

static kafka_msg_t *_init_kafka_msg(uint32_t job_id, char *payload)
{
	kafka_msg_t *kafka_msg = NULL;

	kafka_msg = xmalloc(sizeof(*kafka_msg));
	kafka_msg->job_id = job_id;
	kafka_msg->payload = payload;

	return kafka_msg;
}

static void _destroy_kafka_msg(void *arg)
{
	kafka_msg_t *kafka_msg = arg;

	if (!kafka_msg)
		return;

	xfree(kafka_msg->payload);
	xfree(kafka_msg);
}

/* Kafka poll thread handler. */
static void *_poll_handler(void *no_data)
{
	struct timespec ts = {0, 0};

	while (!terminate) {
		if (rk)
			rd_kafka_poll(rk, 0);

		slurm_rwlock_rdlock(&kafka_conf_rwlock);
		ts.tv_sec = time(NULL) + kafka_conf->poll_interval;
		slurm_rwlock_unlock(&kafka_conf_rwlock);

		slurm_mutex_lock(&poll_mutex);
		slurm_cond_timedwait(&poll_stop_cond, &poll_mutex, &ts);
		slurm_mutex_unlock(&poll_mutex);
	}

	return NULL;
}

/*
 * Purge messages currently handled by the producer instance.
 *
 * purge_flags Tells which messages to purge and how.
 *
 * The application will need to call rd_kafka_poll() or rd_kafka_flush()
 * afterwards to serve the delivery report callbacks of the purged messages.
 *
 * Messages purged from internal queues fail with the delivery report
 * error code set to RD_KAFKA_RESP_ERR__PURGE_QUEUE, while purged messages that
 * are in-flight to or from the broker will fail with the error code set to
 * RD_KAFKA_RESP_ERR__PURGE_INFLIGHT.
 *
 * @warning Purging messages that are in-flight to or from the broker will
 * ignore any subsequent acknowledgement for these messages received from the
 * broker, effectively making it impossible for the application to know if the
 * messages were successfully produced or not. This may result in duplicate
 * messages if the application retries these messages at a later time.
 *
 * @remark This call may block for a short time while background thread queues
 * are purged.
 *
 * @returns RD_KAFKA_RESP_ERR_NO_ERROR on success,
 * RD_KAFKA_RESP_ERR__INVALID_ARG if the \p purge flags are invalid
 * or unknown,
 * RD_KAFKA_RESP_ERR__NOT_IMPLEMENTED if called on a non-producer
 * client instance.
 */
static void _purge_rd_kafka_msgs(void)
{
#if RD_KAFKA_VERSION >= 0x010000ff
	int purge_flags = 0;
	rd_kafka_resp_err_t err;

	if (!rk)
		return;

	purge_flags = RD_KAFKA_PURGE_F_QUEUE;
	slurm_rwlock_rdlock(&kafka_conf_rwlock);
	if (kafka_conf->flags & KAFKA_CONF_FLAG_PURGE_IN_FLIGHT)
		purge_flags |= RD_KAFKA_PURGE_F_INFLIGHT;
	if (kafka_conf->flags & KAFKA_CONF_FLAG_PURGE_NON_BLOCKING)
		purge_flags |= RD_KAFKA_PURGE_F_NON_BLOCKING;
	slurm_rwlock_unlock(&kafka_conf_rwlock);

	log_flag(JOBCOMP, "Purging messages with flags=0x%x", purge_flags);
	if ((err = rd_kafka_purge(rk, purge_flags)) !=
	    RD_KAFKA_RESP_ERR_NO_ERROR)
		error("%s: rd_kafka_purge(0x%x) failed: %s",
		      plugin_type, purge_flags, rd_kafka_err2str(err));
#endif
}

/*
 * Pack kafka_msg_t to a buffer.
 *
 * IN kafka_msg_t pointer.
 * IN/OUT buf_t pointer - buffer to store packed data, pointers automatically
 * advanced.
 */
static int _pack_kafka_msg(void *object, void *arg)
{
	kafka_msg_t *kafka_msg = object;
	buf_t *buffer = arg;

	xassert(kafka_msg);
	xassert(kafka_msg->job_id);
	xassert(kafka_msg->payload);
	xassert(buffer);

	pack32(kafka_msg->job_id, buffer);
	packstr(kafka_msg->payload, buffer);

	return 0;
}

static void _pack_jobcomp_kafka_state(buf_t *buffer)
{
	xassert(buffer);
	xassert(state_msg_list);

	/* Pack state header. */
	pack16(SLURM_PROTOCOL_VERSION, buffer);
	pack32(list_count(state_msg_list), buffer);

	/* Pack state body. */
	list_for_each_ro(state_msg_list, _pack_kafka_msg, buffer);
}

/*
 * Unpack kafka_msg_t from buffer and produce to librdkafka if no unpack error
 *
 * IN: uint16_t protocol_version
 * IN: buf_t pointer to buffer to unpack from
 *
 * RET: SLURM_ERROR if unpack_error or SLURM_SUCCESS
 */
static int _unpack_jobcomp_kafka_msg(uint16_t protocol_version, buf_t *buffer)
{
	uint32_t job_id = 0;
	char *payload = NULL;

	xassert(buffer);

	safe_unpack32(&job_id, buffer);
	safe_unpackstr(&payload, buffer);

	jobcomp_kafka_message_produce(job_id, payload);
	xfree(payload);

	return SLURM_SUCCESS;

unpack_error:
	if (!ignore_state_errors)
		fatal("Incomplete jobcomp/kafka state file, start with '-i' to ignore this. Warning: using -i will lose the data that can't be recovered.");
	error("Incomplete jobcomp/kafka state file");
	xfree(payload);

	return SLURM_ERROR;
}

static void _unpack_jobcomp_kafka_state(buf_t *buffer)
{
	uint32_t msg_cnt = 0;
	uint16_t protocol_version = NO_VAL16;

	xassert(buffer);

	/* Unpack state header. */
	safe_unpack16(&protocol_version, buffer);
	safe_unpack32(&msg_cnt, buffer);

	/* Unpack state body. */
	for (int i = 0; i < msg_cnt; i++)
		if (_unpack_jobcomp_kafka_msg(protocol_version,
					      buffer) != SLURM_SUCCESS)
			break;

	FREE_NULL_BUFFER(buffer);
	return;

unpack_error:
	if (!ignore_state_errors)
		fatal("Incomplete jobcomp/kafka state file, start with '-i' to ignore this. Warning: using -i will lose the data that can't be recovered.");
	error("Incomplete jobcomp/kafka state file");
	FREE_NULL_BUFFER(buffer);
}

static void _load_jobcomp_kafka_state(void)
{
	buf_t *buffer;

	if ((buffer = jobcomp_common_load_state_file(KAFKA_STATE_FILE)))
		_unpack_jobcomp_kafka_state(buffer);
}

static void _save_jobcomp_kafka_state(void)
{
	static uint32_t high_buffer_size = BUF_SIZE;
	buf_t *buffer = NULL;
	DEF_TIMERS;

	if (!(buffer = init_buf(high_buffer_size))) {
		error("%s: init_buf() failed. Can't save state.", plugin_type);
		return;
	}

	START_TIMER;
	_pack_jobcomp_kafka_state(buffer);
	jobcomp_common_write_state_file(buffer, KAFKA_STATE_FILE);
	END_TIMER2("save_jobcomp_kafka_state");

	FREE_NULL_BUFFER(buffer);
}

static void _terminate_poll_handler(void)
{
	slurm_mutex_lock(&poll_mutex);
	terminate = true;
	slurm_cond_broadcast(&poll_stop_cond);
	slurm_mutex_unlock(&poll_mutex);
	if (pthread_join(poll_thread, NULL))
		error("%s: pthread_join() on poll thread failed: %m",
		      plugin_type);
}

extern int jobcomp_kafka_message_init(void)
{
	if (_configure_rd_kafka_handle() != SLURM_SUCCESS)
		return SLURM_ERROR;

	state_msg_list = list_create(_destroy_kafka_msg);
	_load_jobcomp_kafka_state();
	slurm_thread_create(&poll_thread, _poll_handler, NULL);

	return SLURM_SUCCESS;
}

extern void jobcomp_kafka_message_fini(void)
{
	_terminate_poll_handler();
	_purge_rd_kafka_msgs();
	_flush_rd_kafka_msgs();
	_destroy_rd_kafka_handle();
	_save_jobcomp_kafka_state();
	FREE_NULL_LIST(state_msg_list);
}

/*
 * Attempt to produce a message in an asynchronous non-blocking way.
 *
 * IN: uint32_t job_id
 * IN: char *payload
 */
extern void jobcomp_kafka_message_produce(uint32_t job_id, char *payload)
{
	uint32_t *opaque = NULL;
	size_t len;
	rd_kafka_resp_err_t err;

	xassert(rk);

	len = strlen(payload) + 1;
	opaque = xmalloc(sizeof(*opaque));
	*opaque = job_id;

	slurm_rwlock_rdlock(&kafka_conf_rwlock);
	/*
	 * Arguments to rd_kafka_producev():
	 *
	 * 0. Producer handle.
	 * 1. Topic name. librdkafka makes a copy, so after call it can be
	 * freed.
	 * 2. Flag to tell librdkafka to make copy of payload, so after call it
	 * can be freed.
	 * 3. Message value (payload) and payload length.
	 * 4. Per-message opaque (see _dr_msg_cb rd_kafka_message_t->_private).
	 * Make a manual copy since librdkafka doesn't copy it.
	 * xfree() _private at the end of callback.
	 * 5. End sentinel
	 */
	err = rd_kafka_producev(rk,
				RD_KAFKA_V_TOPIC(kafka_conf->topic),
				RD_KAFKA_V_MSGFLAGS(RD_KAFKA_MSG_F_COPY),
				RD_KAFKA_V_VALUE(payload, len),
				RD_KAFKA_V_OPAQUE(opaque),
				RD_KAFKA_V_END);

	if (err == RD_KAFKA_RESP_ERR_NO_ERROR) {
		log_flag(JOBCOMP, "Produced JobId=%u message for topic '%s' to librdkafka queue.",
			 job_id, kafka_conf->topic);
		/* Do not xfree(opaque). Delivery msg callback will do it. */
	} else {
		error("%s: Failed to produce JobId=%u message for topic '%s': %s. Message discarded.",
		      plugin_type, job_id, kafka_conf->topic,
		      rd_kafka_err2str(err));
		xfree(opaque);
	}
	slurm_rwlock_unlock(&kafka_conf_rwlock);
}
