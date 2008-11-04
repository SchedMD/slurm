/*****************************************************************************\
 *  bridge_linker.c
 * 
 *****************************************************************************
 *  Copyright (C) 2004 The Regents of the University of California.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Dan Phung <phung4@llnl.gov>, Danny Auble <da@llnl.gov>
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


#include "bridge_linker.h"

#ifdef HAVE_BG_FILES
typedef struct {
	/* all the rm functions */
	status_t (*set_serial)(const rm_serial_t serial);
	status_t (*get_bg)(my_bluegene_t **bg);
	status_t (*add_partition)(rm_partition_t *partition);
	status_t (*get_partition)(pm_partition_id_t pid, 
				  rm_partition_t **partition);
	status_t (*get_partition_info)(pm_partition_id_t pid,
				       rm_partition_t **partition);
	status_t (*modify_partition)(pm_partition_id_t pid, 
				     enum rm_modify_op op, const void *data);
	status_t (*set_part_owner)(pm_partition_id_t pid, const char *name);
	status_t (*add_part_user)(pm_partition_id_t pid, const char *name);
	status_t (*remove_part_user)(pm_partition_id_t pid, const char *name);
	status_t (*remove_partition)(pm_partition_id_t pid);
	status_t (*get_partitions)(rm_partition_state_flag_t flag, 
				   rm_partition_list_t **part_list);
	status_t (*get_partitions_info)(rm_partition_state_flag_t flag, 
					rm_partition_list_t **part_list);
	status_t (*get_job)(db_job_id_t dbJobId, rm_job_t **job);
	status_t (*get_jobs)(rm_job_state_flag_t flag, rm_job_list_t **jobs);
	status_t (*remove_job)(db_job_id_t jid);  
	status_t (*get_nodecards)(rm_bp_id_t bpid,
				  rm_nodecard_list_t **nc_list);
	status_t (*new_partition)(rm_partition_t **partition);
	status_t (*free_partition)(rm_partition_t *partition);
	status_t (*free_job)(rm_job_t *job);
	status_t (*free_bg)(my_bluegene_t *bg);
	status_t (*free_partition_list)(rm_partition_list_t *part_list);
	status_t (*free_job_list)(rm_job_list_t *job_list);  
	status_t (*free_nodecard_list)(rm_nodecard_list_t *nc_list);
	status_t (*get_data)(rm_element_t* element,
			     enum rm_specification field, void *data);
	status_t (*set_data)(rm_element_t* element, 
			     enum rm_specification field, void *data);
	
	/* all the jm functions */
	status_t (*signal_job)(db_job_id_t jid, rm_signal_t sig);
	status_t (*cancel_job)(db_job_id_t jid);
  
	/* all the pm functions */
	status_t (*create_partition)(pm_partition_id_t pid);
	status_t (*destroy_partition)(pm_partition_id_t pid);
	
	/* set say message stuff */
	void (*set_log_params)(FILE * stream, unsigned int level);

} bridge_api_t;

pthread_mutex_t api_file_mutex = PTHREAD_MUTEX_INITIALIZER;
bridge_api_t bridge_api;
bool initialized = false;
bool have_db2 = true;
void *handle = NULL;
	
int _get_syms(int n_syms, const char *names[], void *ptrs[])
{
        int i, count;
#ifdef HAVE_BGL	
#ifdef BG_DB2_SO
	void *db_handle = NULL;
	db_handle = dlopen (BG_DB2_SO, RTLD_LAZY);
	if (!db_handle) {
		have_db2 = false;
		debug("%s\n", dlerror());
		return 0;
	}
	dlclose(db_handle);
#else
	fatal("No BG_DB2_SO is set, can't run.");
#endif
#endif // HAVE_BGL

#ifdef BG_BRIDGE_SO
	handle = dlopen (BG_BRIDGE_SO, RTLD_LAZY);
	if (!handle) {
		have_db2 = false;
		debug("%s\n", dlerror());
		return 0;
	}
#else
	fatal("No BG_BRIDGE_SO is set, can't run.");
#endif
	
	dlerror();    /* Clear any existing error */
        count = 0;
        for ( i = 0; i < n_syms; ++i ) {
                ptrs[i] = dlsym(handle, names[i]);
                if (ptrs[i]) {
			++count;
		} else
			fatal("Can't find %s in api", names[i]);
	}
        return count;
}



