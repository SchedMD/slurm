/*
 * srun_cr.c - C/R wrapper for srun
 *
 */
#ifdef HAVE_CONFIG_H
#  include "config.h"
#endif

#include <stdint.h>
#include <stdio.h>
#include <signal.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <time.h>
#include <poll.h>
#include <pthread.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/time.h>
#include <sys/resource.h>
#include <sys/wait.h>

#include <libcr.h>
#include <slurm/slurm.h>

#include "src/common/log.h"
#include "src/common/xmalloc.h"
#include "src/common/xstring.h"

static char *cr_run_path = BLCR_HOME "/bin/cr_run";
static char *srun_path = SLURM_PREFIX "/bin/srun";

/* global variables */
static char **srun_argv = NULL;
static pid_t srun_pid = 0;

static uint32_t jobid = 0;
static uint32_t stepid = 0xFFFFFFFF;
static char *nodelist = NULL;

static char cr_sock_addr[32];
static int listen_fd = -1;

static int step_launched = 0;
static pthread_mutex_t step_launch_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t step_launch_cond = PTHREAD_COND_INITIALIZER;

static void remove_listen_socket(void);
static int  _wait_for_srun_connect(void);
static void _read_info_from_srun(int srun_fd);

/******************** copied and modified from cr_restart of BLCR ********************/
static void signal_child (int, siginfo_t *, void *);

static void
signal_self(int sig)
{
	struct sigaction sa;

	/* restore default (in kernel) handler */
	sa.sa_handler = SIG_DFL;
	sa.sa_flags = SA_RESTART | SA_NOMASK;
	sigemptyset(&sa.sa_mask);
	(void)sigaction(sig, &sa, NULL);

	/* send to self */
	raise(sig);

	/* restore self as handler */
	sa.sa_sigaction = &signal_child;
	sa.sa_flags = SA_RESTART | SA_NOMASK | SA_SIGINFO;
	(void)sigaction(sig, &sa, NULL);
}

static void
signal_child (int sig, siginfo_t *siginfo, void *context)
{
	if (srun_pid == 0) {	/* srun not forked yet */
		signal_self(sig);
		return;
	}
	
	if ((siginfo->si_code > 0) &&	/* si_code > 0 indicates sent by kernel */
	    (sig == SIGILL || sig == SIGFPE || sig == SIGBUS || sig == SIGSEGV )) {
		/* This signal is OUR error, so we don't forward */
		signal_self(sig);
	} else if (sig == SIGTSTP || sig == SIGTTIN || sig == SIGTTOU) {
		/* The catchable stop signals go to child AND self */
		(void)kill(srun_pid, sig);
		signal_self(sig);
	} else {
		/* default case */
		kill(srun_pid, sig);
	}
}

static void
mimic_exit(int status)
{
	if (WIFEXITED(status)) {
		/* easy to mimic normal return */
		exit(WEXITSTATUS(status));
	} else if (WIFSIGNALED(status)) {
		/* disable generation of a 'core' */
		struct rlimit r;
		r.rlim_cur = r.rlim_max = 0;
		(void)setrlimit(RLIMIT_CORE, &r);
		
		/* now raise the signal */
		signal_self(WTERMSIG(status));
	} else {
		warn("Unexpected status from child\n");
		exit(-1);
	}
}
/****************************************************************/
static void
on_child_exit(int signum)
{
	int status;

	if (waitpid(srun_pid, &status, WNOHANG) == srun_pid) {
		verbose("srun(%d) exited, status: %d", srun_pid, status);
		mimic_exit(status);
	}
}

static int
_slurm_debug_env_val (void)
{
        long int level = 0;
        const char *val;

        if ((val = getenv ("SLURM_DEBUG"))) {
                char *p;
                if ((level = strtol (val, &p, 10)) < -LOG_LEVEL_INFO)
                        level = -LOG_LEVEL_INFO;
                if (p && *p != '\0')
                        level = 0;
        }
        return ((int) level);
}


static void
update_env(char *name, char *val)
{
	char *buf = NULL;

        xstrfmtcat (buf, "%s=%s", name, val);
	if (putenv(buf)) {
		fatal("failed to update env: %m");
	}
}

static int
init_srun_argv(int argc, char **argv)
{
	int i;
	char *rchar;
	
	srun_argv = (char **)xmalloc(sizeof(char *) * (argc + 3));
	if (!srun_argv) {
		error("failed to malloc srun_argv: %m");
		return -1;
	}

	srun_argv[0] = cr_run_path;
	srun_argv[1] = "--omit";
	srun_argv[2] = srun_path;
	for (i = 1; i < argc; i ++) {
		srun_argv[i + 2] = argv[i];
	}
	srun_argv[argc + 2] = NULL;

	return  0;
}

