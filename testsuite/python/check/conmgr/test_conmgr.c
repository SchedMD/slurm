/*****************************************************************************\
 *  test_148_1.c - Test conmgr
\*****************************************************************************/

/* _GNU_SOURCE is required for unshare() */
#define _GNU_SOURCE
#include <check.h>

#include <fcntl.h>
#include <sys/socket.h>
#include <unistd.h>

#include "src/common/log.h"
#include "src/common/read_config.h"
#include "src/common/slurm_protocol_api.h"

#include "src/conmgr/conmgr.h"
#include "src/conmgr/mgr.h"

#if SLURM_VERSION_NUMBER >= SLURM_VERSION_NUM(26, 11, 0)
/* Issue 50528: conmgr redesigned */
#include "src/common/workerpool.h"
#endif

static slurm_addr_t listen_addr = { 0 };

#if defined(__linux__)
static void setup(void)
{
	int fd;
	char buffer[1024] = { 0 };
	uid_t uid = getuid();
	uid_t gid = getgid();
	log_options_t log_opts = LOG_OPTS_INITIALIZER;
	const char *debug_env = getenv("SLURM_DEBUG");
	const char *debug_flags_env = getenv("SLURM_DEBUG_FLAGS");

	/* Setup logging */
	if (debug_env)
		log_opts.stderr_level = log_string2num(debug_env);
	if (debug_flags_env)
		debug_str2flags(debug_flags_env, &slurm_conf.debug_flags);
	log_init("conmgr-test", log_opts, 0, NULL);

	/* Set the listen_addr */
	slurm_set_addr(&listen_addr, 80, "localhost");

	/* Create new network namespace */
	ck_assert(!unshare(CLONE_NEWNET | CLONE_NEWUSER));

	/* Map to root user/group */
	fd = open("/proc/self/uid_map", O_WRONLY);
	ck_assert(snprintf(buffer, ARRAY_SIZE(buffer), "0 %d 1", uid) > 0);
	ck_assert(write(fd, buffer, strlen(buffer)) == strlen(buffer));
	ck_assert(!close(fd));

	fd = open("/proc/self/gid_map", O_WRONLY);
	ck_assert(snprintf(buffer, ARRAY_SIZE(buffer), "0 %d 1", gid) > 0);
	ck_assert(write(fd, buffer, strlen(buffer) == strlen(buffer)));
	ck_assert(!close(fd));

	static const char STR_DENY[] = "deny";
	fd = open("/proc/self/setgroups", O_WRONLY);
	ck_assert(fd >= 0);
	ck_assert(write(fd, STR_DENY, strlen(STR_DENY)) == strlen(STR_DENY));
	ck_assert(!close(fd));

	/* Activate loopback in network namespace */
	ck_assert(!system("ip link set lo up"));
}
#else
static void setup(void)
{
	/* do nothing */
}
#endif

static void teardown(void)
{
	log_fini();
}

START_TEST(test_params)
{
	ck_assert(!conmgr_set_params("CONMGR_MAX_CONNECTIONS=3484"));
	ck_assert(mgr.conf_max_connections == 3484);

	ck_assert(!conmgr_set_params(
		"CONMGR_WAIT_WRITE_DELAY=845,,,,CONMGR_QUIESCE_TIMEOUT=3838"));

#if SLURM_VERSION_NUMBER >= SLURM_VERSION_NUM(26,5,0)
	ck_assert(mgr.timeouts.write_complete.tv_sec == 845);
	ck_assert(!mgr.timeouts.write_complete.tv_nsec);
	ck_assert(mgr.timeouts.quiesce.tv_sec == 3838);
	ck_assert(!mgr.timeouts.quiesce.tv_nsec);
#else 	/* Issue 50812: Convert all timeouts in conmgr to conmgr_timeouts_t.*/
	ck_assert(mgr.conf_delay_write_complete == 845);
	ck_assert(mgr.quiesce.conf_timeout.tv_sec == 3838);
#endif

	ck_assert(!conmgr_set_params(",,CONMGR_READ_TIMEOUT=9858,,,,,"));

#if SLURM_VERSION_NUMBER >= SLURM_VERSION_NUM(26,5,0)
	ck_assert(mgr.timeouts.read.tv_sec == 9858);
	ck_assert(!mgr.timeouts.read.tv_nsec);
#else 	/* Issue 50812: Convert all timeouts in conmgr to conmgr_timeouts_t.*/
	ck_assert(mgr.conf_read_timeout.tv_sec == 9858);
#endif

	ck_assert(!conmgr_set_params(
		"CONMGR_WRITE_TIMEOUT=3483,CONMGR_CONNECT_TIMEOUT=984"));

#if SLURM_VERSION_NUMBER >= SLURM_VERSION_NUM(26,5,0)
	ck_assert(mgr.timeouts.write.tv_sec == 3483);
	ck_assert(!mgr.timeouts.write.tv_nsec);
	ck_assert(mgr.timeouts.connect.tv_sec == 984);
	ck_assert(!mgr.timeouts.connect.tv_nsec);
#else 	/* Issue 50812: Convert all timeouts in conmgr to conmgr_timeouts_t.*/
	ck_assert(mgr.conf_write_timeout.tv_sec == 3483);
	ck_assert(mgr.conf_connect_timeout.tv_sec == 984);
#endif
}

