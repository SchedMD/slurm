/*****************************************************************************\
 *  switch_elan.c - Library routines for initiating jobs on QsNet. 
 *****************************************************************************
 *  Copyright (C) 2003-2007 The Regents of the University of California.
 *  Copyright (C) 2008 Lawrence Livermore National Security.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Kevin Tew <tew1@llnl.gov>, et. al.
 *  CODE-OCEC-09-009. All rights reserved.
 *  
 *  This file is part of SLURM, a resource management program.
 *  For details, see <https://computing.llnl.gov/linux/slurm/>.
 *  Please also read the included file: DISCLAIMER.
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

#if     HAVE_CONFIG_H
#  include "config.h"
#endif

#ifdef WITH_PTHREADS
#  include <pthread.h>
#endif /* WITH_PTHREADS */

#include <fcntl.h>
#include <signal.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include <dlfcn.h>

#include <slurm/slurm_errno.h>

#include "src/common/slurm_xlator.h"

#include "src/plugins/switch/elan/qsw.h"

#define BUFFER_SIZE 1024
#define QSW_STATE_VERSION "VER001"

/*
 * Static prototypes for network error resolver creation:
 */
static int   _set_elan_ids(void);
static void *_neterr_thr(void *arg);

static int             neterr_retval = 0;
static pthread_t       neterr_tid = 0;
static pthread_mutex_t neterr_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t  neterr_cond  = PTHREAD_COND_INITIALIZER;

/* Type for error string table entries */
typedef struct {
	int xe_number;
	char *xe_message;
} slurm_errtab_t;

static slurm_errtab_t slurm_errtab[] = {
	{0, "No error"},
	{-1, "Unspecified error"},

	/* Quadrics Elan routine error codes */

	{ ENOSLURM, 	/* oh no! */
	  "Out of slurm"					},
	{ EBADMAGIC_QSWLIBSTATE, 
	  "Bad magic in QSW libstate"				},
	{ EBADMAGIC_QSWJOBINFO, 
	  "Bad magic in QSW jobinfo"				},
	{ EINVAL_PRGCREATE,
	  "Program identifier in use or CPU count invalid, try again" },
	{ ECHILD_PRGDESTROY,
	  "Processes belonging to this program are still running" },
	{ EEXIST_PRGDESTROY, 
	  "Program identifier does not exist"			},
	{ EELAN3INIT, 
	  "Too many processes using Elan or mapping failure"	},
	{ EELAN3CONTROL, 
	  "Could not open elan3 control device"			},
	{ EELAN3CREATE, 
	  "Could not create elan capability"			},
	{ ESRCH_PRGADDCAP, 
	  "Program does not exist (addcap)"			},
	{ EFAULT_PRGADDCAP, 
	  "Capability has invalid address (addcap)"		},
	{ EINVAL_SETCAP, 
	  "Invalid context number (setcap)" 		 	},
	{ EFAULT_SETCAP, 
	  "Capability has invalid address (setcap)"		},
	{ EGETNODEID, 
	  "Cannot determine local elan address"			},
	{ EGETNODEID_BYHOST, 
	  "Cannot translate hostname to elan address"		},
	{ EGETHOST_BYNODEID, 
	  "Cannot translate elan address to hostname"		},
	{ ESRCH_PRGSIGNAL, 
	  "No such program identifier"				},
	{ EINVAL_PRGSIGNAL, 
	  "Invalid signal number"				}
};

/*
 * These variables are required by the generic plugin interface.  If they
 * are not found in the plugin, the plugin loader will ignore it.
 *
 * plugin_name - a string giving a human-readable description of the
 * plugin.  There is no maximum length, but the symbol must refer to
 * a valid string.
 *
 * plugin_type - a string suggesting the type of the plugin or its
 * applicability to a particular form of data or method of data handling.
 * If the low-level plugin API is used, the contents of this string are
 * unimportant and may be anything.  SLURM uses the higher-level plugin
 * interface which requires this string to be of the form
 *
 *      <application>/<method>
 *
 * where <application> is a description of the intended application of
 * the plugin (e.g., "switch" for SLURM switch) and <method> is a description 
 * of how this plugin satisfies that application.  SLURM will only load
 * a switch plugin if the plugin_type string has a prefix of "switch/".
 *
 * plugin_version - an unsigned 32-bit integer giving the version number
 * of the plugin.  If major and minor revisions are desired, the major
 * version number may be multiplied by a suitable magnitude constant such
 * as 100 or 1000.  Various SLURM versions will likely require a certain
 * minimum versions for their plugins as this API matures.
 */
