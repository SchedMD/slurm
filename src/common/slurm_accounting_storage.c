/*****************************************************************************\
 *  slurm_accounting_storage.c - account torage plugin wrapper.
 *
 *  $Id: slurm_accounting_storage.c 10744 2007-01-11 20:09:18Z da $
 *****************************************************************************
 *  Copyright (C) 2002-2007 The Regents of the University of California.
 *  Copyright (C) 2008 Lawrence Livermore National Security.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Danny Aubke <da@llnl.gov>.
 *  UCRL-CODE-226842.
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

#ifdef HAVE_CONFIG_H
#  include "config.h"
#endif

#include <pthread.h>

#include "src/common/list.h"
#include "src/common/slurm_accounting_storage.h"
#include "src/common/plugin.h"
#include "src/common/plugrack.h"
#include "src/common/slurm_protocol_api.h"
#include "src/common/xstring.h"
#include "src/slurmctld/slurmctld.h"
#include "src/sacctmgr/sacctmgr.h"

/*
 * Local data
 */

typedef struct slurm_acct_storage_ops {
	void *(*get_conn)          ();
	int  (*close_conn)         (void *db_conn);
	int  (*add_users)          (void *db_conn,
				    List user_list);
	int  (*add_coord)          (void *db_conn,
				    char *acct,
				    acct_user_cond_t *user_q);
	int  (*add_accts)          (void *db_conn,
				    List acct_list);
	int  (*add_clusters)       (void *db_conn,
				    List cluster_list);
	int  (*add_associations)   (void *db_conn,
				    List association_list);
	int  (*modify_users)       (void *db_conn,
				    acct_user_cond_t *user_q,
				    acct_user_rec_t *user);
	int  (*modify_user_admin_level)(void *db_conn,
					acct_user_cond_t *user_q);
	int  (*modify_accts)       (void *db_conn,
				    acct_account_cond_t *acct_q,
				    acct_account_rec_t *acct);
	int  (*modify_clusters)    (void *db_conn,
				    acct_cluster_cond_t *cluster_q,
				    acct_cluster_rec_t *cluster);
	int  (*modify_associations)(void *db_conn,
				    acct_association_cond_t *assoc_q,
				    acct_association_rec_t *assoc);
	int  (*remove_users)       (void *db_conn,
				    acct_user_cond_t *user_q);
	int  (*remove_coord)       (void *db_conn,
				    char *acct,
				    acct_user_cond_t *user_q);
	int  (*remove_accts)       (void *db_conn,
				    acct_account_cond_t *acct_q);
	int  (*remove_clusters)    (void *db_conn,
				    acct_cluster_cond_t *cluster_q);
	int  (*remove_associations)(void *db_conn,
				    acct_association_cond_t *assoc_q);
	List (*get_users)          (void *db_conn,
				    acct_user_cond_t *user_q);
	List (*get_accts)          (void *db_conn,
				    acct_account_cond_t *acct_q);
	List (*get_clusters)       (void *db_conn,
				    acct_cluster_cond_t *cluster_q);
	List (*get_associations)   (void *db_conn,
				    acct_association_cond_t *assoc_q);
	int  (*get_usage)          (void *db_conn,
				    acct_usage_type_t type,
				    void *acct_assoc,
				    time_t start, 
				    time_t end);
	int (*roll_usage)          (void *db_conn,
				    acct_usage_type_t type,
				    time_t start);
	int  (*node_down)          (void *db_conn,
				    char *cluster,
				    struct node_record *node_ptr,
				    time_t event_time,
				    char *reason);
	int  (*node_up)            (void *db_conn,
				    char *cluster,
				    struct node_record *node_ptr,
				    time_t event_time);
	int  (*cluster_procs)      (void *db_conn,
				    char *cluster,
				    uint32_t procs, time_t event_time);
	int  (*c_get_usage)        (void *db_conn,
				    acct_usage_type_t type,
				    void *cluster_rec, 
				    time_t start, time_t end);
	int  (*job_start)          (void *db_conn,
				    struct job_record *job_ptr);
	int  (*job_complete)       (void *db_conn,
				    struct job_record *job_ptr);
	int  (*step_start)         (void *db_conn,
				    struct step_record *step_ptr);
	int  (*step_complete)      (void *db_conn,
				    struct step_record *step_ptr);
	int  (*job_suspend)        (void *db_conn,
				    struct job_record *job_ptr);
	List (*get_jobs)           (void *db_conn,
				    List selected_steps,
				    List selected_parts,
				    void *params);	
	void (*job_archive)        (void *db_conn,
				    List selected_parts, void *params);	
} slurm_acct_storage_ops_t;

typedef struct slurm_acct_storage_context {
	char	       	*acct_storage_type;
	plugrack_t     	plugin_list;
	plugin_handle_t	cur_plugin;
	int		acct_storage_errno;
	slurm_acct_storage_ops_t ops;
} slurm_acct_storage_context_t;

static slurm_acct_storage_context_t * g_acct_storage_context = NULL;
static pthread_mutex_t		g_acct_storage_context_lock = 
					PTHREAD_MUTEX_INITIALIZER;

/*
 * Local functions
 */
static slurm_acct_storage_ops_t *_acct_storage_get_ops(
	slurm_acct_storage_context_t *c);
static slurm_acct_storage_context_t *_acct_storage_context_create(
	const char *acct_storage_type);
static int _acct_storage_context_destroy(
	slurm_acct_storage_context_t *c);

/*
 * Locate and load the appropriate plugin
 */
