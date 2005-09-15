/*****************************************************************************\
 **  switch_federation.c - Library routines for initiating jobs on IBM 
 **	Federation
 **  $Id$
 *****************************************************************************
 *  Copyright (C) 2004 The Regents of the University of California.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Jason King <jking@llnl.gov>
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

#if     HAVE_CONFIG_H
#  include "config.h"
#endif

#include <fcntl.h>
#include <signal.h>
#include <sys/types.h>

#include <slurm/slurm_errno.h>
#include "src/common/slurm_xlator.h"
#include "src/plugins/switch/federation/federation.h"

#define BUF_SIZE 1024

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
	fed_init_cache();

	return SLURM_SUCCESS;
}

int fini ( void )
{
	return SLURM_SUCCESS;
}

/*
 * switch functions for global state save/restore
 */
int switch_p_libstate_save ( char * dir_name )
{
	int err;
	Buf buffer;
	char *file_name;
	int ret = SLURM_SUCCESS;
	int state_fd;
	
	buffer = init_buf(FED_LIBSTATE_LEN);
	(void)fed_libstate_save(buffer);
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

int switch_p_libstate_restore ( char * dir_name )
{
	char *data = NULL, *file_name;
	Buf buffer = NULL;
	int error_code = SLURM_SUCCESS;
	int state_fd, data_allocated = 0, data_read = 0, data_size = 0;

	if (dir_name == NULL)   /* clean start, no recovery */
		return fed_init();

	file_name = xstrdup(dir_name);
	xstrcat(file_name, "/fed_state");
	state_fd = open (file_name, O_RDONLY);
	if (state_fd >= 0) {
		data_allocated = BUF_SIZE;
		data = xmalloc(data_allocated);
		while (1) {
			data_read = read (state_fd, &data[data_size],
 					BUF_SIZE);
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

int switch_p_alloc_node_info(switch_node_info_t *switch_node)
{
	return fed_alloc_nodeinfo((fed_nodeinfo_t **)switch_node);
}

int switch_p_build_node_info(switch_node_info_t switch_node)
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

int switch_p_pack_node_info(switch_node_info_t switch_node, Buf buffer)
{
	return fed_pack_nodeinfo((fed_nodeinfo_t *)switch_node, buffer);
}

int switch_p_unpack_node_info(switch_node_info_t switch_node, Buf buffer)
{
	return fed_unpack_nodeinfo((fed_nodeinfo_t *)switch_node, buffer);
}

void switch_p_free_node_info(switch_node_info_t *switch_node)
{
	if(switch_node)
		fed_free_nodeinfo((fed_nodeinfo_t *)*switch_node, false);
}

char * switch_p_sprintf_node_info(switch_node_info_t switch_node, 
		char *buf, size_t size)
{
	return fed_print_nodeinfo((fed_nodeinfo_t *)switch_node, buf, size);
}

/*
 * switch functions for job step specific credential
 */
int switch_p_alloc_jobinfo(switch_jobinfo_t *switch_job)
{
	return fed_alloc_jobinfo((fed_jobinfo_t **)switch_job);
}

int switch_p_build_jobinfo(switch_jobinfo_t switch_job, char *nodelist, 
			   int *tasks_per_node, int cyclic_alloc, char *network) 
{
	hostlist_t list = NULL;
	bool sn_all;
	int i, err, nprocs = 0;
	int bulk_xfer = 0;

	debug3("network = \"%s\"", network);
	if(strstr(network, "ip") || strstr(network, "IP")) {
		debug2("federation: \"ip\" found in network string, "
		       "no network tables allocated");
		return SLURM_SUCCESS;
	} else {
		list = hostlist_create(nodelist);
		if(!list)
			fatal("hostlist_create(%s): %m", nodelist);

		if (strstr(network, "sn_all")
		    || strstr(network, "SN_ALL")) {
			debug3("Found sn_all in network string");
			sn_all = true;
		} else if (strstr(network, "sn_single")
			   || strstr(network, "SN_SINGLE")) {
			debug3("Found sn_single in network string");
			sn_all = false;
		} else {
			error("Network string contained neither sn_all "
			      "nor sn_single");
			return SLURM_ERROR;
		}
		for (i = 0; i < hostlist_count(list); i++)
			nprocs += tasks_per_node[i];

		if (strstr(network, "bulk_xfer")
		    || strstr(network, "BULK_XFER"))
			bulk_xfer = 1;
		err = fed_build_jobinfo((fed_jobinfo_t *)switch_job, list,
					nprocs,	cyclic_alloc, sn_all,
					bulk_xfer);
		hostlist_destroy(list);

		return err;
	}
}

switch_jobinfo_t switch_p_copy_jobinfo(switch_jobinfo_t switch_job)
{
	switch_jobinfo_t j;

	j = (switch_jobinfo_t)fed_copy_jobinfo((fed_jobinfo_t *)switch_job);
	if (!j)
		error("fed_copy_jobinfo failed");

	return j;
}

void switch_p_free_jobinfo(switch_jobinfo_t switch_job)
{
	return fed_free_jobinfo((fed_jobinfo_t *)switch_job);
}

int switch_p_pack_jobinfo(switch_jobinfo_t switch_job, Buf buffer)
{
	return fed_pack_jobinfo((fed_jobinfo_t *)switch_job, buffer);
}

int switch_p_unpack_jobinfo(switch_jobinfo_t switch_job, Buf buffer)
{
	return fed_unpack_jobinfo((fed_jobinfo_t *)switch_job, buffer);
}

extern int switch_p_get_jobinfo(switch_jobinfo_t switch_job, int key, 
	void *resulting_data)
{
	return fed_get_jobinfo((fed_jobinfo_t *)switch_job, key, resulting_data);
} 

int switch_p_job_step_complete(switch_jobinfo_t jobinfo, char *nodelist)
{
	hostlist_t list = NULL;
	int rc;

	list = hostlist_create(nodelist);
	rc = fed_job_step_complete((fed_jobinfo_t *)jobinfo, list);
	hostlist_destroy(list);

	return rc;
}

int switch_p_job_step_allocated(switch_jobinfo_t jobinfo, char *nodelist)
{
	hostlist_t list = NULL;
	int rc;

	list = hostlist_create(nodelist);
	rc = fed_job_step_allocated((fed_jobinfo_t *)jobinfo, list);
	hostlist_destroy(list);

	return rc;
}

void switch_p_print_jobinfo(FILE *fp, switch_jobinfo_t jobinfo)
{
	return;
}

char *switch_p_sprint_jobinfo(switch_jobinfo_t switch_jobinfo, char *buf,
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

int switch_p_job_preinit(switch_jobinfo_t jobinfo)
{
	return SLURM_SUCCESS;
}

int switch_p_job_init (switch_jobinfo_t jobinfo, uid_t uid)
{
	pid_t pid;
	
	pid = getpid();
	return fed_load_table((fed_jobinfo_t *)jobinfo, uid, pid);
}

int switch_p_job_fini (switch_jobinfo_t jobinfo)
{
	return SLURM_SUCCESS;
}

int switch_p_job_postfini(switch_jobinfo_t jobinfo, uid_t pgid, 
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

int switch_p_job_attach(switch_jobinfo_t jobinfo, char ***env, 
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
bool switch_p_no_frag(void)
{
	return false;
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

char *switch_p_strerror(int errnum)
{
	char *res = _lookup_slurm_api_errtab(errnum);
	return (res ? res : strerror(errnum));
}