/* remove the listen socket file */
static void
remove_listen_socket(void)
{
        unlink(cr_sock_addr);
}

/*
 * create_listen_socket - create a listening UNIX domain socket
 *     for srun to connect
 * RETURN: the socket fd on success, -1 on error
 */
static int
create_listen_socket(void)
{
	struct sockaddr_un sa;
	unsigned int sa_len;
	int re_use_addr = 1;


	close (listen_fd);	/* close possible old socket */
	
	sprintf(cr_sock_addr, "/tmp/sock.srun_cr.%u", (unsigned int)getpid());

	listen_fd = socket(AF_UNIX, SOCK_STREAM, 0);
	if (listen_fd < 0) {
		error("failed to create listen socket: %m");
		return -1;
	}
	
	sa.sun_family = AF_UNIX;
	strcpy(sa.sun_path, cr_sock_addr);
	sa_len = strlen(sa.sun_path) + sizeof(sa.sun_family);

	unlink(sa.sun_path);	/* remove possible old socket */

	setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR, (void*)&re_use_addr, sizeof(int));

	if (bind(listen_fd, (struct sockaddr *)&sa, sa_len) < 0) {
		error("failed to bind listen socket: %m");
		unlink(sa.sun_path);
		return -1;
	}

	if (listen(listen_fd, 2) < 0) {
		error("failed to listen: %m");
		unlink(sa.sun_path);
		return -1;
	}

	fd_set_nonblocking(listen_fd);
	
	return listen_fd;
}

/*
 * fork_exec_srun - fork and exec srun
 * GLOBALS cr_argv: arguments for running srun
 * RETURN: 0 on success, otherwise on error
 */
static int
fork_exec_srun(void)
{
	int rc = 0;
	sigset_t sigset;

	listen_fd = create_listen_socket();
	if (listen_fd < 0) {
		return -1;
	}

	srun_pid = fork();
	if (srun_pid < 0) {
		error("failed to fork child process: %m");
		return -1;
	} else if (srun_pid == 0) {	/* child */
		/*
		 * remove srun from the foreground process group,
		 * or Ctrl-C will cause SIGINT duplicated
		 */
		setpgrp();
		
		update_env("SLURM_SRUN_CR_SOCKET", cr_sock_addr);

		/*
		 * BLCR blocks all signals in thread-context callback functions
		 */
		sigemptyset(&sigset);
		pthread_sigmask(SIG_SETMASK, &sigset, NULL);
		
		execv(srun_argv[0], srun_argv);
		perror("failed execv srun");
		exit(-1);
	}

	return 0;
}

/*
 * get_step_image_dir - get the dir to store step task images
 * IN cr: checkpoint/restart
 * RET image dir on success, NULL on error
 *
 * NOTE: only can be called in callbak
 */
static char *
get_step_image_dir(int cr)
{
	const struct cr_checkpoint_info *ckpt_info;
	const struct cr_restart_info *rstrt_info;
	const char *dest;
	char *rchar, *dir;

	if (cr) {		/* checkpoint */
		ckpt_info = cr_get_checkpoint_info();
		if (!ckpt_info) {
			error("failed to get checkpoint info: %s", cr_strerror(errno));
			return NULL;
		}
		dest = ckpt_info->dest;
	} else {		/* retart */
		rstrt_info = cr_get_restart_info();
		if (!rstrt_info) {
			error("failed to get restart info: %s", cr_strerror(errno));
			return NULL;
		}
		dest = rstrt_info->src;
	}

	rchar = strrchr(dest, '/');
	if (rchar) {
		dir = xstrndup(dest, rchar - dest + 1);
	}
	xstrfmtcat(dir, "%u.%u", jobid, stepid);

	return dir;
}

static int
cr_callback(void *unused)
{
	int rc;
	char *step_image_dir = NULL;

	rc = CR_CHECKPOINT_READY;
	if (step_launched) {
		step_image_dir = get_step_image_dir(1);
		if (step_image_dir == NULL) {
			error ("failed to get step image directory");
			rc = CR_CHECKPOINT_PERM_FAILURE;
		} else if (slurm_checkpoint_tasks(jobid,
						  stepid,
						  time(NULL), /* timestamp */
						  step_image_dir,
						  60, /* wait */
						  nodelist) != SLURM_SUCCESS) {
			error ("failed to checkpoint step tasks");
			rc = CR_CHECKPOINT_PERM_FAILURE;
		}
		xfree(step_image_dir);
	}
	rc = cr_checkpoint(rc);	/* dump */
	
	if (rc < 0) {
		fatal("checkpoint failed: %s", cr_strerror(errno));
	} else if (rc == 0) {
		/* continue, nothing to do */
	} else {
		/* restarted */
		if (step_launched) {
			step_image_dir = get_step_image_dir(0);
			if (step_image_dir == NULL) {
				fatal("failed to get step image directory");
			}
			update_env("SLURM_RESTART_DIR", step_image_dir);
			xfree(step_image_dir);
		}

		if (fork_exec_srun()) {
			fatal("failed fork/exec srun");
		}

		/* XXX: step_launched => listen_fd valid */
		step_launched = 0;
		
		debug2("step not launched.");

		pthread_cond_broadcast(&step_launch_cond);
	}

	return 0;
}