static slurm_acct_storage_ops_t * _acct_storage_get_ops(
	slurm_acct_storage_context_t *c)
{
	/*
	 * Must be synchronized with slurm_acct_storage_ops_t above.
	 */
	static const char *syms[] = {
		"acct_storage_p_get_connection",
		"acct_storage_p_close_connection",
		"acct_storage_p_add_users",
		"acct_storage_p_add_coord",
		"acct_storage_p_add_accts",
		"acct_storage_p_add_clusters",
		"acct_storage_p_add_associations",
		"acct_storage_p_modify_users",
		"acct_storage_p_modify_user_admin_level",
		"acct_storage_p_modify_accts",
		"acct_storage_p_modify_clusters",
		"acct_storage_p_modify_associations",
		"acct_storage_p_remove_users",
		"acct_storage_p_remove_coord",
		"acct_storage_p_remove_accts",
		"acct_storage_p_remove_clusters",
		"acct_storage_p_remove_associations",
		"acct_storage_p_get_users",
		"acct_storage_p_get_accts",
		"acct_storage_p_get_clusters",
		"acct_storage_p_get_associations",
		"acct_storage_p_get_usage",
		"acct_storage_p_roll_usage",
		"clusteracct_storage_p_node_down",
		"clusteracct_storage_p_node_up",
		"clusteracct_storage_p_cluster_procs",
		"clusteracct_storage_p_get_usage",
		"jobacct_storage_p_job_start",
		"jobacct_storage_p_job_complete",
		"jobacct_storage_p_step_start",
		"jobacct_storage_p_step_complete",
		"jobacct_storage_p_suspend",
		"jobacct_storage_p_get_jobs",
		"jobacct_storage_p_archive"
	};
	int n_syms = sizeof( syms ) / sizeof( char * );

	/* Get plugin list. */
	if ( c->plugin_list == NULL ) {
		char *plugin_dir;
		c->plugin_list = plugrack_create();
		if ( c->plugin_list == NULL ) {
			error( "cannot create plugin manager" );
			return NULL;
		}
		plugrack_set_major_type( c->plugin_list, "accounting_storage" );
		plugrack_set_paranoia( c->plugin_list,
				       PLUGRACK_PARANOIA_NONE,
				       0 );
		plugin_dir = slurm_get_plugin_dir();
		plugrack_read_dir( c->plugin_list, plugin_dir );
		xfree(plugin_dir);
	}

	c->cur_plugin = plugrack_use_by_type( c->plugin_list,
					      c->acct_storage_type );
	if ( c->cur_plugin == PLUGIN_INVALID_HANDLE ) {
		error( "cannot find accounting_storage plugin for %s", 
			c->acct_storage_type );
		return NULL;
	}

	/* Dereference the API. */
	if ( plugin_get_syms( c->cur_plugin,
			      n_syms,
			      syms,
			      (void **) &c->ops ) < n_syms ) {
		error( "incomplete acct_storage plugin detected" );
		return NULL;
	}

	return &c->ops;
}

/*
 * Create a acct_storage context
 */
static slurm_acct_storage_context_t *_acct_storage_context_create(
	const char *acct_storage_type)
{
	slurm_acct_storage_context_t *c;

	if ( acct_storage_type == NULL ) {
		debug3( "_acct_storage_context_create: no uler type" );
		return NULL;
	}

	c = xmalloc( sizeof( slurm_acct_storage_context_t ) );
	c->acct_storage_type	= xstrdup( acct_storage_type );
	c->plugin_list	= NULL;
	c->cur_plugin	= PLUGIN_INVALID_HANDLE;
	c->acct_storage_errno	= SLURM_SUCCESS;

	return c;
}

/*
 * Destroy a acct_storage context
 */
static int _acct_storage_context_destroy(slurm_acct_storage_context_t *c)
{
	/*
	 * Must check return code here because plugins might still
	 * be loaded and active.
	 */
	if ( c->plugin_list ) {
		if ( plugrack_destroy( c->plugin_list ) != SLURM_SUCCESS ) {
			return SLURM_ERROR;
		}
	}

	xfree( c->acct_storage_type );
	xfree( c );

	return SLURM_SUCCESS;
}

extern void destroy_acct_user_rec(void *object)
{
	acct_user_rec_t *acct_user = (acct_user_rec_t *)object;

	if(acct_user) {
		if(acct_user->coord_accts)
			list_destroy(acct_user->coord_accts);
		xfree(acct_user->name);
		xfree(acct_user->default_acct);
		xfree(acct_user);
	}
}

extern void destroy_acct_account_rec(void *object)
{
	acct_account_rec_t *acct_account =
		(acct_account_rec_t *)object;

	if(acct_account) {
		if(acct_account->coordinators)
			list_destroy(acct_account->coordinators);
		xfree(acct_account->description);
		xfree(acct_account->name);
		xfree(acct_account->organization);
		xfree(acct_account);
	}
}

extern void destroy_acct_coord_rec(void *object)
{
	acct_coord_rec_t *acct_coord =
		(acct_coord_rec_t *)object;

	if(acct_coord) {
		xfree(acct_coord->acct_name);
		xfree(acct_coord);
	}
}

extern void destroy_cluster_accounting_rec(void *object)
{
	cluster_accounting_rec_t *clusteracct_rec =
		(cluster_accounting_rec_t *)object;

	if(clusteracct_rec) {
		xfree(clusteracct_rec);
	}
}

extern void destroy_acct_cluster_rec(void *object)
{
	acct_cluster_rec_t *acct_cluster =
		(acct_cluster_rec_t *)object;

	if(acct_cluster) {
		if(acct_cluster->accounting_list)
			list_destroy(acct_cluster->accounting_list);
		xfree(acct_cluster->backup);
		xfree(acct_cluster->name);
		xfree(acct_cluster->primary);
		xfree(acct_cluster);
	}
}

extern void destroy_acct_accounting_rec(void *object)
{
	acct_accounting_rec_t *acct_accounting =
		(acct_accounting_rec_t *)object;

	if(acct_accounting) {
		xfree(acct_accounting);
	}
}

extern void destroy_acct_association_rec(void *object)
{
	acct_association_rec_t *acct_association = 
		(acct_association_rec_t *)object;

	if(acct_association) {
		if(acct_association->accounting_list)
			list_destroy(acct_association->accounting_list);
		xfree(acct_association->acct);
		xfree(acct_association->cluster);
		xfree(acct_association->partition);
		xfree(acct_association->user);
		xfree(acct_association);
	}
}

extern void destroy_acct_user_cond(void *object)
{
	acct_user_cond_t *acct_user = (acct_user_cond_t *)object;

	if(acct_user) {
		if(acct_user->def_acct_list)
			list_destroy(acct_user->def_acct_list);
		if(acct_user->user_list)
			list_destroy(acct_user->user_list);
		xfree(acct_user);
	}
}

extern void destroy_acct_account_cond(void *object)
{
	acct_account_cond_t *acct_account =
		(acct_account_cond_t *)object;

	if(acct_account) {
		if(acct_account->acct_list)
			list_destroy(acct_account->acct_list);
		if(acct_account->description_list)
			list_destroy(acct_account->description_list);
		if(acct_account->organization_list)
			list_destroy(acct_account->organization_list);
		xfree(acct_account);
	}
}

extern void destroy_acct_cluster_cond(void *object)
{
	acct_cluster_cond_t *acct_cluster =
		(acct_cluster_cond_t *)object;

	if(acct_cluster) {
		if(acct_cluster->cluster_list)
			list_destroy(acct_cluster->cluster_list);
		xfree(acct_cluster);
	}
}