extern int bridge_init()
{
#ifdef HAVE_BGP
	static const char *syms[] = {
		"rm_set_serial",
		"rm_get_BGP",
		"rm_add_partition",
		"rm_get_partition",
		"rm_get_partition_info",
		"rm_modify_partition",
		"rm_set_part_owner",
		"rm_add_part_user",
		"rm_remove_part_user",
		"rm_remove_partition",
		"rm_get_partitions",
		"rm_get_partitions_info",
		"rm_get_job",
		"rm_get_jobs",
		"rm_remove_job",
		"rm_get_nodecards",
		"rm_new_partition",
		"rm_free_partition",
		"rm_free_job",
		"rm_free_BGP",
		"rm_free_partition_list",
		"rm_free_job_list",
		"rm_free_nodecard_list",
		"rm_get_data",
		"rm_set_data",
		"jm_signal_job",
		"jm_cancel_job",
		"pm_create_partition",
		"pm_destroy_partition",
		"setSayMessageParams"
	};
#elseif HAVE_BGL
	static const char *syms[] = {
		"rm_set_serial",
		"rm_get_BGL",
		"rm_add_partition",
		"rm_get_partition",
		"rm_get_partition_info",
		"rm_modify_partition",
		"rm_set_part_owner",
		"rm_add_part_user",
		"rm_remove_part_user",
		"rm_remove_partition",
		"rm_get_partitions",
		"rm_get_partitions_info",
		"rm_get_job",
		"rm_get_jobs",
		"rm_remove_job",
		"rm_get_nodecards",
		"rm_new_partition",
		"rm_free_partition",
		"rm_free_job",
		"rm_free_BGL",
		"rm_free_partition_list",
		"rm_free_job_list",
		"rm_free_nodecard_list",
		"rm_get_data",
		"rm_set_data",
		"jm_signal_job",
		"jm_cancel_job",
		"pm_create_partition",
		"pm_destroy_partition",
		"setSayMessageParams"
	};
#endif
	int n_syms = sizeof( syms ) / sizeof( char * );
	int rc;

	if(initialized)
		return 1;

	initialized = true;
	if(!_get_syms(n_syms, syms, (void **) &bridge_api))
		return 0;
#ifdef BG_SERIAL
	debug("setting the serial to %s", BG_SERIAL);
	slurm_mutex_lock(&api_file_mutex);
	rc = (*(bridge_api.set_serial))(BG_SERIAL);
	slurm_mutex_unlock(&api_file_mutex);
	debug2("done %d", rc);
#else
	fatal("No BG_SERIAL is set, can't run.");
#endif
	return 1;
	
}

extern int bridge_fini()
{
	if(handle)
		dlclose(handle);

	return SLURM_ERROR;
}

extern status_t bridge_get_bg(my_bluegene_t **bg)
{
	int rc = CONNECTION_ERROR;
	if(!bridge_init())
		return rc;

	slurm_mutex_lock(&api_file_mutex);
	rc = (*(bridge_api.get_bg))(bg);
	slurm_mutex_unlock(&api_file_mutex);
	return rc;
}

extern status_t bridge_add_block(rm_partition_t *partition)
{
	int rc = CONNECTION_ERROR;
	if(!bridge_init())
		return rc;
	
	slurm_mutex_lock(&api_file_mutex);
	rc = (*(bridge_api.add_partition))(partition);
	slurm_mutex_unlock(&api_file_mutex);
	return rc;

}

extern status_t bridge_get_block(pm_partition_id_t pid, 
				 rm_partition_t **partition)
{
	int rc = CONNECTION_ERROR;
	if(!bridge_init())
		return rc;
	
	slurm_mutex_lock(&api_file_mutex);
	rc = (*(bridge_api.get_partition))(pid, partition);
	slurm_mutex_unlock(&api_file_mutex);
	return rc;

}

extern status_t bridge_get_block_info(pm_partition_id_t pid, 
				      rm_partition_t **partition)
{
	int rc = CONNECTION_ERROR;
	if(!bridge_init())
		return rc;
	
	slurm_mutex_lock(&api_file_mutex);
	rc = (*(bridge_api.get_partition_info))(pid, partition);
	slurm_mutex_unlock(&api_file_mutex);
	return rc;

}

extern status_t bridge_modify_block(pm_partition_id_t pid, 
				    enum rm_modify_op op, const void *data)
{
	int rc = CONNECTION_ERROR;
	if(!bridge_init())
		return rc;
	
	slurm_mutex_lock(&api_file_mutex);
	rc = (*(bridge_api.modify_partition))(pid, op, data);
	slurm_mutex_unlock(&api_file_mutex);
	return rc;

}
	
extern status_t bridge_set_block_owner(pm_partition_id_t pid, const char *name)
{
	int rc = CONNECTION_ERROR;
	if(!bridge_init())
		return rc;
	
	slurm_mutex_lock(&api_file_mutex);
	rc = (*(bridge_api.set_part_owner))(pid, name);
	slurm_mutex_unlock(&api_file_mutex);
	return rc;

}