int 
main(int argc, char **argv)
{
	int debug_level, sig, srun_fd;
	cr_client_id_t cr_id;
	struct sigaction sa;
        log_options_t logopt = LOG_OPTS_STDERR_ONLY;
	struct sockaddr_un ca;
	unsigned int ca_len = sizeof(ca);

	atexit(remove_listen_socket);
	
	/* copied from srun */
	debug_level = _slurm_debug_env_val();
        logopt.stderr_level += debug_level;
        log_init(xbasename(argv[0]), logopt, 0, NULL);

	if (init_srun_argv(argc, argv)) {
		fatal("failed to initialize arguments for running srun");
	}
	
	if ((cr_id = cr_init()) < 0) {
		fatal("failed to initialize libcr: %s", cr_strerror(errno));
	}
	(void)cr_register_callback(cr_callback, NULL, CR_THREAD_CONTEXT);
	
	/* forward signals. copied from cr_restart */
	sa.sa_sigaction = signal_child;
	sa.sa_flags = SA_RESTART | SA_NOMASK | SA_SIGINFO;
	sigemptyset(&sa.sa_mask);
	for (sig = 0;  sig < _NSIG; sig ++) {
		if (sig == SIGSTOP ||
		    sig == SIGKILL ||
		    sig == SIGCHLD)
			continue;
		sigaction(sig, &sa, NULL);
	}
	signal(SIGCHLD, on_child_exit);

	if ( fork_exec_srun() ) {
		fatal("failed fork/exec/wait srun");
	}

	while (1) {
		pthread_mutex_lock(&step_launch_mutex);
		while (step_launched) {
			/* just avoid busy waiting */
			pthread_cond_wait(&step_launch_cond, &step_launch_mutex);
		}
		pthread_mutex_unlock(&step_launch_mutex);

		if (_wait_for_srun_connect() < 0)
			continue;

		cr_enter_cs(cr_id); /* BEGIN CS: checkpoint(callback) will be delayed */

		srun_fd = accept(listen_fd, (struct sockaddr*)&ca, &ca_len);
		if (srun_fd < 0) {
			/* restarted before enter CS. socket will not be restored */
			if (errno == EBADF) { 
				cr_leave_cs(cr_id);
				continue;
			} else {
				fatal("failed to accept socket: %m");
			}
		}

		_read_info_from_srun(srun_fd);
		close(srun_fd);
		
		step_launched = 1;
		debug2("step launched");

		cr_leave_cs(cr_id); /* END CS */
	}

	return 0;
}

static int
_wait_for_srun_connect(void)
{
        struct pollfd fds[1];
        int rc;

        fds[0].fd = listen_fd;
        fds[0].events = POLLIN;

        while ((rc = poll(fds, 1, -1)) < 0) {
                switch (errno) {
		case EAGAIN:
		case EINTR:
			continue;
		case EBADF:	/* restarted */
			return -1;
		case ENOMEM:
		case EINVAL:
		case EFAULT:
			fatal("poll: %m");
		default:
			error("poll: %m. Continuing...");
                }
        }
	return 0;
}

static void
_read_info_from_srun(int srun_fd)
{
	int len;
	
	if (read(srun_fd, &jobid, sizeof(uint32_t)) != sizeof(uint32_t)) {
		fatal("failed to read jobid: %m");
	}

	if (read(srun_fd, &stepid, sizeof(uint32_t)) != sizeof(uint32_t)) {
		fatal("failed to read stepid: %m");
	}

	if (read(srun_fd, &len, sizeof(int)) != sizeof(int)) {
		fatal("failed to read nodelist length: %m");
	}

	xfree(nodelist);
	nodelist = (char *)xmalloc(len + 1);
	if (!nodelist) {
		fatal("failed to malloc nodelist: %m");
	}
	if (read(srun_fd, nodelist, len + 1) != len + 1) {
		fatal("failed to read nodelist: %m");
	}
}