extern void destroy_acct_association_cond(void *object)
{
	acct_association_cond_t *acct_association = 
		(acct_association_cond_t *)object;

	if(acct_association) {
		if(acct_association->acct_list)
			list_destroy(acct_association->acct_list);
		if(acct_association->cluster_list)
			list_destroy(acct_association->cluster_list);
		if(acct_association->id_list)
			list_destroy(acct_association->id_list);
		if(acct_association->partition_list)
			list_destroy(acct_association->partition_list);
		xfree(acct_association->parent_acct);
		if(acct_association->user_list)
			list_destroy(acct_association->user_list);
		xfree(acct_association);
	}
}


/****************************************************************************\
 * Pack and unpack data structures
\****************************************************************************/
extern void pack_acct_user_rec(void *in, Buf buffer)
{
	ListIterator itr = NULL;
	acct_user_rec_t *object = (acct_user_rec_t *)in;
	uint32_t count = 0;
	acct_coord_rec_t *coord = NULL;

	if(!object) {
		pack16(0, buffer);
		pack32(0, buffer);
		packstr("", buffer);
		pack16(0, buffer);
		packstr("", buffer);
		pack32(0, buffer);
		return;
	}
 
	pack16((uint16_t)object->admin_level, buffer);
	if(object->coord_accts)
		count = list_count(object->coord_accts);
	
	pack32(count, buffer);
	if(count) {
		itr = list_iterator_create(object->coord_accts);
		while((coord = list_next(itr))) {
			pack_acct_coord_rec(coord, buffer);
		}
		list_iterator_destroy(itr);
	}
	
	packstr(object->default_acct, buffer);
	pack16((uint16_t)object->expedite, buffer);
	packstr(object->name, buffer);
	pack32(object->uid, buffer);
}

extern int unpack_acct_user_rec(void **object, Buf buffer)
{
	uint32_t uint32_tmp;
	acct_user_rec_t *object_ptr = xmalloc(sizeof(acct_user_rec_t));
	uint32_t count = 0;
	acct_coord_rec_t *coord = NULL;
	int i;

	*object = object_ptr;
	safe_unpack16((uint16_t *)&object_ptr->admin_level, buffer);
	safe_unpack32(&count, buffer);
	object_ptr->coord_accts = list_create(destroy_acct_coord_rec);
	for(i=0; i<count; i++) {
		unpack_acct_coord_rec((void *)&coord, buffer);
		list_append(object_ptr->coord_accts, coord);
	}
	safe_unpackstr_xmalloc(&object_ptr->default_acct, &uint32_tmp, buffer);
	safe_unpack16((uint16_t *)&object_ptr->expedite, buffer);
	safe_unpackstr_xmalloc(&object_ptr->name, &uint32_tmp, buffer);
	safe_unpack32(&object_ptr->uid, buffer);
	return SLURM_SUCCESS;

unpack_error:
	destroy_acct_user_rec(object_ptr);
	*object = NULL;
	return SLURM_ERROR;
}

extern void pack_acct_account_rec(void *in, Buf buffer)
{
	char *coord = NULL;
	ListIterator itr = NULL;
	uint32_t count = 0;
	acct_account_rec_t *object = (acct_account_rec_t *)in;

	if(!object) {
		pack32(0, buffer);
		packstr("", buffer);
		pack16(0, buffer);
		packstr("", buffer);
		packstr("", buffer);
		return;
	}
 
	if(object->coordinators)
		count = list_count(object->coordinators);
	
	pack32(count, buffer);
	if(count) {
		itr = list_iterator_create(object->coordinators);
		while((coord = list_next(itr))) {
			packstr(coord, buffer);
		}
		list_iterator_destroy(itr);
	}

	packstr(object->description, buffer);
	pack16((uint16_t)object->expedite, buffer);
	packstr(object->name, buffer);
	packstr(object->organization, buffer);
}

extern int unpack_acct_account_rec(void **object, Buf buffer)
{
	uint32_t uint32_tmp;
	int i;
	uint32_t count;
	char *coord = NULL;
	acct_account_rec_t *object_ptr = xmalloc(sizeof(acct_account_rec_t));

	*object = object_ptr;

	safe_unpack32(&count, buffer);
	object_ptr->coordinators = list_create(slurm_destroy_char);
	for(i=0; i<count; i++) {
		safe_unpackstr_xmalloc(&coord, &uint32_tmp, buffer);
		list_append(object_ptr->coordinators, coord);
	}
	safe_unpackstr_xmalloc(&object_ptr->description, &uint32_tmp, buffer);
	safe_unpack16((uint16_t *)&object_ptr->expedite, buffer);
	safe_unpackstr_xmalloc(&object_ptr->name, &uint32_tmp, buffer);
	safe_unpackstr_xmalloc(&object_ptr->organization, &uint32_tmp, buffer);

	return SLURM_SUCCESS;

unpack_error:
	destroy_acct_account_rec(object_ptr);
	*object = NULL;
	return SLURM_ERROR;
}

extern void pack_acct_coord_rec(void *in, Buf buffer)
{
	acct_coord_rec_t *object = (acct_coord_rec_t *)in;

	if(!object) {
		packstr("", buffer);
		pack16(0, buffer);
		return;
	}

	packstr(object->acct_name, buffer);
	pack16(object->sub_acct, buffer);
}

extern int unpack_acct_coord_rec(void **object, Buf buffer)
{
	uint32_t uint32_tmp;
	acct_coord_rec_t *object_ptr = xmalloc(sizeof(acct_coord_rec_t));

	*object = object_ptr;
	safe_unpackstr_xmalloc(&object_ptr->acct_name, &uint32_tmp, buffer);
	safe_unpack16(&object_ptr->sub_acct, buffer);

unpack_error:
	destroy_acct_coord_rec(object_ptr);
	*object = NULL;
	return SLURM_ERROR;
}

extern void pack_cluster_accounting_rec(void *in, Buf buffer)
{
	cluster_accounting_rec_t *object = (cluster_accounting_rec_t *)in;
	
	if(!object) {
		pack32(0, buffer);
		pack32(0, buffer);
		pack32(0, buffer);
		pack32(0, buffer);
		pack_time(0, buffer);
		pack32(0, buffer);
		return;
	}

 	pack32(object->alloc_secs, buffer);
	pack32(object->cpu_count, buffer);
	pack32(object->down_secs, buffer);
	pack32(object->idle_secs, buffer);
	pack_time(object->period_start, buffer);
	pack32(object->resv_secs, buffer);
}