extern status_t bridge_add_block_user(pm_partition_id_t pid, const char *name)
{
	int rc = CONNECTION_ERROR;
	if(!bridge_init())
		return rc;
	
	slurm_mutex_lock(&api_file_mutex);
	rc = (*(bridge_api.add_part_user))(pid, name);
	slurm_mutex_unlock(&api_file_mutex);
	return rc;

}

extern status_t bridge_remove_block_user(pm_partition_id_t pid, 
					 const char *name)
{
	int rc = CONNECTION_ERROR;
	if(!bridge_init())
		return rc;
	
	slurm_mutex_lock(&api_file_mutex);
	rc = (*(bridge_api.remove_part_user))(pid, name);
	slurm_mutex_unlock(&api_file_mutex);
	return rc;

}

extern status_t bridge_remove_block(pm_partition_id_t pid)
{
	int rc = CONNECTION_ERROR;
	if(!bridge_init())
		return rc;
	
	slurm_mutex_lock(&api_file_mutex);
	rc = (*(bridge_api.remove_partition))(pid);
	slurm_mutex_unlock(&api_file_mutex);
	return rc;

}

extern status_t bridge_get_blocks(rm_partition_state_flag_t flag, 
				  rm_partition_list_t **part_list)
{
	int rc = CONNECTION_ERROR;
	if(!bridge_init())
		return rc;
	
	slurm_mutex_lock(&api_file_mutex);
	rc = (*(bridge_api.get_partitions))(flag, part_list);
	slurm_mutex_unlock(&api_file_mutex);
	return rc;

}

extern status_t bridge_get_blocks_info(rm_partition_state_flag_t flag, 
				       rm_partition_list_t **part_list)
{
	int rc = CONNECTION_ERROR;
	if(!bridge_init())
		return rc;
	
	slurm_mutex_lock(&api_file_mutex);
	rc = (*(bridge_api.get_partitions_info))(flag, part_list);
	slurm_mutex_unlock(&api_file_mutex);
	return rc;

}

extern status_t bridge_get_job(db_job_id_t dbJobId, rm_job_t **job)
{
	int rc = CONNECTION_ERROR;
	if(!bridge_init())
		return rc;
	
	slurm_mutex_lock(&api_file_mutex);
	rc = (*(bridge_api.get_job))(dbJobId, job);
	slurm_mutex_unlock(&api_file_mutex);
	return rc;

}

extern status_t bridge_get_jobs(rm_job_state_flag_t flag, rm_job_list_t **jobs)
{
	int rc = CONNECTION_ERROR;
	if(!bridge_init())
		return rc;
	
	slurm_mutex_lock(&api_file_mutex);
	rc = (*(bridge_api.get_jobs))(flag, jobs);
	slurm_mutex_unlock(&api_file_mutex);
	return rc;

}

extern status_t bridge_remove_job(db_job_id_t jid)
{
	int rc = CONNECTION_ERROR;
	if(!bridge_init())
		return rc;
	
	slurm_mutex_lock(&api_file_mutex);
	rc = (*(bridge_api.remove_job))(jid);
	slurm_mutex_unlock(&api_file_mutex);
	return rc;

}

extern status_t bridge_get_nodecards(rm_bp_id_t bpid, 
				     rm_nodecard_list_t **nc_list)
{
	int rc = CONNECTION_ERROR;
	if(!bridge_init())
		return rc;
	
	slurm_mutex_lock(&api_file_mutex);
	rc = (*(bridge_api.get_nodecards))(bpid, nc_list);
	slurm_mutex_unlock(&api_file_mutex);
	return rc;

}

extern status_t bridge_new_block(rm_partition_t **partition)
{
	int rc = CONNECTION_ERROR;
	if(!bridge_init())
		return rc;
	
	slurm_mutex_lock(&api_file_mutex);
	rc = (*(bridge_api.new_partition))(partition);
	slurm_mutex_unlock(&api_file_mutex);
	return rc;

}

extern status_t bridge_free_block(rm_partition_t *partition)
{
	int rc = CONNECTION_ERROR;
	if(!bridge_init())
		return rc;
	
	slurm_mutex_lock(&api_file_mutex);
	rc = (*(bridge_api.free_partition))(partition);
	slurm_mutex_unlock(&api_file_mutex);
	return rc;

}

extern status_t bridge_free_job(rm_job_t *job)
{
	int rc = CONNECTION_ERROR;
	if(!bridge_init())
		return rc;
	
	slurm_mutex_lock(&api_file_mutex);
	rc = (*(bridge_api.free_job))(job);
	slurm_mutex_unlock(&api_file_mutex);
	return rc;

}