const char plugin_name[]        = "switch Quadrics Elan3 or Elan4 plugin";
const char plugin_type[]        = "switch/elan";
const uint32_t plugin_version   = 90;

/*
 * init() is called when the plugin is loaded, before any other functions
 * are called.  Put global initialization here.
 */
int init ( void )
{
#ifdef HAVE_FRONT_END
	fatal("Plugin switch/elan is incompatable with front-end configuration");
#endif
	verbose("%s loaded", plugin_name);
	return SLURM_SUCCESS;
}

int fini ( void )
{
	return SLURM_SUCCESS;
}

/*
 * switch functions for global state save/restore
 */
int switch_p_libstate_save (char *dir_name)
{
	int error_code = SLURM_SUCCESS;
	qsw_libstate_t old_state = NULL;
	Buf buffer = NULL;
	int state_fd;
	char *file_name;

	if (qsw_alloc_libstate(&old_state))
		return SLURM_ERROR;
	qsw_fini(old_state);
	buffer = init_buf(1024);
	packstr(QSW_STATE_VERSION, buffer);
	(void) qsw_pack_libstate(old_state, buffer);
	file_name = xstrdup(dir_name);
	xstrcat(file_name, "/qsw_state");
	(void) unlink(file_name);
	state_fd = creat (file_name, 0600);
	if (state_fd == 0) {
		error ("Can't save state, error creating file %s %m",
			file_name);
		error_code = SLURM_ERROR;
	} else {
		char  *buf = get_buf_data(buffer);
		size_t len = get_buf_offset(buffer);
		while(1) {
			int wrote = write (state_fd, buf, len);
			if ((wrote < 0) && (errno == EINTR))
				continue;
			if (wrote == 0)
				break;
			if (wrote < 0) {
				error ("Can't save switch state: %m");
				error_code = SLURM_ERROR;
				break;
			}
			buf += wrote;
			len -= wrote;
		}
		close (state_fd);
	}
	xfree(file_name);

	if (buffer)
		free_buf(buffer);
	if (old_state)
		qsw_free_libstate(old_state);

	return error_code;
}

int switch_p_libstate_restore (char *dir_name, bool recover)
{
	char *data = NULL, *file_name;
	qsw_libstate_t old_state = NULL;
	Buf buffer = NULL;
	int error_code = SLURM_SUCCESS;
	int state_fd, data_allocated = 0, data_read = 0, data_size = 0;
	char *ver_str = NULL;
	uint32_t ver_str_len;

	if (!recover)	/* clean start, no recovery */
		return qsw_init(NULL);
	
	file_name = xstrdup(dir_name);
	xstrcat(file_name, "/qsw_state");
	state_fd = open (file_name, O_RDONLY);
	if (state_fd >= 0) {
		data_allocated = BUFFER_SIZE;
		data = xmalloc(data_allocated);
		while (1) {
			data_read = read (state_fd, &data[data_size],
					  BUFFER_SIZE);
			if ((data_read < 0) && (errno == EINTR))
				continue;
			if (data_read < 0) {
				error ("Read error on %s, %m", file_name);
				error_code = SLURM_ERROR;
				break;
			} else if (data_read == 0)
				break;
			data_size      += data_read;
			data_allocated += data_read;
			xrealloc(data, data_allocated);
		}
		close (state_fd);
		xfree(file_name);
	} else {
		error("No %s file for QSW state recovery", file_name);
		error("Starting QSW with clean state");
		xfree(file_name);
		return qsw_init(NULL);
	}

	if (error_code == SLURM_SUCCESS) {
		buffer = create_buf (data, data_size);
		data = NULL;    /* now in buffer, don't xfree() */
		if (buffer && (size_buf(buffer) >= sizeof(uint32_t) + 
				strlen(QSW_STATE_VERSION))) {
			char *ptr = get_buf_data(buffer);

			if (!memcmp(&ptr[sizeof(uint32_t)], 
					QSW_STATE_VERSION, 3)) {
				unpackstr_xmalloc(&ver_str, &ver_str_len, 
						buffer);
				debug3("qsw_state file version: %s", ver_str);
			}
		}
	}

	if (ver_str && (strcmp(ver_str, QSW_STATE_VERSION) == 0)) {
		if ((qsw_alloc_libstate(&old_state))
		||  (qsw_unpack_libstate(old_state, buffer) < 0))
			error_code = SLURM_ERROR;
	} else 
		error("qsw_state file is in an unsupported format, ignored");

	if (buffer)
		free_buf(buffer);
	xfree(data);
	xfree(ver_str);

	if (error_code == SLURM_SUCCESS)
		error_code = qsw_init(old_state);
	if (old_state)
		qsw_free_libstate(old_state);

	return error_code;
}