extern int unpack_cluster_accounting_rec(void **object, Buf buffer)
{
	cluster_accounting_rec_t *object_ptr =
		xmalloc(sizeof(cluster_accounting_rec_t));
	
	*object = object_ptr;
	safe_unpack32(&object_ptr->alloc_secs, buffer);
	safe_unpack32(&object_ptr->cpu_count, buffer);
	safe_unpack32(&object_ptr->down_secs, buffer);
	safe_unpack32(&object_ptr->idle_secs, buffer);
	safe_unpack_time(&object_ptr->period_start, buffer);
	safe_unpack32(&object_ptr->resv_secs, buffer);
	
	return SLURM_SUCCESS;

unpack_error:
	destroy_cluster_accounting_rec(object_ptr);
	*object = NULL;
	return SLURM_ERROR;
}

extern void pack_acct_cluster_rec(void *in, Buf buffer)
{
	cluster_accounting_rec_t *acct_info = NULL;
	ListIterator itr = NULL;
	uint32_t count = 0;
	acct_cluster_rec_t *object = (acct_cluster_rec_t *)in;

	if(!object) {
		pack32(0, buffer);
		packstr("", buffer);
		packstr("", buffer);
		packstr("", buffer);
		return;
	}
 
	if(object->accounting_list)
		count = list_count(object->accounting_list);

	pack32(count, buffer);

	if(count) {
		itr = list_iterator_create(object->accounting_list);
		while((acct_info = list_next(itr))) {
			pack_cluster_accounting_rec(acct_info, buffer);
		}
		list_iterator_destroy(itr);
	}
	packstr(object->backup, buffer);
	packstr(object->name, buffer);
	packstr(object->primary, buffer);
}

extern int unpack_acct_cluster_rec(void **object, Buf buffer)
{
	uint32_t uint32_tmp;
	int i;
	uint32_t count;
	acct_cluster_rec_t *object_ptr = xmalloc(sizeof(acct_cluster_rec_t));
	cluster_accounting_rec_t *acct_info = NULL;

	*object = object_ptr;

	safe_unpack32(&count, buffer);
	object_ptr->accounting_list =
		list_create(destroy_cluster_accounting_rec);
	for(i=0; i<count; i++) {
		unpack_cluster_accounting_rec((void *)&acct_info, buffer);
		list_append(object_ptr->accounting_list, acct_info);
	}
	safe_unpackstr_xmalloc(&object_ptr->backup, &uint32_tmp, buffer);
	safe_unpackstr_xmalloc(&object_ptr->name, &uint32_tmp, buffer);
	safe_unpackstr_xmalloc(&object_ptr->primary, &uint32_tmp, buffer);

	return SLURM_SUCCESS;

unpack_error:
	destroy_acct_cluster_rec(object_ptr);
	*object = NULL;
	return SLURM_ERROR;
}

extern void pack_acct_accounting_rec(void *in, Buf buffer)
{
	acct_accounting_rec_t *object = (acct_accounting_rec_t *)in;
	
	if(!object) {
		pack_time(0, buffer);
		pack32(0, buffer);
		return;
	}

	pack_time(object->period_start, buffer);
	pack32(object->alloc_secs, buffer);
}

extern int unpack_acct_accounting_rec(void **object, Buf buffer)
{
	acct_accounting_rec_t *object_ptr =
		xmalloc(sizeof(acct_accounting_rec_t));
	
	*object = object_ptr;
	safe_unpack_time(&object_ptr->period_start, buffer);
	safe_unpack32(&object_ptr->alloc_secs, buffer);

	return SLURM_SUCCESS;

unpack_error:
	destroy_acct_accounting_rec(object_ptr);
	*object = NULL;
	return SLURM_ERROR;
}

extern void pack_acct_association_rec(void *in, Buf buffer)
{
	acct_accounting_rec_t *acct_info = NULL;
	ListIterator itr = NULL;
	uint32_t count = 0;
	acct_association_rec_t *object = (acct_association_rec_t *)in;	
	
	if(!object) {
		pack32(0, buffer);
		packstr("", buffer);
		packstr("", buffer);
		pack32(0, buffer);
		pack32(0, buffer);
		pack32(0, buffer);
		pack32(0, buffer);
		pack32(0, buffer);
		pack32(0, buffer);
		pack32(0, buffer);
		pack32(0, buffer);
		packstr("", buffer);
		pack32(0, buffer);
		pack32(0, buffer);
		pack32(0, buffer);
		packstr("", buffer);
		return;
	}
 
	if(object->accounting_list)
		count = list_count(object->accounting_list);

	pack32(count, buffer);

	if(count) {
		itr = list_iterator_create(object->accounting_list);
		while((acct_info = list_next(itr))) {
			pack_acct_accounting_rec(acct_info, buffer);
		}
		list_iterator_destroy(itr);
	}

	packstr(object->acct, buffer);
	packstr(object->cluster, buffer);
	pack32(object->fairshare, buffer);
	pack32(object->id, buffer);
	pack32(object->lft, buffer);
	pack32(object->max_cpu_secs_per_job, buffer);
	pack32(object->max_jobs, buffer);
	pack32(object->max_nodes_per_job, buffer);
	pack32(object->max_wall_duration_per_job, buffer);
	pack32(object->parent, buffer);
	packstr(object->parent_acct, buffer);
	pack32(object->parent, buffer);
	pack32(object->rgt, buffer);
	pack32(object->uid, buffer);
	packstr(object->user, buffer);	
}

extern int unpack_acct_association_rec(void **object, Buf buffer)
{
	uint32_t uint32_tmp;
	int i;
	uint32_t count;
	acct_association_rec_t *object_ptr = 
		xmalloc(sizeof(acct_association_rec_t));
	acct_accounting_rec_t *acct_info = NULL;

	*object = object_ptr;

	safe_unpack32(&count, buffer);
	object_ptr->accounting_list =
		list_create(destroy_acct_accounting_rec);
	for(i=0; i<count; i++) {
		unpack_acct_accounting_rec((void **)&acct_info, buffer);
		list_append(object_ptr->accounting_list, acct_info);
	}
	safe_unpackstr_xmalloc(&object_ptr->acct, &uint32_tmp, buffer);
	safe_unpackstr_xmalloc(&object_ptr->cluster, &uint32_tmp, buffer);
	safe_unpack32(&object_ptr->fairshare, buffer);
	safe_unpack32(&object_ptr->id, buffer);
	safe_unpack32(&object_ptr->lft, buffer);
	safe_unpack32(&object_ptr->max_cpu_secs_per_job, buffer);
	safe_unpack32(&object_ptr->max_jobs, buffer);
	safe_unpack32(&object_ptr->max_nodes_per_job, buffer);
	safe_unpack32(&object_ptr->max_wall_duration_per_job, buffer);
	safe_unpack32(&object_ptr->parent, buffer);
	safe_unpackstr_xmalloc(&object_ptr->parent_acct, &uint32_tmp, buffer);
	safe_unpack32(&object_ptr->parent, buffer);
	safe_unpack32(&object_ptr->rgt, buffer);
	safe_unpack32(&object_ptr->uid, buffer);
	safe_unpackstr_xmalloc(&object_ptr->user, &uint32_tmp, buffer);	

	return SLURM_SUCCESS;

unpack_error:
	destroy_acct_association_rec(object_ptr);
	*object = NULL;
	return SLURM_ERROR;
}