extern status_t bridge_free_bg(my_bluegene_t *bg)
{
	int rc = CONNECTION_ERROR;
	if(!bridge_init())
		return rc;
	
	slurm_mutex_lock(&api_file_mutex);
	rc = (*(bridge_api.free_bg))(bg);
	slurm_mutex_unlock(&api_file_mutex);
	return rc;

}

extern status_t bridge_free_block_list(rm_partition_list_t *part_list)
{
	int rc = CONNECTION_ERROR;
	if(!bridge_init())
		return rc;
	
	slurm_mutex_lock(&api_file_mutex);
	rc = (*(bridge_api.free_partition_list))(part_list);
	slurm_mutex_unlock(&api_file_mutex);
	return rc;

}

extern status_t bridge_free_job_list(rm_job_list_t *job_list)
{
	int rc = CONNECTION_ERROR;
	if(!bridge_init())
		return rc;
	
	slurm_mutex_lock(&api_file_mutex);
	rc = (*(bridge_api.free_job_list))(job_list);
	slurm_mutex_unlock(&api_file_mutex);
	return rc;

}
  
extern status_t bridge_free_nodecard_list(rm_nodecard_list_t *nc_list)
{
	int rc = CONNECTION_ERROR;
	if(!bridge_init())
		return rc;
	
	slurm_mutex_lock(&api_file_mutex);
	rc = (*(bridge_api.free_nodecard_list))(nc_list);
	slurm_mutex_unlock(&api_file_mutex);
	return rc;

}

extern status_t bridge_get_data(rm_element_t* element,
				enum rm_specification field, void *data)
{
	int rc = CONNECTION_ERROR;
	if(!bridge_init())
		return rc;
	
	slurm_mutex_lock(&api_file_mutex);
	rc = (*(bridge_api.get_data))(element, field, data);
	slurm_mutex_unlock(&api_file_mutex);
	return rc;

}

extern status_t bridge_set_data(rm_element_t* element, 
				enum rm_specification field, void *data)
{
	int rc = CONNECTION_ERROR;
	if(!bridge_init())
		return rc;
	
	slurm_mutex_lock(&api_file_mutex);
	rc = (*(bridge_api.set_data))(element, field, data);
	slurm_mutex_unlock(&api_file_mutex);
	return rc;

}

/* all the jm functions */
extern status_t bridge_signal_job(db_job_id_t jid, rm_signal_t sig)
{
	int rc = CONNECTION_ERROR;
	if(!bridge_init())
		return rc;
	
	slurm_mutex_lock(&api_file_mutex);
	rc = (*(bridge_api.signal_job))(jid, sig);
	slurm_mutex_unlock(&api_file_mutex);
	return rc;

}

extern status_t bridge_cancel_job(db_job_id_t jid)
{
	int rc = CONNECTION_ERROR;
	if(!bridge_init())
		return rc;
	
	slurm_mutex_lock(&api_file_mutex);
	rc = (*(bridge_api.cancel_job))(jid);
	slurm_mutex_unlock(&api_file_mutex);
	return rc;

}

/* all the pm functions */
extern status_t bridge_create_block(pm_partition_id_t pid)
{
	int rc = CONNECTION_ERROR;
	if(!bridge_init())
		return rc;
	
	slurm_mutex_lock(&api_file_mutex);
	rc = (*(bridge_api.create_partition))(pid);
	slurm_mutex_unlock(&api_file_mutex);
	return rc;

}

extern status_t bridge_destroy_block(pm_partition_id_t pid)
{
	int rc = CONNECTION_ERROR;
	if(!bridge_init())
		return rc;
	
	slurm_mutex_lock(&api_file_mutex);
	rc = (*(bridge_api.destroy_partition))(pid);
	slurm_mutex_unlock(&api_file_mutex);
	return rc;

}

extern int bridge_set_log_params(char *api_file_name, unsigned int level)
{
	static FILE *fp = NULL;
        FILE *fp2 = NULL;
	int rc = SLURM_SUCCESS;

	if(!bridge_init())
		return SLURM_ERROR;
	
	slurm_mutex_lock(&api_file_mutex);
	if(fp) 
		fp2 = fp;
	
	fp = fopen(api_file_name, "a");
	
	if (fp == NULL) { 
		error("can't open file for bridgeapi.log at %s: %m", 
		      api_file_name);
		rc = SLURM_ERROR;
		goto end_it;
	}

	
	(*(bridge_api.set_log_params))(fp, level);
	if(fp2)
		fclose(fp2);
end_it:
	slurm_mutex_unlock(&api_file_mutex);
	return rc;
}
#endif /* HAVE_BG_FILES */