int switch_p_libstate_clear ( void )
{
	return qsw_clear();
}

/*
 * switch functions for job step specific credential
 */
int switch_p_alloc_jobinfo(switch_jobinfo_t **jp)
{
	return qsw_alloc_jobinfo((qsw_jobinfo_t *)jp);
}

int switch_p_build_jobinfo ( switch_jobinfo_t *switch_job, char *nodelist,
		uint16_t *tasks_per_node, int cyclic_alloc, char *network)
{
	int node_set_size = QSW_MAX_TASKS; /* overkill but safe */
	hostlist_t host_list;
	char *this_node_name;
	bitstr_t *nodeset;
	int node_id, error_code = SLURM_SUCCESS;
	int i, nnodes, ntasks = 0;
	
	if (!tasks_per_node) {
		slurm_seterrno(ENOMEM);
		return SLURM_ERROR;
	}
	
	if ((host_list = hostlist_create(nodelist)) == NULL)
		fatal("hostlist_create(%s): %m", nodelist);

	nnodes = hostlist_count(host_list);
	for (i = 0; i < nnodes; i++)
		ntasks += tasks_per_node[i];

	if (ntasks > node_set_size) {
		slurm_seterrno(ESLURM_BAD_TASK_COUNT);
		hostlist_destroy(host_list);
		return SLURM_ERROR;
	}

	if ((nodeset = bit_alloc (node_set_size)) == NULL)
		fatal("bit_alloc: %m");

	while ((this_node_name = hostlist_shift(host_list))) {
		node_id = qsw_getnodeid_byhost(this_node_name);
		if (node_id >= 0)
			bit_set(nodeset, node_id);
		else {
			error("qsw_getnodeid_byhost(%s) failure", 
					this_node_name);
			slurm_seterrno(ESLURM_INTERCONNECT_FAILURE);
			error_code = SLURM_ERROR;
		}
		free(this_node_name);
	}
	hostlist_destroy(host_list);

	if (error_code == SLURM_SUCCESS) {
		qsw_jobinfo_t j = (qsw_jobinfo_t) switch_job;
		error_code = qsw_setup_jobinfo(j, ntasks, nodeset, 
				tasks_per_node, cyclic_alloc); 
				/* allocs hw context */
	}

	bit_free(nodeset);
	return error_code;
}

switch_jobinfo_t *switch_p_copy_jobinfo(switch_jobinfo_t *j)
{
	return (switch_jobinfo_t) qsw_copy_jobinfo((qsw_jobinfo_t) j);
}

void switch_p_free_jobinfo(switch_jobinfo_t *k)
{
	qsw_free_jobinfo((qsw_jobinfo_t) k);
}

int switch_p_pack_jobinfo(switch_jobinfo_t *k, Buf buffer)
{
	return qsw_pack_jobinfo((qsw_jobinfo_t) k, buffer);
}

int switch_p_unpack_jobinfo(switch_jobinfo_t *k, Buf buffer)
{
	return qsw_unpack_jobinfo((qsw_jobinfo_t) k, buffer);
}

void switch_p_print_jobinfo(FILE *fp, switch_jobinfo_t *jobinfo)
{
	qsw_print_jobinfo(fp, (qsw_jobinfo_t) jobinfo);
}