extern void pack_acct_user_cond(void *in, Buf buffer)
{
	char *info = NULL;
	ListIterator itr = NULL;
	acct_user_cond_t *object = (acct_user_cond_t *)in;
	uint32_t count = 0;

	if(!object) {
		pack16(0, buffer);
		pack32(0, buffer);
		pack16(0, buffer);
		pack32(0, buffer);
		return;
	}
 
	pack16((uint16_t)object->admin_level, buffer);

	if(object->def_acct_list)
		count = list_count(object->def_acct_list);

	pack32(count, buffer);

	if(count) {
		itr = list_iterator_create(object->def_acct_list);
		while((info = list_next(itr))) {
			packstr(info, buffer);
		}
		list_iterator_destroy(itr);
	}
	pack16((uint16_t)object->expedite, buffer);

	count = 0;
	if(object->user_list)
		count = list_count(object->user_list);

	pack32(count, buffer);

	if(count) {
		itr = list_iterator_create(object->user_list);
		while((info = list_next(itr))) {
			packstr(info, buffer);
		}
		list_iterator_destroy(itr);
	}

}

extern int unpack_acct_user_cond(void **object, Buf buffer)
{
	uint32_t uint32_tmp;
	int i;
	uint32_t count;
	acct_user_cond_t *object_ptr = xmalloc(sizeof(acct_user_cond_t));
	char *info = NULL;

	*object = object_ptr;

	safe_unpack16((uint16_t *)&object_ptr->admin_level, buffer);
	safe_unpack32(&count, buffer);
	object_ptr->def_acct_list = list_create(slurm_destroy_char);
	for(i=0; i<count; i++) {
		safe_unpackstr_xmalloc(&info, &uint32_tmp, buffer);
		list_append(object_ptr->def_acct_list, info);
	}
	safe_unpack16((uint16_t *)&object_ptr->expedite, buffer);
	safe_unpack32(&count, buffer);
	object_ptr->user_list = list_create(slurm_destroy_char);
	for(i=0; i<count; i++) {
		safe_unpackstr_xmalloc(&info, &uint32_tmp, buffer);
		list_append(object_ptr->user_list, info);
	}

	return SLURM_SUCCESS;

unpack_error:
	destroy_acct_user_cond(object_ptr);
	*object = NULL;
	return SLURM_ERROR;
}

extern void pack_acct_account_cond(void *in, Buf buffer)
{
	char *info = NULL;
	ListIterator itr = NULL;
	acct_account_cond_t *object = (acct_account_cond_t *)in;
	uint32_t count = 0;

	if(!object) {
		pack32(0, buffer);
		pack32(0, buffer);
		pack16(0, buffer);
		pack32(0, buffer);
		return;
	}
 	if(object->acct_list)
		count = list_count(object->acct_list);

	pack32(count, buffer);

	if(count) {
		itr = list_iterator_create(object->acct_list);
		while((info = list_next(itr))) {
			packstr(info, buffer);
		}
		list_iterator_destroy(itr);
	}

	count = 0;
	if(object->description_list)
		count = list_count(object->description_list);

	pack32(count, buffer);

	if(count) {
		itr = list_iterator_create(object->description_list);
		while((info = list_next(itr))) {
			packstr(info, buffer);
		}
		list_iterator_destroy(itr);
	}

	pack16((uint16_t)object->expedite, buffer);

	count = 0;
	if(object->organization_list)
		count = list_count(object->organization_list);

	pack32(count, buffer);

	if(count) {
		itr = list_iterator_create(object->organization_list);
		while((info = list_next(itr))) {
			packstr(info, buffer);
		}
		list_iterator_destroy(itr);
	}
}

extern int unpack_acct_account_cond(void **object, Buf buffer)
{
	uint32_t uint32_tmp;
	int i;
	uint32_t count;
	acct_account_cond_t *object_ptr = xmalloc(sizeof(acct_account_cond_t));
	char *info = NULL;

	*object = object_ptr;
	safe_unpack32(&count, buffer);
	object_ptr->acct_list = list_create(slurm_destroy_char);
	for(i=0; i<count; i++) {
		safe_unpackstr_xmalloc(&info, &uint32_tmp, buffer);
		list_append(object_ptr->acct_list, info);
	}
	safe_unpack32(&count, buffer);
	object_ptr->description_list = list_create(slurm_destroy_char);
	for(i=0; i<count; i++) {
		safe_unpackstr_xmalloc(&info, &uint32_tmp, buffer);
		list_append(object_ptr->description_list, info);
	}
	safe_unpack16((uint16_t *)&object_ptr->expedite, buffer);
	safe_unpack32(&count, buffer);
	object_ptr->organization_list = list_create(slurm_destroy_char);
	for(i=0; i<count; i++) {
		safe_unpackstr_xmalloc(&info, &uint32_tmp, buffer);
		list_append(object_ptr->organization_list, info);
	}

	return SLURM_SUCCESS;

unpack_error:
	destroy_acct_account_cond(object_ptr);
	*object = NULL;
	return SLURM_ERROR;
}

extern void pack_acct_cluster_cond(void *in, Buf buffer)
{
	char *info = NULL;
	ListIterator itr = NULL;
	acct_cluster_cond_t *object = (acct_cluster_cond_t *)in;
	uint32_t count = 0;

	if(!object) {
		packstr("", buffer);
		pack32(0, buffer);
		return;
	}
 
	if(object->cluster_list)
		count = list_count(object->cluster_list);
	
	pack32(count, buffer);
	if(count) {
		itr = list_iterator_create(object->cluster_list);
		while((info = list_next(itr))) {
			packstr(info, buffer);
		}
		list_iterator_destroy(itr);
	}
}