END_TEST

START_TEST(test_reinit)
{
#if SLURM_VERSION_NUMBER >= SLURM_VERSION_NUM(26, 11, 0)
	conmgr_init(0);
	ck_assert(conmgr_enabled());
	conmgr_init(0);
	ck_assert(conmgr_enabled());
	conmgr_fini();
	ck_assert(conmgr_enabled());
	conmgr_request_shutdown();
	ck_assert(conmgr_enabled());
	conmgr_init(0);
	ck_assert(conmgr_enabled());
	conmgr_fini();
	ck_assert(conmgr_enabled());
	conmgr_fini();
	ck_assert(conmgr_enabled());
	conmgr_request_shutdown();
	ck_assert(conmgr_enabled());
#elif SLURM_VERSION_NUMBER >= SLURM_VERSION_NUM(25, 11, 0)
	conmgr_init(0, 0, 0);
	ck_assert(conmgr_enabled());
	conmgr_init(0, 0, 0);
	ck_assert(conmgr_enabled());
	conmgr_fini();
	ck_assert(conmgr_enabled());
	conmgr_request_shutdown();
	ck_assert(conmgr_enabled());
	conmgr_init(0, 0, 0);
	ck_assert(conmgr_enabled());
	conmgr_fini();
	ck_assert(conmgr_enabled());
	conmgr_fini();
	ck_assert(conmgr_enabled());
	conmgr_request_shutdown();
	ck_assert(conmgr_enabled());
#else
	ck_abort_msg("conmgr_init() has different arguments for versions < 25.11");
#endif
}

END_TEST

#if SLURM_VERSION_NUMBER >= SLURM_VERSION_NUM(26, 5, 0)
static int shutdown_arg = 0;
static bool on_finish_called = false;
static slurm_err_t on_finish_status_code = SLURM_SUCCESS;

static void *_shutdown_on_connection(conmgr_callback_args_t conmgr_args,
				     void *arg)
{
	/*
	 * The connection is established and tracked by conmgr now. Request a
	 * shutdown so that close_all_connections() tears it down while it is
	 * still open, which is what should set SLURM_SHUTTING_DOWN.
	 */
	conmgr_request_shutdown();
	return arg;
}

static int _noop_on_data(conmgr_callback_args_t conmgr_args, void *arg)
{
	return SLURM_SUCCESS;
}

static void _capture_on_finish(conmgr_callback_args_t conmgr_args, void *arg)
{
	on_finish_called = true;
	on_finish_status_code = conmgr_args.status_code;
}

START_TEST(test_status_code_on_shutdown)
{
	static const conmgr_events_t events = {
		.on_connection = _shutdown_on_connection,
		.on_data = _noop_on_data,
		.on_finish = _capture_on_finish,
	};
	int sv[2] = { -1, -1 };

	on_finish_called = false;
	on_finish_status_code = SLURM_SUCCESS;

	ck_assert_int_eq(socketpair(AF_UNIX, SOCK_STREAM, 0, sv), 0);

#if SLURM_VERSION_NUMBER >= SLURM_VERSION_NUM(26, 11, 0)
	/* Issue 50528: conmgr redesigned */
	workerpool_init(0, 0, NULL);
	conmgr_init(0);
#else
	conmgr_init(0, 0, 0);
#endif

	/* Hand one end of the socketpair to conmgr as a peer connection */
	ck_assert_int_eq(conmgr_process_fd(CON_TYPE_RAW,
					   &conmgr_timeouts_disabled, sv[0],
					   sv[0], &events, 0, NULL, 0, NULL,
					   &shutdown_arg),
			 SLURM_SUCCESS);

	/*
	 * Blocks until the shutdown requested from on_connection tears the
	 * connection down. conmgr_fini() guarantees all workers (and thus the
	 * on_finish callback) have completed before it returns.
	 */
	ck_assert_int_eq(conmgr_run(true), SLURM_SUCCESS);
	conmgr_fini();
#if SLURM_VERSION_NUMBER >= SLURM_VERSION_NUM(26, 11, 0)
	workerpool_fini();
#endif

	ck_assert(on_finish_called);
#if SLURM_VERSION_NUMBER >= SLURM_VERSION_NUM(26, 11, 0)
	/* Ticket 24952: SLURM_SHUTTING_DOWN added in 26.11+ */
	ck_assert_int_eq(on_finish_status_code, SLURM_SHUTTING_DOWN);
#else
	ck_assert_int_eq(on_finish_status_code, SLURM_SUCCESS);
#endif

	close(sv[1]);
}