char *switch_p_sprint_jobinfo(switch_jobinfo_t *switch_jobinfo, char *buf,
		size_t size)
{
	return qsw_capability_string((struct qsw_jobinfo *) switch_jobinfo,
		       buf, size);
}

/*
 * switch functions for job initiation
 */

static int _have_elan3 (void)
{
#if HAVE_LIBELAN3
	return (1);
#else
	struct stat st;

	if (stat ("/proc/qsnet/elan3/device0", &st) < 0)
		return (0);

	return (1);
#endif /* HAVE_LIBELAN3 */
	return (0);
}	

/*  Initialize node for use of the Elan interconnect by loading 
 *   elanid/hostname pairs then spawning the Elan network error
 *   resolver thread.
 *
 *  Main thread waits for neterr thread to successfully start before
 *   continuing.
 */
int switch_p_node_init ( void )
{
	pthread_attr_t attr;

	/*
	 *  Only need to run neterr resolver thread on Elan3 systems.
	 */
	if (!_have_elan3 ()) return SLURM_SUCCESS;

	/*
	 *  Load neterr elanid/hostname values into kernel 
	 */
	if (_set_elan_ids() < 0)
		return SLURM_ERROR;

	slurm_attr_init(&attr);
	if (pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED))
		error("pthread_attr_setdetachstate: %m");

	slurm_mutex_lock(&neterr_mutex);

	if (pthread_create(&neterr_tid, &attr, _neterr_thr, NULL)) {
		error("pthread_create: %m");
		slurm_attr_destroy(&attr);
		return SLURM_ERROR;
	}
	slurm_attr_destroy(&attr);

	/*
	 *  Wait for successful startup of neterr thread before
	 *   returning control to slurmd.
	 */
	pthread_cond_wait(&neterr_cond, &neterr_mutex);
	pthread_mutex_unlock(&neterr_mutex);

	return neterr_retval;


        return SLURM_SUCCESS;
}

/*
 * Use dlopen(3) for libelan3.so (when needed)
 *   This allows us to build a single version of the elan plugin
 *   for Elan3 and Elan4 on QsNetII systems.
 */
static void *elan3h = NULL;

/*
 *  * Wrapper functions for needed libelan3 functions
 *   */
static int _elan3_init_neterr_svc (int dbglvl)
{
	static int (*init_svc) (int);

	if (!(init_svc = dlsym (elan3h, "elan3_init_neterr_svc")))
		return (0);

	return (init_svc (dbglvl));
}


static int _elan3_register_neterr_svc (void)
{
	static int (*reg_svc) (void);

	if (!(reg_svc = dlsym (elan3h, "elan3_register_neterr_svc")))
		return (0);

	return (reg_svc ());
}

static int _elan3_run_neterr_svc (void)
{
	static int (*run_svc) ();

	if (!(run_svc = dlsym (elan3h, "elan3_run_neterr_svc")))
		return (0);

	return (run_svc ());
}


static int _elan3_load_neterr_svc (int i, char *host)
{
	static int (*load_svc) (int, char *);

	if (!(load_svc = dlsym (elan3h, "elan3_load_neterr_svc")))
		return (0);

	return (load_svc (i, host));
}

static void *_neterr_thr(void *arg)
{	
	debug3("Starting Elan network error resolver thread");

	if (!(elan3h = dlopen ("libelan3.so", RTLD_LAZY))) {
		error ("Unable to open libelan3.so: %s", dlerror ());
		goto fail;
	}

	if (!_elan3_init_neterr_svc(0)) {
		error("elan3_init_neterr_svc: %m");
		goto fail;
	}

	/* 
	 *  Attempt to register the neterr svc thread. If the address 
	 *   cannot be bound, then there is already a thread running, and
	 *   we should just exit with success.
	 */
	if (!_elan3_register_neterr_svc()) {
		if (errno != EADDRINUSE) {
			error("elan3_register_neterr_svc: %m");
			goto fail;
		}
		info("Warning: Elan error resolver thread already running");
	}

	/* 
	 *  Signal main thread that we've successfully initialized
	 */
	slurm_mutex_lock(&neterr_mutex);
	neterr_retval = 0;
	pthread_cond_signal(&neterr_cond);
	slurm_mutex_unlock(&neterr_mutex);

	/*
	 *  Run the network error resolver thread. This should
	 *   never return. If it does, there's not much we can do
	 *   about it.
	 */
	_elan3_run_neterr_svc();

	return NULL;

   fail:
	slurm_mutex_lock(&neterr_mutex);
	neterr_retval = SLURM_FAILURE;
	pthread_cond_signal(&neterr_cond);
	slurm_mutex_unlock(&neterr_mutex);

	return NULL;
}