extern int unpack_acct_cluster_cond(void **object, Buf buffer)
{
	uint32_t uint32_tmp;
	int i;
	uint32_t count;
	acct_cluster_cond_t *object_ptr = xmalloc(sizeof(acct_cluster_cond_t));
	char *info = NULL;

	*object = object_ptr;
	safe_unpack32(&count, buffer);
	object_ptr->cluster_list = list_create(slurm_destroy_char);
	for(i=0; i<count; i++) {
		safe_unpackstr_xmalloc(&info, &uint32_tmp, buffer);
		list_append(object_ptr->cluster_list, info);
	}

	return SLURM_SUCCESS;

unpack_error:
	destroy_acct_cluster_cond(object_ptr);
	*object = NULL;
	return SLURM_ERROR;
}

extern void pack_acct_association_cond(void *in, Buf buffer)
{
	char *info = NULL;
	uint32_t count = 0;

	ListIterator itr = NULL;
	acct_association_cond_t *object = (acct_association_cond_t *)in;

	if(!object) {
		pack32(0, buffer);
		pack32(0, buffer);
		pack32(0, buffer);
		pack32(0, buffer);
		pack32(0, buffer);
		packstr("", buffer);
		pack32(0, buffer);
		pack32(0, buffer);
		pack32(0, buffer);
		pack32(0, buffer);
		return;
	}

	if(object->acct_list)
		count = list_count(object->acct_list);
	
	pack32(count, buffer);
	if(count) {
		itr = list_iterator_create(object->acct_list);
		while((info = list_next(itr))) {
			packstr(info, buffer);
		}
		list_iterator_destroy(itr);
	}
	count = 0;

	if(object->cluster_list)
		count = list_count(object->cluster_list);
	
	pack32(count, buffer);
	if(count) {
		itr = list_iterator_create(object->cluster_list);
		while((info = list_next(itr))) {
			packstr(info, buffer);
		}
		list_iterator_destroy(itr);
	}
	count = 0;

	if(object->id_list)
		count = list_count(object->id_list);
	
	pack32(count, buffer);
	if(count) {
		itr = list_iterator_create(object->id_list);
		while((info = list_next(itr))) {
			packstr(info, buffer);
		}
	}
	count = 0;

	pack32(object->lft, buffer);
	
	if(object->partition_list)
		count = list_count(object->partition_list);
	
	pack32(count, buffer);
	if(count) {
		itr = list_iterator_create(object->partition_list);
		while((info = list_next(itr))) {
			packstr(info, buffer);
		}
		list_iterator_destroy(itr);
	}
	count = 0;

	packstr(object->parent_acct, buffer);

	pack32(object->parent, buffer);
	pack32(object->rgt, buffer);

	pack32(object->lft, buffer);
	
	if(object->user_list)
		count = list_count(object->user_list);
	
	pack32(count, buffer);
	if(count) {
		itr = list_iterator_create(object->user_list);
		while((info = list_next(itr))) {
			packstr(info, buffer);
		}
		list_iterator_destroy(itr);
	}
}

extern int unpack_acct_association_cond(void **object, Buf buffer)
{
	uint32_t uint32_tmp;
	int i;
	uint32_t count;
	acct_association_cond_t *object_ptr =
		xmalloc(sizeof(acct_association_cond_t));
	char *info = NULL;

	*object = object_ptr;
	safe_unpack32(&count, buffer);
	object_ptr->acct_list = list_create(slurm_destroy_char);
	for(i=0; i<count; i++) {
		safe_unpackstr_xmalloc(&info, &uint32_tmp, buffer);
		list_append(object_ptr->acct_list, info);
	}

	safe_unpack32(&count, buffer);
	object_ptr->cluster_list = list_create(slurm_destroy_char);
	for(i=0; i<count; i++) {
		safe_unpackstr_xmalloc(&info, &uint32_tmp, buffer);
		list_append(object_ptr->cluster_list, info);
	}

	safe_unpack32(&count, buffer);
	object_ptr->id_list = list_create(slurm_destroy_char);
	for(i=0; i<count; i++) {
		safe_unpackstr_xmalloc(&info, &uint32_tmp, buffer);
		list_append(object_ptr->id_list, info);
	}
	
	safe_unpack32(&object_ptr->lft, buffer);

	safe_unpack32(&count, buffer);
	object_ptr->partition_list = list_create(slurm_destroy_char);
	for(i=0; i<count; i++) {
		safe_unpackstr_xmalloc(&info, &uint32_tmp, buffer);
		list_append(object_ptr->partition_list, info);
	}

	safe_unpackstr_xmalloc(&object_ptr->parent_acct, &uint32_tmp, buffer);

	safe_unpack32(&object_ptr->parent, buffer);
	safe_unpack32(&object_ptr->rgt, buffer);

	safe_unpack32(&count, buffer);
	object_ptr->user_list = list_create(slurm_destroy_char);
	for(i=0; i<count; i++) {
		safe_unpackstr_xmalloc(&info, &uint32_tmp, buffer);
		list_append(object_ptr->user_list, info);
	}

	return SLURM_SUCCESS;

unpack_error:
	destroy_acct_association_cond(object_ptr);
	*object = NULL;
	return SLURM_ERROR;
}

extern char *acct_expedite_str(acct_expedite_level_t level)
{
	switch(level) {
	case ACCT_EXPEDITE_NOTSET:
		return "Not Set";
		break;
	case ACCT_EXPEDITE_NORMAL:
		return "Normal";
		break;
	case ACCT_EXPEDITE_EXPEDITE:
		return "Expedite";
		break;
	case ACCT_EXPEDITE_STANDBY:
		return "Standby";
		break;
	case ACCT_EXPEDITE_EXEMPT:
		return "Exempt";
		break;
	default:
		return "Unknown";
		break;
	}
	return "Unknown";
}

extern acct_expedite_level_t str_2_acct_expedite(char *level)
{
	if(!level) {
		return ACCT_EXPEDITE_NOTSET;
	} else if(!strncasecmp(level, "Normal", 1)) {
		return ACCT_EXPEDITE_NORMAL;
	} else if(!strncasecmp(level, "Expedite", 3)) {
		return ACCT_EXPEDITE_EXPEDITE;
	} else if(!strncasecmp(level, "Standby", 1)) {
		return ACCT_EXPEDITE_STANDBY;
	} else if(!strncasecmp(level, "Exempt", 3)) {
		return ACCT_EXPEDITE_EXEMPT;
	} else {
		return ACCT_EXPEDITE_NOTSET;		
	}
}

extern char *acct_admin_level_str(acct_admin_level_t level)
{
	switch(level) {
	case ACCT_ADMIN_NOTSET:
		return "Not Set";
		break;
	case ACCT_ADMIN_NONE:
		return "None";
		break;
	case ACCT_ADMIN_OPERATOR:
		return "Operator";
		break;
	case ACCT_ADMIN_SUPER_USER:
		return "Administrator";
		break;
	default:
		return "Unknown";
		break;
	}
	return "Unknown";
}