END_TEST
#endif

#if SLURM_VERSION_NUMBER >= SLURM_VERSION_NUM(26, 11, 0)
/* Issue 51078: on_quiesce() added */
static pthread_mutex_t quiesce_lock = PTHREAD_MUTEX_INITIALIZER;
static int quiesce_new_arg = 0;
static int quiesce_con_arg = 0;
static int on_quiesce_calls = 0;
static int on_quiesce_rc = SLURM_SUCCESS;
static void *on_quiesce_arg = NULL;
static int connection_calls = 0;
static int quiesce_fd_rc = SLURM_SUCCESS;
static bool consume_data = true;
static int data_calls = 0;
static int barrier_calls = 0;
static int finish_calls = 0;
static slurm_err_t finish_status_code = SLURM_SUCCESS;
static conmgr_fd_ref_t *quiesce_ref = NULL;
static bool hold_on_quiesce = false;
static pthread_cond_t hold_on_quiesce_cond = PTHREAD_COND_INITIALIZER;

static void _quiesce_test_init(int rc, bool consume, const char *params,
			       int sv[2])
{
	on_quiesce_calls = 0;
	on_quiesce_rc = rc;
	on_quiesce_arg = NULL;
	connection_calls = 0;
	quiesce_fd_rc = SLURM_SUCCESS;
	consume_data = consume;
	data_calls = 0;
	barrier_calls = 0;
	finish_calls = 0;
	finish_status_code = SLURM_SUCCESS;
	hold_on_quiesce = false;

	ck_assert_int_eq(socketpair(AF_UNIX, SOCK_STREAM, 0, sv), 0);

	if (params)
		ck_assert(!conmgr_set_params(params));

	workerpool_init(0, 0, NULL);
	conmgr_init(0);
}

static void _quiesce_test_fini(int sv[2])
{
	conmgr_request_shutdown();
	conmgr_fini();
	workerpool_fini();

	if (sv[1] >= 0)
		close(sv[1]);
}

/* Wait up to 2 seconds for counter to reach count */
static bool _wait_for(const int *counter, int count)
{
	for (int i = 0; i < 2000; i++) {
		int value;

		slurm_mutex_lock(&quiesce_lock);
		value = *counter;
		slurm_mutex_unlock(&quiesce_lock);

		if (value >= count)
			return true;

		usleep(1000);
	}

	return false;
}

/* Return an arg that differs from new_arg to catch the wrong one reaching us */
static void *_return_arg_on_connection(conmgr_callback_args_t conmgr_args,
				       void *arg)
{
	slurm_mutex_lock(&quiesce_lock);
	connection_calls++;
	slurm_mutex_unlock(&quiesce_lock);

	return &quiesce_con_arg;
}

/* Keep a reference to quiesce the connection from the test */
static void *_ref_on_connection(conmgr_callback_args_t conmgr_args, void *arg)
{
	slurm_mutex_lock(&quiesce_lock);
	quiesce_ref = conmgr_fd_new_ref(conmgr_args.con);
	connection_calls++;
	slurm_mutex_unlock(&quiesce_lock);

	return &quiesce_con_arg;
}

static int _count_on_quiesce(conmgr_callback_args_t conmgr_args, void *arg)
{
	int rc;

	slurm_mutex_lock(&quiesce_lock);
	on_quiesce_calls++;
	on_quiesce_arg = arg;
	rc = on_quiesce_rc;
	slurm_mutex_unlock(&quiesce_lock);

	return rc;
}

static int _count_on_data(conmgr_callback_args_t conmgr_args, void *arg)
{
	bool consume;

	slurm_mutex_lock(&quiesce_lock);
	consume = consume_data;
	slurm_mutex_unlock(&quiesce_lock);

	if (consume) {
		buf_t *in = conmgr_fd_shadow_in_buffer(conmgr_args.con);

		conmgr_fd_mark_consumed_in_buffer(conmgr_args.con,
						  size_buf(in));
		FREE_NULL_BUFFER(in);
	}

	slurm_mutex_lock(&quiesce_lock);
	data_calls++;
	slurm_mutex_unlock(&quiesce_lock);

	return SLURM_SUCCESS;
}