/*
 *  Called from slurmd just before termination.
 *   We don't really need to do anything special for Elan, but
 *   we'll call pthread_cancel() on the neterr resolver thread anyhow.
 */
extern int switch_p_node_fini ( void )
{
#if HAVE_LIBELAN3
	int i;

	if (!neterr_tid)
		return SLURM_SUCCESS;

	for (i=0; i<4; i++) {
		if (pthread_cancel(neterr_tid)) {
			neterr_tid = 0;
			return SLURM_SUCCESS;
		}
		usleep(1000);
	}
	error("Could not kill switch elan pthread");
	return SLURM_ERROR;
#else  /* !HAVE_LIBELAN3 */

        return SLURM_SUCCESS;
#endif /*  HAVE_LIBELAN3 */
}

int switch_p_job_preinit ( switch_jobinfo_t *jobinfo )
{
	return SLURM_SUCCESS;
}

/* 
 * prepare node for interconnect use
 */
int switch_p_job_init ( switch_jobinfo_t *jobinfo, uid_t uid )
{
	char buf[4096];

	debug2("calling qsw_prog_init from process %lu", 
		(unsigned long) getpid());
	verbose("ELAN: %s", qsw_capability_string(
		(qsw_jobinfo_t)jobinfo, buf, 4096));

	if (qsw_prog_init((qsw_jobinfo_t)jobinfo, uid) < 0) {
		/*
		 * Check for EBADF, which probably means the rms
		 *  kernel module is not loaded.
		 */
		if (errno == EBADF)
			error("Initializing interconnect: "
			      "is the rms kernel module loaded?");
		else
			error ("qsw_prog_init: %m");

		qsw_print_jobinfo(log_fp(), (qsw_jobinfo_t)jobinfo);
		return SLURM_ERROR;
	}
	
	return SLURM_SUCCESS; 
}

int switch_p_job_fini ( switch_jobinfo_t *jobinfo )
{
	qsw_prog_fini((qsw_jobinfo_t)jobinfo); 
	return SLURM_SUCCESS;
}

int switch_p_job_postfini ( switch_jobinfo_t *jobinfo, uid_t pgid, 
				uint32_t job_id, uint32_t step_id )
{
	return SLURM_SUCCESS;
}

int switch_p_job_attach ( switch_jobinfo_t *jobinfo, char ***env, 
			uint32_t nodeid, uint32_t procid, uint32_t nnodes, 
			uint32_t nprocs, uint32_t rank )
{
	int id = -1;
	debug3("nodeid=%lu nnodes=%lu procid=%lu nprocs=%lu rank=%lu", 
		(unsigned long) nodeid, (unsigned long) nnodes, 
		(unsigned long) procid, (unsigned long) nprocs, 
		(unsigned long) rank);
	debug3("setting capability in process %lu", 
		(unsigned long) getpid());
	if (qsw_setcap((qsw_jobinfo_t) jobinfo, (int) procid) < 0) {
		error("qsw_setcap: %m");
		return SLURM_ERROR;
	}

	if (slurm_setenvpf(env, "RMS_RANK",   "%lu", (unsigned long) rank  ) 
	    < 0)
		return SLURM_ERROR;
	if (slurm_setenvpf(env, "RMS_NODEID", "%lu", (unsigned long) nodeid) 
	    < 0)
		return SLURM_ERROR;
	if (slurm_setenvpf(env, "RMS_PROCID", "%lu", (unsigned long) rank  ) 
	    < 0)
		return SLURM_ERROR;
	if (slurm_setenvpf(env, "RMS_NNODES", "%lu", (unsigned long) nnodes) 
	    < 0)
		return SLURM_ERROR;
	if (slurm_setenvpf(env, "RMS_NPROCS", "%lu", (unsigned long) nprocs) 
	    < 0)
		return SLURM_ERROR;

	/* 
	 * Tell libelan the key to use for Elan state shmem segment
	 */
	if (qsw_statkey ((qsw_jobinfo_t) jobinfo, &id) >= 0)
		slurm_setenvpf (env, "ELAN_STATKEY", "%d", id);

	return SLURM_SUCCESS;
}