extern acct_admin_level_t str_2_acct_admin_level(char *level)
{
	if(!level) {
		return ACCT_ADMIN_NOTSET;
	} else if(!strncasecmp(level, "None", 1)) {
		return ACCT_ADMIN_NONE;
	} else if(!strncasecmp(level, "Operator", 1)) {
		return ACCT_ADMIN_OPERATOR;
	} else if(!strncasecmp(level, "SuperUser", 1) 
		  || !strncasecmp(level, "Admin", 1)) {
		return ACCT_ADMIN_SUPER_USER;
	} else {
		return ACCT_ADMIN_NOTSET;		
	}	
}

/*
 * Initialize context for acct_storage plugin
 */
extern int slurm_acct_storage_init(char *loc)
{
	int retval = SLURM_SUCCESS;
	char *acct_storage_type = NULL;
	
	slurm_mutex_lock( &g_acct_storage_context_lock );

	if ( g_acct_storage_context )
		goto done;
	if(loc)
		slurm_set_accounting_storage_loc(loc);
	
	acct_storage_type = slurm_get_accounting_storage_type();
	
	g_acct_storage_context = _acct_storage_context_create(
		acct_storage_type);
	if ( g_acct_storage_context == NULL ) {
		error( "cannot create acct_storage context for %s",
		       acct_storage_type );
		retval = SLURM_ERROR;
		goto done;
	}

	if ( _acct_storage_get_ops( g_acct_storage_context ) == NULL ) {
		error( "cannot resolve acct_storage plugin operations" );
		_acct_storage_context_destroy( g_acct_storage_context );
		g_acct_storage_context = NULL;
		retval = SLURM_ERROR;
	}

done:
	slurm_mutex_unlock( &g_acct_storage_context_lock );
	xfree(acct_storage_type);
	return retval;
}

extern int slurm_acct_storage_fini(void)
{
	int rc;

	if (!g_acct_storage_context)
		return SLURM_SUCCESS;

//	(*(g_acct_storage_context->ops.acct_storage_fini))();
	rc = _acct_storage_context_destroy( g_acct_storage_context );
	g_acct_storage_context = NULL;
	return rc;
}

extern void *acct_storage_g_get_connection()
{
	if (slurm_acct_storage_init(NULL) < 0)
		return NULL;
	return (*(g_acct_storage_context->ops.get_conn))();
}

extern int acct_storage_g_close_connection(void *db_conn)
{
	if (slurm_acct_storage_init(NULL) < 0)
		return SLURM_ERROR;
	return (*(g_acct_storage_context->ops.close_conn))(db_conn);

}

extern int acct_storage_g_add_users(void *db_conn,
				    List user_list)
{
	if (slurm_acct_storage_init(NULL) < 0)
		return SLURM_ERROR;
	return (*(g_acct_storage_context->ops.add_users))(db_conn, user_list);
}

extern int acct_storage_g_add_coord(void *db_conn,
				    char *acct, acct_user_cond_t *user_q)
{
	if (slurm_acct_storage_init(NULL) < 0)
		return SLURM_ERROR;
	return (*(g_acct_storage_context->ops.add_coord))
		(db_conn, acct, user_q);
}

extern int acct_storage_g_add_accounts(void *db_conn,
				       List acct_list)
{
	if (slurm_acct_storage_init(NULL) < 0)
		return SLURM_ERROR;
	return (*(g_acct_storage_context->ops.add_accts))(db_conn, acct_list);
}

extern int acct_storage_g_add_clusters(void *db_conn,
				       List cluster_list)
{
	if (slurm_acct_storage_init(NULL) < 0)
		return SLURM_ERROR;
	return (*(g_acct_storage_context->ops.add_clusters))(db_conn, cluster_list);
}

extern int acct_storage_g_add_associations(void *db_conn,
					   List association_list)
{
	if (slurm_acct_storage_init(NULL) < 0)
		return SLURM_ERROR;
	return (*(g_acct_storage_context->ops.add_associations))
		(db_conn, association_list);
}

extern int acct_storage_g_modify_users(void *db_conn,
				       acct_user_cond_t *user_q,
				       acct_user_rec_t *user)
{
	if (slurm_acct_storage_init(NULL) < 0)
		return SLURM_ERROR;
	return (*(g_acct_storage_context->ops.modify_users))
		(db_conn, user_q, user);
}

extern int acct_storage_g_modify_user_admin_level(void *db_conn,
						  acct_user_cond_t *user_q)
{
	if (slurm_acct_storage_init(NULL) < 0)
		return SLURM_ERROR;
	return (*(g_acct_storage_context->ops.modify_user_admin_level))
		(db_conn, user_q);
}

extern int acct_storage_g_modify_accounts(void *db_conn,
					  acct_account_cond_t *acct_q,
					  acct_account_rec_t *acct)
{
	if (slurm_acct_storage_init(NULL) < 0)
		return SLURM_ERROR;
	return (*(g_acct_storage_context->ops.modify_accts))
		(db_conn, acct_q, acct);
}

extern int acct_storage_g_modify_clusters(void *db_conn,
					  acct_cluster_cond_t *cluster_q,
					  acct_cluster_rec_t *cluster)
{
	if (slurm_acct_storage_init(NULL) < 0)
		return SLURM_ERROR;
	return (*(g_acct_storage_context->ops.modify_clusters))
		(db_conn, cluster_q, cluster);
}

extern int acct_storage_g_modify_associations(void *db_conn,
					      acct_association_cond_t *assoc_q,
					      acct_association_rec_t *assoc)
{
	if (slurm_acct_storage_init(NULL) < 0)
		return SLURM_ERROR;
	return (*(g_acct_storage_context->ops.modify_associations))
		(db_conn, assoc_q, assoc);
}

extern int acct_storage_g_remove_users(void *db_conn,
				       acct_user_cond_t *user_q)
{
	if (slurm_acct_storage_init(NULL) < 0)
		return SLURM_ERROR;
	return (*(g_acct_storage_context->ops.remove_users))(db_conn, user_q);
}

extern int acct_storage_g_remove_coord(void *db_conn,
				       char *acct, acct_user_cond_t *user_q)
{
	if (slurm_acct_storage_init(NULL) < 0)
		return SLURM_ERROR;
	return (*(g_acct_storage_context->ops.remove_coord))
		(db_conn, acct, user_q);
}