static void _record_on_finish(conmgr_callback_args_t conmgr_args, void *arg)
{
	slurm_mutex_lock(&quiesce_lock);
	finish_calls++;
	finish_status_code = conmgr_args.status_code;
	slurm_mutex_unlock(&quiesce_lock);
}

static void _mark_barrier(conmgr_callback_args_t conmgr_args, void *arg)
{
	slurm_mutex_lock(&quiesce_lock);
	barrier_calls++;
	slurm_mutex_unlock(&quiesce_lock);
}

/* Quiesce only this connection so the peer's pending byte is held */
static void *_quiesce_on_connection(conmgr_callback_args_t conmgr_args,
				    void *arg)
{
	int rc = conmgr_quiesce_fd(conmgr_args.con);

	slurm_mutex_lock(&quiesce_lock);
	quiesce_fd_rc = rc;
	quiesce_ref = conmgr_fd_new_ref(conmgr_args.con);
	connection_calls++;
	slurm_mutex_unlock(&quiesce_lock);

	return &quiesce_con_arg;
}

static void _unquiesce_ref(void)
{
	conmgr_fd_ref_t *ref = NULL;

	slurm_mutex_lock(&quiesce_lock);
	SWAP(ref, quiesce_ref);
	slurm_mutex_unlock(&quiesce_lock);

	if (ref && conmgr_unquiesce_con(ref)) {
		slurm_mutex_lock(&quiesce_lock);
		quiesce_fd_rc = SLURM_ERROR;
		slurm_mutex_unlock(&quiesce_lock);
	}

	conmgr_fd_free_ref(&ref);
}

/* Count the call, then return only once the test releases it */
static int _held_on_quiesce(conmgr_callback_args_t conmgr_args, void *arg)
{
	int rc = _count_on_quiesce(conmgr_args, arg);

	slurm_mutex_lock(&quiesce_lock);
	while (hold_on_quiesce)
		slurm_cond_wait(&hold_on_quiesce_cond, &quiesce_lock);
	slurm_mutex_unlock(&quiesce_lock);

	return rc;
}

static void _release_on_quiesce(void)
{
	slurm_mutex_lock(&quiesce_lock);
	hold_on_quiesce = false;
	slurm_cond_broadcast(&hold_on_quiesce_cond);
	slurm_mutex_unlock(&quiesce_lock);
}

/* Resume the connection so the peer's pending byte arrives */
static int _resume_on_quiesce(conmgr_callback_args_t conmgr_args, void *arg)
{
	int rc = _count_on_quiesce(conmgr_args, arg);

	if (!rc)
		_unquiesce_ref();

	return rc;
}

/*
 * First call: quiesce only this connection so the global quiesce can complete
 * without it. Later calls: return on_quiesce_rc.
 */
static int _requiesce_on_quiesce(conmgr_callback_args_t conmgr_args, void *arg)
{
	int calls, rc = _count_on_quiesce(conmgr_args, arg);

	slurm_mutex_lock(&quiesce_lock);
	calls = on_quiesce_calls;
	slurm_mutex_unlock(&quiesce_lock);

	if (calls == 1) {
		int quiesce_rc;

		slurm_mutex_lock(&quiesce_lock);
		quiesce_ref = conmgr_fd_new_ref(conmgr_args.con);
		slurm_mutex_unlock(&quiesce_lock);

		quiesce_rc = conmgr_quiesce_fd(conmgr_args.con);

		slurm_mutex_lock(&quiesce_lock);
		quiesce_fd_rc = quiesce_rc;
		slurm_mutex_unlock(&quiesce_lock);

		return SLURM_SUCCESS;
	}

	return rc;
}

static void _start_con(const conmgr_events_t *events, int sv[2])
{
	ck_assert_int_eq(conmgr_process_fd(CON_TYPE_RAW,
					   &conmgr_timeouts_disabled, sv[0],
					   sv[0], events, 0, NULL, 0, NULL,
					   &quiesce_new_arg),
			 SLURM_SUCCESS);
	ck_assert_int_eq(conmgr_run(false), SLURM_SUCCESS);
}

