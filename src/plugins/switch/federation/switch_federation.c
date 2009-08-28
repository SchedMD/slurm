/***************************************************************************** \
 *  switch_federation.c - Library routines for initiating jobs on IBM 
 *	Federation
 *****************************************************************************
 *  Copyright (C) 2004-2007 The Regents of the University of California.
 *  Copyright (C) 2008 Lawrence Livermore National Security.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Jason King <jking@llnl.gov>
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

#include <stdlib.h>
#include <fcntl.h>
#include <signal.h>
#include <sys/types.h>
#include <regex.h>
#include <stdlib.h>

#include <slurm/slurm_errno.h>
#include "src/common/macros.h"
#include "src/common/slurm_xlator.h"
#include "src/plugins/switch/federation/federation.h"

#define FED_BUF_SIZE 4096

bool fed_need_state_save = false;

static void _spawn_state_save_thread(char *dir);
static int  _switch_p_libstate_save(char * dir_name, bool free_flag);

/* Type for error string table entries */
typedef struct {
	int xe_number;
	char *xe_message;
} slurm_errtab_t;

static slurm_errtab_t slurm_errtab[] = {
	{0, "No error"},
	{-1, "Unspecified error"},

	/* Federation routine error codes */

	{ ESTATUS,
	  "Cannot get adapter status" },
	{ EADAPTER, 
	  "Open of adapter failed" },
	{ ENOADAPTER,
	  "No adapters found" },
	{ EBADMAGIC_FEDNODEINFO,
	  "Bad magic in Federation nodeinfo" },
	{ EBADMAGIC_FEDJOBINFO,
	  "Bad magic in Federation jobinfo" },
	{ EBADMAGIC_FEDLIBSTATE,
	  "Bad magic in Federation libstate" },
	{ EUNPACK,
	  "Error during unpack" },
	{ EHOSTNAME,
	  "Cannot get hostname" },
	{ ENOTSUPPORTED,
	  "This feature not currently supported" },
	{ EVERSION,
	  "Header/library version mismatch" },
	{ EWINDOW,
	  "Error allocating switch window" },
	{ EUNLOAD,
	  "Error unloading switch window table" }
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
const char plugin_name[]        = "switch FEDERATION plugin";
const char plugin_type[]        = "switch/federation";
const uint32_t plugin_version   = 100;

/*
 * init() is called when the plugin is loaded, before any other functions
 * are called.  Put global initialization here.
 */
int init ( void )
{
	verbose("%s loaded", plugin_name);

	return SLURM_SUCCESS;
}

int fini ( void )
{
	return fed_fini();
}

int switch_p_slurmctld_init( void )
{
	return fed_slurmctld_init();
}

int switch_p_slurmd_init( void )
{
	return fed_slurmd_init();
}

int switch_p_slurmd_step_init( void )
{
	return fed_slurmd_step_init();
}

/*
 * Switch functions for global state save
 * NOTE: Clears current switch state as needed for backup 
 * controller to repeatedly assume control primary server
 */
int switch_p_libstate_save ( char * dir_name )
{
	return _switch_p_libstate_save(dir_name, true);
}

/* save and purge the libstate if free_flag is true */
static int _switch_p_libstate_save ( char * dir_name, bool free_flag )
{
	Buf buffer;
	char *file_name;
	int ret = SLURM_SUCCESS;
	int state_fd;
	
	buffer = init_buf(FED_LIBSTATE_LEN);
	(void)fed_libstate_save(buffer, free_flag);
	file_name = xstrdup(dir_name);
	xstrcat(file_name, "/fed_state");
	(void)unlink(file_name);
	state_fd = creat(file_name, 0600);
	if(state_fd == 0) {
		error ("Can't save state, error creating file %s %m",
			file_name);
		ret = SLURM_ERROR;
	} else {
		char  *buf = get_buf_data(buffer);
		size_t len =get_buf_offset(buffer);
		while(1) {
        		int wrote = write (state_fd, buf, len);
        		if ((wrote < 0) && (errno == EINTR))
                		continue;
        		if (wrote == 0)
                		break;
        		if (wrote < 0) {
				error ("Can't save switch state: %m");
				ret = SLURM_ERROR;
				break;
			}
			buf += wrote;
			len -= wrote;
		}
		close(state_fd);
	}
	xfree(file_name);
	
	if(buffer)
		free_buf(buffer);
		
	return ret;
}


/*
 * Restore global nodeinfo from a file.
 *
 * NOTE: switch_p_libstate_restore is only called by slurmctld, and only
 * once at start-up.  We exploit (abuse?) this fact to spawn a pthread to
 * periodically call _switch_p_libstate_save().
 */
int switch_p_libstate_restore ( char * dir_name, bool recover )
{
	char *data = NULL, *file_name;
	Buf buffer = NULL;
	int error_code = SLURM_SUCCESS;
	int state_fd, data_allocated = 0, data_read = 0, data_size = 0;

	xassert(dir_name != NULL);

	_spawn_state_save_thread(xstrdup(dir_name));
	if (!recover)   /* clean start, no recovery */
		return fed_init();

	file_name = xstrdup(dir_name);
	xstrcat(file_name, "/fed_state");
	state_fd = open (file_name, O_RDONLY);
	if (state_fd >= 0) {
		data_allocated = FED_BUF_SIZE;
		data = xmalloc(data_allocated);
		while (1) {
			data_read = read (state_fd, &data[data_size],
					  FED_BUF_SIZE);
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
		error("No %s file for Federation state recovery", file_name);
		error("Starting Federation with clean state");
		xfree(file_name);
		return fed_init();
	}

        if (error_code == SLURM_SUCCESS) {
		buffer = create_buf (data, data_size);
		data = NULL;    /* now in buffer, don't xfree() */
		if (fed_libstate_restore(buffer) < 0)
			error_code = SLURM_ERROR;
        }

        if (buffer)
                free_buf(buffer);
        xfree(data);

        return error_code;
}

int switch_p_libstate_clear(void)
{
	return fed_libstate_clear();
}

/*
 * switch state monitoring functions
 */
/* NOTE:  we assume that once the switch state is cleared, 
 * notification of this will be forwarded to slurmctld.  We do not
 * enforce that in this function.
 */
/* FIX ME! - should use adapter name from federation.conf file now that
 *           we have that file support.
 */
#define ZERO 48
int switch_p_clear_node_state(void)
{
	int i, j;
	ADAPTER_RESOURCES res;
	char name[] = "sniN";
	int err;
	
	for(i = 0; i < FED_MAXADAPTERS; i++) {
		name[3] = i + ZERO;
		err = ntbl_adapter_resources(NTBL_VERSION, name, &res);
		if(err != NTBL_SUCCESS)
			continue;
		for(j = 0; j < res.window_count; j++)
			ntbl_clean_window(NTBL_VERSION, name, 
				ALWAYS_KILL, res.window_list[j]);
		free(res.window_list);
	}
	
	return SLURM_SUCCESS;
}

int switch_p_alloc_node_info(switch_node_info_t **switch_node)
{
	return fed_alloc_nodeinfo((fed_nodeinfo_t **)switch_node);
}

int switch_p_build_node_info(switch_node_info_t *switch_node)
{
	char hostname[256];
	char *tmp;

	if(gethostname(hostname, 256) < 0)
		slurm_seterrno_ret(EHOSTNAME);
	/* remove the domain portion, if necessary */
	tmp = strstr(hostname, ".");
	if(tmp)
		*tmp = '\0';
	return fed_build_nodeinfo((fed_nodeinfo_t *)switch_node, hostname);
}

int switch_p_pack_node_info(switch_node_info_t *switch_node, Buf buffer)
{
	return fed_pack_nodeinfo((fed_nodeinfo_t *)switch_node, buffer);
}

int switch_p_unpack_node_info(switch_node_info_t *switch_node, Buf buffer)
{
	return fed_unpack_nodeinfo((fed_nodeinfo_t *)switch_node, buffer);
}

void switch_p_free_node_info(switch_node_info_t **switch_node)
{
	if(switch_node)
		fed_free_nodeinfo((fed_nodeinfo_t *)*switch_node, false);
}

char * switch_p_sprintf_node_info(switch_node_info_t *switch_node, 
		char *buf, size_t size)
{
	return fed_print_nodeinfo((fed_nodeinfo_t *)switch_node, buf, size);
}

/*
 * switch functions for job step specific credential
 */
int switch_p_alloc_jobinfo(switch_jobinfo_t **switch_job)
{
	return fed_alloc_jobinfo((fed_jobinfo_t **)switch_job);
}

static char *adapter_name_check(char *network)
{
	regex_t re;
	char *pattern = "(sni[[:digit:]])";
        size_t nmatch = 5;
        regmatch_t pmatch[5];
        char *name;

	if (regcomp(&re, pattern, REG_EXTENDED) != 0) {
                error("sockname regex compilation failed\n");
                return NULL;
        }
	memset(pmatch, 0, sizeof(regmatch_t)*nmatch);
	if (regexec(&re, network, nmatch, pmatch, 0) == REG_NOMATCH) {
		return NULL;
	}
	name = strndup(network + pmatch[1].rm_so,
		       (size_t)(pmatch[1].rm_eo - pmatch[1].rm_so));
	regfree(&re);
	
	return name;
}

int switch_p_build_jobinfo(switch_jobinfo_t *switch_job, char *nodelist, 
			uint16_t *tasks_per_node, int cyclic_alloc, 
			char *network) 
{
	hostlist_t list = NULL;
	bool sn_all;
	int i, err, nprocs = 0;
	int bulk_xfer = 0;
	char *adapter_name = NULL;

	debug3("network = \"%s\"", network);
	if(strstr(network, "ip") || strstr(network, "IP")) {
		debug2("federation: \"ip\" found in network string, "
		       "no network tables allocated");
		return SLURM_SUCCESS;
	} else {
		if (strstr(network, "sn_all")
		    || strstr(network, "SN_ALL")) {
			debug3("Found sn_all in network string");
			sn_all = true;
		} else if (strstr(network, "sn_single")
			   || strstr(network, "SN_SINGLE")) {
			debug3("Found sn_single in network string");
			sn_all = false;
		} else if ((adapter_name = adapter_name_check(network))) {
			debug3("Found adapter %s in network string",
			       adapter_name);
			sn_all = false;
		} else {
			/* default to sn_all */
			sn_all = true;
		}

		list = hostlist_create(nodelist);
		if(!list)
			fatal("hostlist_create(%s): %m", nodelist);
		for (i = 0; i < hostlist_count(list); i++)
			nprocs += tasks_per_node[i];

		if (strstr(network, "bulk_xfer")
		    || strstr(network, "BULK_XFER"))
			bulk_xfer = 1;
		err = fed_build_jobinfo((fed_jobinfo_t *)switch_job, list,
					nprocs,	sn_all, adapter_name,
					bulk_xfer);
		hostlist_destroy(list);
		if (adapter_name)
			free(adapter_name);

		return err;
	}
}

switch_jobinfo_t *switch_p_copy_jobinfo(switch_jobinfo_t *switch_job)
{
	switch_jobinfo_t *j;

	j = (switch_jobinfo_t *)fed_copy_jobinfo((fed_jobinfo_t *)switch_job);
	if (!j)
		error("fed_copy_jobinfo failed");

	return j;
}

void switch_p_free_jobinfo(switch_jobinfo_t *switch_job)
{
	return fed_free_jobinfo((fed_jobinfo_t *)switch_job);
}

int switch_p_pack_jobinfo(switch_jobinfo_t *switch_job, Buf buffer)
{
	return fed_pack_jobinfo((fed_jobinfo_t *)switch_job, buffer);
}

int switch_p_unpack_jobinfo(switch_jobinfo_t *switch_job, Buf buffer)
{
	return fed_unpack_jobinfo((fed_jobinfo_t *)switch_job, buffer);
}

extern int switch_p_get_jobinfo(switch_jobinfo_t *switch_job, int key, 
				void *resulting_data)
{
	return fed_get_jobinfo((fed_jobinfo_t *)switch_job, key,
			       resulting_data);
} 

static inline int _make_step_comp(switch_jobinfo_t *jobinfo, char *nodelist)
{
	hostlist_t list = NULL;
	int rc;

	list = hostlist_create(nodelist);
	rc = fed_job_step_complete((fed_jobinfo_t *)jobinfo, list);
	hostlist_destroy(list);

	return rc;
}

extern int switch_p_job_step_complete(switch_jobinfo_t *jobinfo, char *nodelist)
{
	return _make_step_comp(jobinfo, nodelist);
}

extern int switch_p_job_step_part_comp(switch_jobinfo_t *jobinfo, 
				       char *nodelist)
{
	return _make_step_comp(jobinfo, nodelist);
}

extern bool switch_p_part_comp(void)
{
	return true;
}

extern int switch_p_job_step_allocated(switch_jobinfo_t *jobinfo, char *nodelist)
{
	hostlist_t list = NULL;
	int rc;

	list = hostlist_create(nodelist);
	rc = fed_job_step_allocated((fed_jobinfo_t *)jobinfo, list);
	hostlist_destroy(list);

	return rc;
}

void switch_p_print_jobinfo(FILE *fp, switch_jobinfo_t *jobinfo)
{
	return;
}

char *switch_p_sprint_jobinfo(switch_jobinfo_t *switch_jobinfo, char *buf,
		size_t size)
{
	return NULL;
}

/*
 * switch functions for job initiation
 */
static int _ntbl_version_ok(void)
{
	return((ntbl_version() == NTBL_VERSION) ? 1 : 0);
}

int switch_p_node_init(void)
{
	/* check to make sure the version of the library we compiled with
	 * matches the one dynamically linked
	 */
	if(!_ntbl_version_ok()) {
		slurm_seterrno_ret(EVERSION);
	}
		
	return SLURM_SUCCESS;
}

int switch_p_node_fini(void)
{
	return SLURM_SUCCESS;
}

int switch_p_job_preinit(switch_jobinfo_t *jobinfo)
{
	return SLURM_SUCCESS;
}

int switch_p_job_init (switch_jobinfo_t *jobinfo, uid_t uid)
{
	pid_t pid;
	
	pid = getpid();
	return fed_load_table((fed_jobinfo_t *)jobinfo, uid, pid);
}

int switch_p_job_fini (switch_jobinfo_t *jobinfo)
{
	return SLURM_SUCCESS;
}

int switch_p_job_postfini(switch_jobinfo_t *jobinfo, uid_t pgid, 
				uint32_t job_id, uint32_t step_id)
{
	int err;

	/*
	 *  Kill all processes in the job's session
	 */
	if(pgid) {
		debug2("Sending SIGKILL to pgid %lu", 
			(unsigned long) pgid); 
		kill(-pgid, SIGKILL);
	} else
		debug("Job %u.%u: Bad pid valud %lu", job_id, 
		      step_id, (unsigned long) pgid);

	err = fed_unload_table((fed_jobinfo_t *)jobinfo);
	if(err != SLURM_SUCCESS)
		return SLURM_ERROR;

	return SLURM_SUCCESS;
}

int switch_p_job_attach(switch_jobinfo_t *jobinfo, char ***env, 
			uint32_t nodeid, uint32_t procid, uint32_t nnodes, 
			uint32_t nprocs, uint32_t rank)
{
#if 0
	printf("nodeid = %u\n", nodeid);
	printf("procid = %u\n", procid);
	printf("nnodes = %u\n", nnodes);
	printf("nprocs = %u\n", nprocs);
	printf("rank = %u\n", rank);
#endif
	return SLURM_SUCCESS;
}

/*
 * switch functions for other purposes
 */

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

char *switch_p_strerror(int errnum)
{
	char *res = _lookup_slurm_api_errtab(errnum);
	return (res ? res : strerror(errnum));
}


static void *_state_save_thread(void *arg)
{
	char *dir_name = (char *)arg;

	while (1) {
		sleep(300);
		if (fed_need_state_save) {
			fed_need_state_save = false;
			_switch_p_libstate_save(dir_name, false);
		}
	}
}

static void _spawn_state_save_thread(char *dir)
{
	pthread_attr_t attr;
	pthread_t id;

	slurm_attr_init(&attr);

	if (pthread_create(&id, &attr, &_state_save_thread, (void *)dir) != 0)
		error("Could not start federation state saving pthread");

	slurm_attr_destroy(&attr);
}