extern int acct_storage_g_remove_accounts(void *db_conn,
					  acct_account_cond_t *acct_q)
{
	if (slurm_acct_storage_init(NULL) < 0)
		return SLURM_ERROR;
	return (*(g_acct_storage_context->ops.remove_accts))
		(db_conn, acct_q);
}

extern int acct_storage_g_remove_clusters(void *db_conn,
					  acct_cluster_cond_t *cluster_q)
{
	if (slurm_acct_storage_init(NULL) < 0)
		return SLURM_ERROR;
	return (*(g_acct_storage_context->ops.remove_clusters))
		(db_conn, cluster_q);
}

extern int acct_storage_g_remove_associations(void *db_conn,
					      acct_association_cond_t *assoc_q)
{
	if (slurm_acct_storage_init(NULL) < 0)
		return SLURM_ERROR;
	return (*(g_acct_storage_context->ops.remove_associations))
		(db_conn, assoc_q);
}

extern List acct_storage_g_get_users(void *db_conn,
				     acct_user_cond_t *user_q)
{
	if (slurm_acct_storage_init(NULL) < 0)
		return NULL;
	return (*(g_acct_storage_context->ops.get_users))(db_conn, user_q);
}

extern List acct_storage_g_get_accounts(void *db_conn,
					acct_account_cond_t *acct_q)
{
	if (slurm_acct_storage_init(NULL) < 0)
		return NULL;
	return (*(g_acct_storage_context->ops.get_accts))
		(db_conn, acct_q);
}

extern List acct_storage_g_get_clusters(void *db_conn,
					acct_cluster_cond_t *cluster_q)
{
	if (slurm_acct_storage_init(NULL) < 0)
		return NULL;
	return (*(g_acct_storage_context->ops.get_clusters))
		(db_conn, cluster_q);
}

extern List acct_storage_g_get_associations(void *db_conn,
					    acct_association_cond_t *assoc_q)
{
	if (slurm_acct_storage_init(NULL) < 0)
		return NULL;
	return (*(g_acct_storage_context->ops.get_associations))
		(db_conn, assoc_q);
}

extern int acct_storage_g_get_usage(void *db_conn, acct_usage_type_t type,
				    void *acct_assoc, time_t start, time_t end)
{
	if (slurm_acct_storage_init(NULL) < 0)
		return SLURM_ERROR;
	return (*(g_acct_storage_context->ops.get_usage))
		(db_conn, type, acct_assoc, start, end);
}

extern int acct_storage_g_roll_usage(void *db_conn, acct_usage_type_t type,
				     time_t start)
{
	if (slurm_acct_storage_init(NULL) < 0)
		return SLURM_ERROR;
	return (*(g_acct_storage_context->ops.roll_usage))
		(db_conn, type, start);
}

extern int clusteracct_storage_g_node_down(void *db_conn,
					   char *cluster,
					   struct node_record *node_ptr,
					   time_t event_time,
					   char *reason)
{
	if (slurm_acct_storage_init(NULL) < 0)
		return SLURM_ERROR;
 	return (*(g_acct_storage_context->ops.node_down))
		(db_conn, cluster, node_ptr, event_time, reason);
}

extern int clusteracct_storage_g_node_up(void *db_conn,
					 char *cluster,
					 struct node_record *node_ptr,
					 time_t event_time)
{
	if (slurm_acct_storage_init(NULL) < 0)
		return SLURM_ERROR;
 	return (*(g_acct_storage_context->ops.node_up))
		(db_conn, cluster, node_ptr, event_time);
}


extern int clusteracct_storage_g_cluster_procs(void *db_conn,
					       char *cluster,
					       uint32_t procs,
					       time_t event_time)
{
	if (slurm_acct_storage_init(NULL) < 0)
		return SLURM_ERROR;
 	return (*(g_acct_storage_context->ops.cluster_procs))
		(db_conn, cluster, procs, event_time);
}


extern int clusteracct_storage_g_get_usage(
	void *db_conn, acct_usage_type_t type, void *cluster_rec,
	time_t start, time_t end)
{
	if (slurm_acct_storage_init(NULL) < 0)
		return SLURM_ERROR;
	return (*(g_acct_storage_context->ops.c_get_usage))
		(db_conn, type, cluster_rec, start, end);
}

/* 
 * load into the storage the start of a job
 */
extern int jobacct_storage_g_job_start (void *db_conn,
					struct job_record *job_ptr) 
{
	if (slurm_acct_storage_init(NULL) < 0)
		return SLURM_ERROR;
	return (*(g_acct_storage_context->ops.job_start))(db_conn, job_ptr);
}

/* 
 * load into the storage the end of a job
 */
extern int jobacct_storage_g_job_complete  (void *db_conn,
					    struct job_record *job_ptr)
{
	if (slurm_acct_storage_init(NULL) < 0)
		return SLURM_ERROR;
	return (*(g_acct_storage_context->ops.job_complete))(db_conn, job_ptr);
}

/* 
 * load into the storage the start of a job step
 */
extern int jobacct_storage_g_step_start (void *db_conn,
					 struct step_record *step_ptr)
{
	if (slurm_acct_storage_init(NULL) < 0)
		return SLURM_ERROR;
	return (*(g_acct_storage_context->ops.step_start))(db_conn, step_ptr);
}

/* 
 * load into the storage the end of a job step
 */
extern int jobacct_storage_g_step_complete (void *db_conn,
					    struct step_record *step_ptr)
{
	if (slurm_acct_storage_init(NULL) < 0)
		return SLURM_ERROR;
	return (*(g_acct_storage_context->ops.step_complete))(db_conn, step_ptr);
}

/* 
 * load into the storage a suspention of a job
 */
extern int jobacct_storage_g_job_suspend (void *db_conn,
					  struct job_record *job_ptr)
{
	if (slurm_acct_storage_init(NULL) < 0)
		return SLURM_ERROR;
 	return (*(g_acct_storage_context->ops.job_suspend))(db_conn, job_ptr);
}


/* 
 * get info from the storage 
 * returns List of job_rec_t *
 * note List needs to be freed when called
 */
extern List jobacct_storage_g_get_jobs(void *db_conn,
				       List selected_steps,
				       List selected_parts,
				       void *params)
{
	if (slurm_acct_storage_init(NULL) < 0)
		return NULL;
 	return (*(g_acct_storage_context->ops.get_jobs))
		(db_conn, selected_steps, selected_parts, params);
}

/* 
 * expire old info from the storage 
 */
extern void jobacct_storage_g_archive(void *db_conn,
				      List selected_parts, void *params)
{
	if (slurm_acct_storage_init(NULL) < 0)
		return;
 	(*(g_acct_storage_context->ops.job_archive))(db_conn, selected_parts, params);
	return;
}