START_TEST(test_on_quiesce_closes_idle)
{
	static const conmgr_events_t events = {
		.on_connection = _return_arg_on_connection,
		.on_data = _count_on_data,
		.on_finish = _record_on_finish,
		.on_quiesce = _count_on_quiesce,
	};
	int sv[2] = { -1, -1 };

	/* Longer than the test timeout: only on_quiesce() can end it in time */
	_quiesce_test_init(SLURM_COMMUNICATIONS_SHUTDOWN_ERROR, true,
			   "CONMGR_QUIESCE_TIMEOUT=60", sv);
	_start_con(&events, sv);

	/* on_quiesce() gets the arg from on_connection() once it returned */
	ck_assert(_wait_for(&connection_calls, 1));

	conmgr_quiesce(__func__);
	conmgr_unquiesce(__func__);

	_quiesce_test_fini(sv);

	ck_assert_int_eq(on_quiesce_calls, 1);
	ck_assert_ptr_eq(on_quiesce_arg, &quiesce_con_arg);
	ck_assert_int_eq(finish_calls, 1);
	ck_assert_int_eq(finish_status_code,
			 SLURM_COMMUNICATIONS_SHUTDOWN_ERROR);
}

END_TEST

START_TEST(test_on_quiesce_waits_once)
{
	static const conmgr_events_t events = {
		.on_connection = _return_arg_on_connection,
		.on_data = _count_on_data,
		.on_finish = _record_on_finish,
		.on_quiesce = _count_on_quiesce,
	};
	int sv[2] = { -1, -1 };

	/* on_quiesce() waits, so the quiesce times out and closes it */
	_quiesce_test_init(SLURM_SUCCESS, true, "CONMGR_QUIESCE_TIMEOUT=1", sv);
	_start_con(&events, sv);

	/* on_quiesce() gets the arg from on_connection() once it returned */
	ck_assert(_wait_for(&connection_calls, 1));

	conmgr_quiesce(__func__);
	conmgr_unquiesce(__func__);

	_quiesce_test_fini(sv);

	ck_assert_int_eq(on_quiesce_calls, 1);
	ck_assert_ptr_eq(on_quiesce_arg, &quiesce_con_arg);
	ck_assert_int_eq(finish_calls, 1);
	ck_assert_int_eq(finish_status_code,
			 SLURM_COMMUNICATIONS_QUIESCE_TIMEOUT);
}

END_TEST

START_TEST(test_quiesce_waits_without_on_quiesce)
{
	static const conmgr_events_t events = {
		.on_connection = _return_arg_on_connection,
		.on_data = _count_on_data,
		.on_finish = _record_on_finish,
	};
	int sv[2] = { -1, -1 };

	/* Nothing closes the idle connection so the quiesce has to time out */
	_quiesce_test_init(SLURM_SUCCESS, true, "CONMGR_QUIESCE_TIMEOUT=1", sv);
	_start_con(&events, sv);

	conmgr_quiesce(__func__);
	conmgr_unquiesce(__func__);

	_quiesce_test_fini(sv);

	ck_assert_int_eq(finish_calls, 1);
	ck_assert_int_eq(finish_status_code,
			 SLURM_COMMUNICATIONS_QUIESCE_TIMEOUT);
}

END_TEST

START_TEST(test_on_quiesce_each_quiesce)
{
	static const conmgr_events_t events = {
		.on_connection = _return_arg_on_connection,
		.on_data = _count_on_data,
		.on_finish = _record_on_finish,
		.on_quiesce = _requiesce_on_quiesce,
	};
	int sv[2] = { -1, -1 };

	_quiesce_test_init(SLURM_COMMUNICATIONS_SHUTDOWN_ERROR, true,
			   "CONMGR_QUIESCE_TIMEOUT=60", sv);
	_start_con(&events, sv);

	/* on_quiesce() gets the arg from on_connection() once it returned */
	ck_assert(_wait_for(&connection_calls, 1));

	/* Connection quiesces itself so this completes without closing it */
	conmgr_quiesce(__func__);
	conmgr_unquiesce(__func__);

	/* Already called for this quiesce so quiescing it alone must not */
	slurm_mutex_lock(&quiesce_lock);
	ck_assert_int_eq(on_quiesce_calls, 1);
	ck_assert_int_eq(quiesce_fd_rc, SLURM_SUCCESS);
	slurm_mutex_unlock(&quiesce_lock);
	_unquiesce_ref();
	ck_assert_int_eq(quiesce_fd_rc, SLURM_SUCCESS);

	/* Next quiesce must call on_quiesce() again */
	conmgr_quiesce(__func__);
	conmgr_unquiesce(__func__);

	_quiesce_test_fini(sv);

	ck_assert_int_eq(on_quiesce_calls, 2);
	ck_assert_ptr_eq(on_quiesce_arg, &quiesce_con_arg);
	ck_assert_int_eq(finish_calls, 1);
	ck_assert_int_eq(finish_status_code,
			 SLURM_COMMUNICATIONS_SHUTDOWN_ERROR);
}

END_TEST