extern int switch_p_get_jobinfo(switch_jobinfo_t *switch_job,
	int key, void *resulting_data)
{
	slurm_seterrno(EINVAL);
	return SLURM_ERROR;
}

static int 
_set_elan_ids(void)
{
	int i;

	for (i = 0; i <= qsw_maxnodeid(); i++) {
		char host[256]; 
		if (qsw_gethost_bynodeid(host, 256, i) < 0)
			continue;
			
		if (_elan3_load_neterr_svc(i, host) < 0)
			error("elan3_load_neterr_svc(%d, %s): %m", i, host);
	}

	return SLURM_SUCCESS;
}


/* 
 * Linear search through table of errno values and strings,
 * returns NULL on error, string on success.
 */
static char *_lookup_slurm_api_errtab(int errnum)
{
	char *res = NULL;
	int i;

	for (i = 0; i < sizeof(slurm_errtab) / sizeof(slurm_errtab_t); i++) {
		if (slurm_errtab[i].xe_number == errnum) {
			res = slurm_errtab[i].xe_message;
			break;
		}
	}
	return res;
}

extern int switch_p_get_errno(void)
{
	int err = slurm_get_errno();

	if ((err >= ESLURM_SWITCH_MIN) && (err <= ESLURM_SWITCH_MAX))
		return err;

	return SLURM_SUCCESS;
}

extern char *switch_p_strerror(int errnum)
{
	char *res = _lookup_slurm_api_errtab(errnum);
	return (res ? res : strerror(errnum));
}

/*
 * node switch state monitoring functions
 * required for IBM Federation switch
 */
extern int switch_p_clear_node_state(void)
{
	return SLURM_SUCCESS;
}

extern int switch_p_alloc_node_info(switch_node_info_t *switch_node)
{
	return SLURM_SUCCESS;
}

extern int switch_p_build_node_info(switch_node_info_t switch_node)
{
	return SLURM_SUCCESS;
}

extern int switch_p_pack_node_info(switch_node_info_t switch_node,
	Buf buffer)
{
	return SLURM_SUCCESS;
}

extern int switch_p_unpack_node_info(switch_node_info_t switch_node,
	Buf buffer)
{
	return SLURM_SUCCESS;
}

extern int switch_p_free_node_info(switch_node_info_t *switch_node)
{
	return SLURM_SUCCESS;
}

extern char*switch_p_sprintf_node_info(switch_node_info_t switch_node,
	char *buf, size_t size)
{
	if ((buf != NULL) && size) {
		buf[0] = '\0';
		return buf;
	}

	return NULL;
}

extern int switch_p_job_step_complete(switch_jobinfo_t *jobinfo,
	char *nodelist)
{
	qsw_teardown_jobinfo((qsw_jobinfo_t) jobinfo); /* frees hw context */

	return SLURM_SUCCESS;
}

extern int switch_p_job_step_part_comp(switch_jobinfo_t *jobinfo,
	char *nodelist)
{
	return SLURM_SUCCESS;
}

extern bool switch_p_part_comp(void)
{
	return false;
}

extern int switch_p_job_step_allocated(switch_jobinfo_t *jobinfo,
				       char *nodelist)
{
	return qsw_restore_jobinfo((qsw_jobinfo_t) jobinfo);
}

extern int switch_p_slurmctld_init( void )
{
	return SLURM_SUCCESS;
}

extern int switch_p_slurmd_init( void )
{
	return SLURM_SUCCESS;
}

extern int switch_p_slurmd_step_init( void )
{
	return SLURM_SUCCESS;
}