START_TEST(test_on_quiesce_each_quiesce_fd)
{
	static const conmgr_events_t events = {
		.on_connection = _ref_on_connection,
		.on_data = _count_on_data,
		.on_finish = _record_on_finish,
		.on_quiesce = _count_on_quiesce,
	};
	int sv[2] = { -1, -1 };
	conmgr_fd_ref_t *ref = NULL;

	_quiesce_test_init(SLURM_SUCCESS, true, NULL, sv);
	_start_con(&events, sv);

	ck_assert(_wait_for(&connection_calls, 1));

	slurm_mutex_lock(&quiesce_lock);
	SWAP(ref, quiesce_ref);
	slurm_mutex_unlock(&quiesce_lock);

	ck_assert(ref);
	ck_assert_int_eq(conmgr_quiesce_con(ref), SLURM_SUCCESS);
	ck_assert(_wait_for(&on_quiesce_calls, 1));

	/*
	 * Wait for on_quiesce() to return as quiescing again before then only
	 * results in a single call
	 */
	conmgr_con_add_work_fifo(ref, _mark_barrier, NULL);
	ck_assert(_wait_for(&barrier_calls, 1));
	ck_assert_int_eq(conmgr_unquiesce_con(ref), SLURM_SUCCESS);

	/* Quiescing it again must call on_quiesce() again */
	ck_assert_int_eq(conmgr_quiesce_con(ref), SLURM_SUCCESS);
	ck_assert(_wait_for(&on_quiesce_calls, 2));
	ck_assert_int_eq(conmgr_unquiesce_con(ref), SLURM_SUCCESS);

	conmgr_fd_free_ref(&ref);

	_quiesce_test_fini(sv);

	ck_assert_int_eq(on_quiesce_calls, 2);
	ck_assert_ptr_eq(on_quiesce_arg, &quiesce_con_arg);
	ck_assert_int_eq(finish_calls, 1);
}

END_TEST

START_TEST(test_on_quiesce_once_while_running)
{
	static const conmgr_events_t events = {
		.on_connection = _ref_on_connection,
		.on_data = _count_on_data,
		.on_finish = _record_on_finish,
		.on_quiesce = _held_on_quiesce,
	};
	int sv[2] = { -1, -1 };
	conmgr_fd_ref_t *ref = NULL;

	_quiesce_test_init(SLURM_SUCCESS, true, NULL, sv);
	_start_con(&events, sv);

	ck_assert(_wait_for(&connection_calls, 1));

	slurm_mutex_lock(&quiesce_lock);
	SWAP(ref, quiesce_ref);
	hold_on_quiesce = true;
	slurm_mutex_unlock(&quiesce_lock);

	ck_assert(ref);
	ck_assert_int_eq(conmgr_quiesce_con(ref), SLURM_SUCCESS);
	ck_assert(_wait_for(&on_quiesce_calls, 1));

	/* Quiesced again before on_quiesce() returns must not call it again */
	ck_assert_int_eq(conmgr_unquiesce_con(ref), SLURM_SUCCESS);
	ck_assert_int_eq(conmgr_quiesce_con(ref), SLURM_SUCCESS);
	_release_on_quiesce();

	/* Work runs in order so any on_quiesce() queued above runs first */
	conmgr_con_add_work_fifo(ref, _mark_barrier, NULL);
	ck_assert(_wait_for(&barrier_calls, 1));
	ck_assert_int_eq(conmgr_unquiesce_con(ref), SLURM_SUCCESS);
	conmgr_fd_free_ref(&ref);

	_quiesce_test_fini(sv);

	ck_assert_int_eq(on_quiesce_calls, 1);
	ck_assert_int_eq(finish_calls, 1);
}

END_TEST

START_TEST(test_on_quiesce_unquiesced_while_running)
{
	static const conmgr_events_t events = {
		.on_connection = _ref_on_connection,
		.on_data = _count_on_data,
		.on_finish = _record_on_finish,
		.on_quiesce = _held_on_quiesce,
	};
	int sv[2] = { -1, -1 };
	conmgr_fd_ref_t *ref = NULL;

	_quiesce_test_init(SLURM_SUCCESS, true, NULL, sv);
	_start_con(&events, sv);

	ck_assert(_wait_for(&connection_calls, 1));

	slurm_mutex_lock(&quiesce_lock);
	SWAP(ref, quiesce_ref);
	hold_on_quiesce = true;
	slurm_mutex_unlock(&quiesce_lock);

	ck_assert(ref);
	ck_assert_int_eq(conmgr_quiesce_con(ref), SLURM_SUCCESS);
	ck_assert(_wait_for(&on_quiesce_calls, 1));

	ck_assert_int_eq(conmgr_unquiesce_con(ref), SLURM_SUCCESS);
	_release_on_quiesce();
	conmgr_con_add_work_fifo(ref, _mark_barrier, NULL);
	ck_assert(_wait_for(&barrier_calls, 1));

	/* Unquiesced before on_quiesce() returned, so it is called again */
	ck_assert_int_eq(conmgr_quiesce_con(ref), SLURM_SUCCESS);
	ck_assert(_wait_for(&on_quiesce_calls, 2));
	ck_assert_int_eq(conmgr_unquiesce_con(ref), SLURM_SUCCESS);
	conmgr_fd_free_ref(&ref);

	_quiesce_test_fini(sv);

	ck_assert_int_eq(on_quiesce_calls, 2);
	ck_assert_int_eq(finish_calls, 1);
}

END_TEST

START_TEST(test_on_quiesce_per_connection)
{
	static const conmgr_events_t events = {
		.on_connection = _quiesce_on_connection,
		.on_data = _count_on_data,
		.on_finish = _record_on_finish,
		.on_quiesce = _resume_on_quiesce,
	};
	int sv[2] = { -1, -1 };

	_quiesce_test_init(SLURM_SUCCESS, true, NULL, sv);

	/* Held until on_quiesce() resumes the connection */
	ck_assert_int_eq(write(sv[1], "x", 1), 1);
	_start_con(&events, sv);

	ck_assert(_wait_for(&data_calls, 1));

	_quiesce_test_fini(sv);

	ck_assert_int_eq(quiesce_fd_rc, SLURM_SUCCESS);
	ck_assert_int_eq(on_quiesce_calls, 1);
	ck_assert_ptr_eq(on_quiesce_arg, &quiesce_con_arg);
	ck_assert_int_eq(data_calls, 1);
	ck_assert_int_eq(finish_calls, 1);
}

END_TEST

START_TEST(test_on_quiesce_per_connection_closes)
{
	static const conmgr_events_t events = {
		.on_connection = _quiesce_on_connection,
		.on_data = _count_on_data,
		.on_finish = _record_on_finish,
		.on_quiesce = _resume_on_quiesce,
	};
	int sv[2] = { -1, -1 };

	_quiesce_test_init(SLURM_COMMUNICATIONS_SHUTDOWN_ERROR, true, NULL, sv);

	/* Never delivered as on_quiesce() closes the connection */
	ck_assert_int_eq(write(sv[1], "x", 1), 1);
	_start_con(&events, sv);

	ck_assert(_wait_for(&finish_calls, 1));
	_unquiesce_ref();

	_quiesce_test_fini(sv);

	ck_assert_int_eq(on_quiesce_calls, 1);
	ck_assert_int_eq(data_calls, 0);
	ck_assert_int_eq(finish_status_code,
			 SLURM_COMMUNICATIONS_SHUTDOWN_ERROR);
}

END_TEST

START_TEST(test_quiesce_per_connection_without_on_quiesce)
{
	static const conmgr_events_t events = {
		.on_connection = _quiesce_on_connection,
		.on_data = _count_on_data,
		.on_finish = _record_on_finish,
	};
	int sv[2] = { -1, -1 };

	_quiesce_test_init(SLURM_SUCCESS, true, NULL, sv);

	/* Held until resumed below */
	ck_assert_int_eq(write(sv[1], "x", 1), 1);
	_start_con(&events, sv);

	ck_assert(_wait_for(&connection_calls, 1));

	/* The quiesce must leave the connection open to get the byte */
	usleep(100000);
	slurm_mutex_lock(&quiesce_lock);
	ck_assert_int_eq(data_calls, 0);
	slurm_mutex_unlock(&quiesce_lock);
	_unquiesce_ref();
	ck_assert(_wait_for(&data_calls, 1));

	_quiesce_test_fini(sv);

	ck_assert_int_eq(quiesce_fd_rc, SLURM_SUCCESS);
	ck_assert_int_eq(finish_calls, 1);
}

END_TEST

/* Swap in events without on_connection() on first data, as http_con does */
static int _switch_on_data(conmgr_callback_args_t conmgr_args, void *arg)
{
	static const conmgr_events_t events = {
		.on_data = _count_on_data,
		.on_finish = _record_on_finish,
		.on_quiesce = _count_on_quiesce,
	};

	slurm_mutex_lock(&quiesce_lock);
	quiesce_ref = conmgr_fd_new_ref(conmgr_args.con);
	slurm_mutex_unlock(&quiesce_lock);

	return conmgr_con_set_events(conmgr_args.ref, &events, &quiesce_con_arg,
				     __func__);
}

START_TEST(test_on_quiesce_not_after_finish)
{
	static const conmgr_events_t events = {
		.on_connection = _return_arg_on_connection,
		.on_data = _switch_on_data,
	};
	int sv[2] = { -1, -1 };
	conmgr_fd_ref_t *ref = NULL;

	_quiesce_test_init(SLURM_SUCCESS, true, NULL, sv);

	ck_assert_int_eq(write(sv[1], "x", 1), 1);
	_start_con(&events, sv);
	ck_assert(_wait_for(&data_calls, 1));

	/* Peer closing ends the connection while the reference keeps it */
	close(sv[1]);
	sv[1] = -1;
	ck_assert(_wait_for(&finish_calls, 1));

	slurm_mutex_lock(&quiesce_lock);
	SWAP(ref, quiesce_ref);
	slurm_mutex_unlock(&quiesce_lock);

	ck_assert(ref);
	ck_assert_int_eq(conmgr_quiesce_con(ref), SLURM_SUCCESS);

	/* Work runs in order so any on_quiesce() queued above runs first */
	conmgr_con_add_work_fifo(ref, _mark_barrier, NULL);
	ck_assert(_wait_for(&barrier_calls, 1));

	ck_assert_int_eq(conmgr_unquiesce_con(ref), SLURM_SUCCESS);
	conmgr_fd_free_ref(&ref);

	_quiesce_test_fini(sv);

	ck_assert_int_eq(on_quiesce_calls, 0);
}

END_TEST

START_TEST(test_on_quiesce_not_while_closing)
{
	static const conmgr_events_t events = {
		.on_connection = _ref_on_connection,
		.on_data = _count_on_data,
		.on_finish = _record_on_finish,
		.on_quiesce = _count_on_quiesce,
	};
	int sv[2] = { -1, -1 };
	conmgr_fd_ref_t *ref = NULL;

	/* Longer than the test timeout: only the close can end it in time */
	_quiesce_test_init(SLURM_SUCCESS, true, "CONMGR_QUIESCE_TIMEOUT=60",
			   sv);
	_start_con(&events, sv);

	ck_assert(_wait_for(&connection_calls, 1));

	slurm_mutex_lock(&quiesce_lock);
	SWAP(ref, quiesce_ref);
	slurm_mutex_unlock(&quiesce_lock);

	/* Close is requested before the quiesce and before on_finish() */
	ck_assert(ref);
	conmgr_con_queue_close(ref);
	/* A reference keeps the connection, which would hold the quiesce */
	conmgr_fd_free_ref(&ref);

	conmgr_quiesce(__func__);
	conmgr_unquiesce(__func__);

	_quiesce_test_fini(sv);

	ck_assert_int_eq(on_quiesce_calls, 0);
	ck_assert_int_eq(finish_calls, 1);
}

END_TEST
#endif

extern int main(int argc, char **argv)
{
	int failures;

	/* Create main test case with fixtures and the main suite*/
	TCase *tcase = tcase_create("conmgr");
	tcase_add_unchecked_fixture(tcase, setup, teardown);
	tcase_add_test(tcase, test_params);
	tcase_add_test(tcase, test_reinit);
#if SLURM_VERSION_NUMBER >= SLURM_VERSION_NUM(26, 5, 0)
	tcase_add_test(tcase, test_status_code_on_shutdown);
#endif
#if SLURM_VERSION_NUMBER >= SLURM_VERSION_NUM(26, 11, 0)
	/* Issue 51078: on_quiesce() added */
	tcase_add_test(tcase, test_on_quiesce_closes_idle);
	tcase_add_test(tcase, test_on_quiesce_waits_once);
	tcase_add_test(tcase, test_quiesce_waits_without_on_quiesce);
	tcase_add_test(tcase, test_on_quiesce_each_quiesce);
	tcase_add_test(tcase, test_on_quiesce_each_quiesce_fd);
	tcase_add_test(tcase, test_on_quiesce_once_while_running);
	tcase_add_test(tcase, test_on_quiesce_unquiesced_while_running);
	tcase_add_test(tcase, test_on_quiesce_per_connection);
	tcase_add_test(tcase, test_on_quiesce_per_connection_closes);
	tcase_add_test(tcase, test_quiesce_per_connection_without_on_quiesce);
	tcase_add_test(tcase, test_on_quiesce_not_after_finish);
	tcase_add_test(tcase, test_on_quiesce_not_while_closing);
#endif

	Suite *suite = suite_create("conmgr");
	suite_add_tcase(suite, tcase);

	/* Create and run the runner */
	SRunner *sr = srunner_create(suite);
	srunner_run_all(sr, CK_VERBOSE);
	failures = srunner_ntests_failed(sr);
	srunner_free(sr);

	return failures;
}
