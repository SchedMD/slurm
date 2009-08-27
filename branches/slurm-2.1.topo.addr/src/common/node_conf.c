/*****************************************************************************\
 *  node_conf.c - partially manage the node records of slurm
 *                (see src/slurmctld/node_mgr.c for the set of functionalities 
 *                 related to slurmctld usage of nodes)
 *	Note: there is a global node table (node_record_table_ptr), its 
 *	hash table (node_hash_table), time stamp (last_node_update) and 
 *	configuration list (config_list)
 *****************************************************************************
 *  Copyright (C) 2002-2007 The Regents of the University of California.
 *  Copyright (C) 2008-2009 Lawrence Livermore National Security.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Morris Jette <jette1@llnl.gov> et. al.
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

#if HAVE_CONFIG_H
#  include "config.h"
#  if HAVE_INTTYPES_H
#    include <inttypes.h>
#  else
#    if HAVE_STDINT_H
#      include <stdint.h>
#    endif
#  endif			/* HAVE_INTTYPES_H */
#endif

#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

#include "src/common/hostlist.h"
#include "src/common/macros.h"
#include "src/common/node_select.h"
#include "src/common/pack.h"
#include "src/common/parse_time.h"
#include "src/common/read_config.h"
#include "src/common/slurm_accounting_storage.h"
#include "src/common/slurm_topology.h"
#include "src/common/xassert.h"
#include "src/common/xmalloc.h"
#include "src/common/xstring.h"

#define _DEBUG 0

/* Global variables */
List config_list  = NULL;		/* list of config_record entries */
List feature_list = NULL;		/* list of features_record entries */
time_t last_node_update = (time_t) 0;	/* time of last update */
struct node_record *node_record_table_ptr = NULL;	/* node records */
struct node_record **node_hash_table = NULL;	/* node_record hash table */int node_record_count = 0;	/* count in node_record_table_ptr */

static void	_add_config_feature(char *feature, bitstr_t *node_bitmap);
static int	_build_single_nodeline_info(slurm_conf_node_t *node_ptr,
					    struct config_record *config_ptr);
static int	_delete_config_record (void);
#if _DEBUG
static void	_dump_hash (void);
#endif
static struct node_record *_find_alias_node_record (char *name);
static int	_hash_index (char *name);
static void	_list_delete_config (void *config_entry);
static void	_list_delete_feature (void *feature_entry);
static int	_list_find_config (void *config_entry, void *key);
static int	_list_find_feature (void *feature_entry, void *key);



static void _add_config_feature(char *feature, bitstr_t *node_bitmap)
{
	struct features_record *feature_ptr;
	ListIterator feature_iter;
	bool match = false;

	/* If feature already exists in feature_list, just update the bitmap */
	feature_iter = list_iterator_create(feature_list);
	if (feature_iter == NULL)
		fatal("list_iterator_create malloc failure");
	while ((feature_ptr = (struct features_record *) 
			list_next(feature_iter))) {
		if (strcmp(feature, feature_ptr->name))
			continue;
		bit_or(feature_ptr->node_bitmap, node_bitmap);
		match = true;
		break;
	}
	list_iterator_destroy(feature_iter);

	if (!match) {	/* Need to create new feature_list record */
		feature_ptr = xmalloc(sizeof(struct features_record));
		feature_ptr->magic = FEATURE_MAGIC;
		feature_ptr->name = xstrdup(feature);
		feature_ptr->node_bitmap = bit_copy(node_bitmap);
		list_append(feature_list, feature_ptr);
	}
}


/* 
 * _build_single_nodeline_info - From the slurm.conf reader, build table,
 * 	and set values
 * RET 0 if no error, error code otherwise
 * Note: Operates on common variables
 *	default_node_record - default node configuration values
 */
static int _build_single_nodeline_info(slurm_conf_node_t *node_ptr,
				       struct config_record *config_ptr)
{
	int error_code = SLURM_SUCCESS;
	struct node_record *node_rec = NULL;
	hostlist_t alias_list = NULL;
	hostlist_t hostname_list = NULL;
	hostlist_t address_list = NULL;
	char *alias = NULL;
	char *hostname = NULL;
	char *address = NULL;
	int state_val = NODE_STATE_UNKNOWN;

	if (node_ptr->state != NULL) {
		state_val = state_str2int(node_ptr->state);
		if (state_val == NO_VAL)
			goto cleanup;
	}

	if ((alias_list = hostlist_create(node_ptr->nodenames)) == NULL) {
		fatal("Unable to create NodeName list from %s",
		      node_ptr->nodenames);
		error_code = errno;
		goto cleanup;
	}
	if ((hostname_list = hostlist_create(node_ptr->hostnames)) == NULL) {
		fatal("Unable to create NodeHostname list from %s",
		      node_ptr->hostnames);
		error_code = errno;
		goto cleanup;
	}
	if ((address_list = hostlist_create(node_ptr->addresses)) == NULL) {
		fatal("Unable to create NodeAddr list from %s",
		      node_ptr->addresses);
		error_code = errno;
		goto cleanup;
	}

	/* some sanity checks */
#ifdef HAVE_FRONT_END
	if ((hostlist_count(hostname_list) != 1) ||
	    (hostlist_count(address_list)  != 1)) {
		error("Only one hostname and address allowed "
		      "in FRONT_END mode");
		goto cleanup;
	}
	hostname = node_ptr->hostnames;
	address = node_ptr->addresses;
#else
	if (hostlist_count(hostname_list) < hostlist_count(alias_list)) {
		error("At least as many NodeHostname are required "
		      "as NodeName");
		goto cleanup;
	}
	if (hostlist_count(address_list) < hostlist_count(alias_list)) {
		error("At least as many NodeAddr are required as NodeName");
		goto cleanup;
	}
#endif

	/* now build the individual node structures */
	while ((alias = hostlist_shift(alias_list))) {
#ifndef HAVE_FRONT_END
		hostname = hostlist_shift(hostname_list);
		address = hostlist_shift(address_list);
#endif
		/* find_node_record locks this to get the
		 * alias so we need to unlock */
		node_rec = find_node_record(alias);

		if (node_rec == NULL) {
			node_rec = create_node_record(config_ptr, alias);
			if ((state_val != NO_VAL) &&
			    (state_val != NODE_STATE_UNKNOWN))
				node_rec->node_state = state_val;
			node_rec->last_response = (time_t) 0;
			node_rec->comm_name = xstrdup(address);

			node_rec->port = node_ptr->port;
			node_rec->reason = xstrdup(node_ptr->reason);
		} else {
			/* FIXME - maybe should be fatal? */
			error("reconfiguration for node %s, ignoring!", alias);
		}
		free(alias);
#ifndef HAVE_FRONT_END
		free(hostname);
		free(address);
#endif
	}

	/* free allocated storage */
cleanup:
	if (alias_list)
		hostlist_destroy(alias_list);
	if (hostname_list)
		hostlist_destroy(hostname_list);
	if (address_list)
		hostlist_destroy(address_list);
	return error_code;
}

/*
 * _delete_config_record - delete all configuration records
 * RET 0 if no error, errno otherwise
 * global: config_list - list of all configuration records
 */
static int _delete_config_record (void) 
{
	last_node_update = time (NULL);
	(void) list_delete_all (config_list,  &_list_find_config,  NULL);
	(void) list_delete_all (feature_list, &_list_find_feature, NULL);
	return SLURM_SUCCESS;
}


#if _DEBUG
/* 
 * _dump_hash - print the node_hash_table contents, used for debugging
 *	or analysis of hash technique 
 * global: node_record_table_ptr - pointer to global node table
 *         node_hash_table - table of hash indecies
 */
static void _dump_hash (void) 
{
	int i, inx;
	struct node_record *node_ptr;

	if (node_hash_table == NULL)
		return;

	for (i = 0; i < node_record_count; i++) {
		node_ptr = node_hash_table[i];
		while (node_ptr) {
			inx = node_ptr -  node_record_table_ptr;
			debug3("node_hash[%d]:%d", i, inx);
			node_ptr = node_ptr->node_next;
		}
	}
}
#endif

/* 
 * _find_alias_node_record - find a record for node with the alias of
 * the specified name supplied
 * input: name - name to be aliased of the desired node 
 * output: return pointer to node record or NULL if not found
 * global: node_record_table_ptr - pointer to global node table
 *         node_hash_table - table of hash indecies
 */
static struct node_record *_find_alias_node_record (char *name) 
{
	int i;
	char *alias = NULL;
	
	if ((name == NULL) || (name[0] == '\0')) {
		info("_find_alias_node_record: passed NULL name");
		return NULL;
	}
	/* Get the alias we have just to make sure the user isn't
	 * trying to use the real hostname to run on something that has
	 * been aliased.  
	 */
	alias = slurm_conf_get_nodename(name);
	
	if (!alias)
		return NULL;
	
	/* try to find via hash table, if it exists */
	if (node_hash_table) {
		struct node_record *node_ptr;
			
		i = _hash_index (alias);
		node_ptr = node_hash_table[i];
		while (node_ptr) {
			xassert(node_ptr->magic == NODE_MAGIC);
			if (!strcmp(node_ptr->name, alias)) {
				xfree(alias);
				return node_ptr;
			}
			node_ptr = node_ptr->node_next;
		}
		error ("_find_alias_node_record: lookup failure for %s", name);
	} 

	/* revert to sequential search */
	else {
		for (i = 0; i < node_record_count; i++) {
			if (!strcmp (alias, node_record_table_ptr[i].name)) {
				xfree(alias);
				return (&node_record_table_ptr[i]);
			} 
		}
	}

	xfree(alias);
	return (struct node_record *) NULL;
}

/* 
 * _hash_index - return a hash table index for the given node name 
 * IN name = the node's name
 * RET the hash table index
 */
static int _hash_index (char *name) 
{
	int index = 0;
	int j;

	if ((node_record_count == 0) ||
	    (name == NULL))
		return 0;	/* degenerate case */

	/* Multiply each character by its numerical position in the
	 * name string to add a bit of entropy, because host names such
	 * as cluster[0001-1000] can cause excessive index collisions.
	 */
	for (j = 1; *name; name++, j++)
		index += (int)*name * j;
	index %= node_record_count;
	
	return index;
}

/* _list_delete_config - delete an entry from the config list, 
 *	see list.h for documentation */
static void _list_delete_config (void *config_entry) 
{
	struct config_record *config_ptr = (struct config_record *) 
					   config_entry;

	xassert(config_ptr);
	xassert(config_ptr->magic == CONFIG_MAGIC);
	xfree (config_ptr->feature);
	build_config_feature_list(config_ptr);
	xfree (config_ptr->nodes);
	FREE_NULL_BITMAP (config_ptr->node_bitmap);
	xfree (config_ptr);
}

/* _list_delete_feature - delete an entry from the feature list, 
 *	see list.h for documentation */
static void _list_delete_feature (void *feature_entry) 
{
	struct features_record *feature_ptr = (struct features_record *) 
					     feature_entry;

	xassert(feature_ptr);
	xassert(feature_ptr->magic == FEATURE_MAGIC);
	xfree (feature_ptr->name);
	FREE_NULL_BITMAP (feature_ptr->node_bitmap);
	xfree (feature_ptr);
}

/* 
 * _list_find_config - find an entry in the config list, see list.h for   
 *	documentation 
 * IN key - is NULL for all config
 * RET 1 if key == NULL, 0 otherwise
 */
static int _list_find_config (void *config_entry, void *key) 
{
	if (key == NULL)
		return 1;
	return 0;
}

/*
 * bitmap2node_name - given a bitmap, build a list of comma separated node 
 *	names. names may include regular expressions (e.g. "lx[01-10]")
 * IN bitmap - bitmap pointer
 * RET pointer to node list or NULL on error 
 * globals: node_record_table_ptr - pointer to node table
 * NOTE: the caller must xfree the memory at node_list when no longer required
 */
char * bitmap2node_name (bitstr_t *bitmap) 
{
	int i, first, last;
	hostlist_t hl;
	char buf[8192];

	if (bitmap == NULL)
		return xstrdup("");

	first = bit_ffs(bitmap);
	if (first == -1)
		return xstrdup("");

	last  = bit_fls(bitmap);
	hl = hostlist_create("");
	if (hl == NULL)
		fatal("hostlist_create: malloc error");
	for (i = first; i <= last; i++) {
		if (bit_test(bitmap, i) == 0)
			continue;
		hostlist_push(hl, node_record_table_ptr[i].name);
	}
	hostlist_uniq(hl);
	hostlist_ranged_string(hl, sizeof(buf), buf);
	hostlist_destroy(hl);

	return xstrdup(buf);
}

/* 
 * _list_find_feature - find an entry in the feature list, see list.h for   
 *	documentation 
 * IN key - is feature name or NULL for all features
 * RET 1 if found, 0 otherwise
 */
static int _list_find_feature (void *feature_entry, void *key) 
{
	struct features_record *feature_ptr;

	if (key == NULL)
		return 1;

	feature_ptr = (struct features_record *) feature_entry;
	if (strcmp(feature_ptr->name, (char *) key) == 0)
		return 1;
	return 0;
}

/* 
 * _build_all_nodeline_info - get a array of slurm_conf_node_t structures
 *	from the slurm.conf reader, build table, and set values
 * RET 0 if no error, error code otherwise
 */
extern int build_all_nodeline_info (void)
{
	slurm_conf_node_t *node, **ptr_array;
	struct config_record *config_ptr = NULL;
	int count;
	int i, rc, max_rc = SLURM_SUCCESS;

	count = slurm_conf_nodename_array(&ptr_array);
	if (count == 0)
		fatal("No NodeName information available!");

	for (i = 0; i < count; i++) {
		node = ptr_array[i];

		config_ptr = create_config_record();
		config_ptr->nodes = xstrdup(node->nodenames);
		config_ptr->cpus = node->cpus;
		config_ptr->sockets = node->sockets;
		config_ptr->cores = node->cores;
		config_ptr->threads = node->threads;
		config_ptr->real_memory = node->real_memory;
		config_ptr->tmp_disk = node->tmp_disk;
		config_ptr->weight = node->weight;
		if (node->feature)
			config_ptr->feature = xstrdup(node->feature);

		rc = _build_single_nodeline_info(node, config_ptr);
		max_rc = MAX(max_rc, rc);
	}

	return max_rc;
}

/* Given a config_record with it's bitmap already set, update feature_list */
extern void  build_config_feature_list(struct config_record *config_ptr)
{
	struct features_record *feature_ptr;
	ListIterator feature_iter;
	int i, j;
	char *tmp_str, *token, *last = NULL;

	/* Clear these nodes from the feature_list record,
	 * then restore as needed */
	feature_iter = list_iterator_create(feature_list);
	if (feature_iter == NULL)
		fatal("list_inerator_create malloc failure");
	bit_not(config_ptr->node_bitmap);
	while ((feature_ptr = (struct features_record *) 
			list_next(feature_iter))) {
		bit_and(feature_ptr->node_bitmap, config_ptr->node_bitmap);
	}
	list_iterator_destroy(feature_iter);
	bit_not(config_ptr->node_bitmap);

	if (config_ptr->feature) {
		i = strlen(config_ptr->feature) + 1;	/* oversized */
		tmp_str = xmalloc(i);
		/* Remove white space from feature specification */
		for (i=0, j=0; config_ptr->feature[i]; i++) {
			if (!isspace(config_ptr->feature[i]))
				tmp_str[j++] = config_ptr->feature[i];
		}
		if (i != j)
			strcpy(config_ptr->feature, tmp_str);
		token = strtok_r(tmp_str, ",", &last);
		while (token) {
			_add_config_feature(token, config_ptr->node_bitmap);
			token = strtok_r(NULL, ",", &last);
		}
		xfree(tmp_str);
	}
}

/*
 * create_config_record - create a config_record entry and set is values to 
 *	the defaults. each config record corresponds to a line in the  
 *	slurm.conf file and typically describes the configuration of a 
 *	large number of nodes
 * RET pointer to the config_record
 * NOTE: memory allocated will remain in existence until 
 *	_delete_config_record() is called to delete all configuration records
 */
extern struct config_record * create_config_record (void) 
{
	struct config_record *config_ptr;

	last_node_update = time (NULL);
	config_ptr = (struct config_record *)
		     xmalloc (sizeof (struct config_record));

	config_ptr->nodes = NULL;
	config_ptr->node_bitmap = NULL;
	xassert (config_ptr->magic = CONFIG_MAGIC);  /* set value */

	if (list_append(config_list, config_ptr) == NULL)
		fatal ("create_config_record: unable to allocate memory");

	return config_ptr;
}

/* 
 * create_node_record - create a node record and set its values to defaults
 * IN config_ptr - pointer to node's configuration information
 * IN node_name - name of the node
 * RET pointer to the record or NULL if error
 * NOTE: allocates memory at node_record_table_ptr that must be xfreed when  
 *	the global node table is no longer required
 */
extern struct node_record *create_node_record (
			struct config_record *config_ptr, char *node_name) 
{
	struct node_record *node_ptr;
	int old_buffer_size, new_buffer_size;

	last_node_update = time (NULL);
	xassert(config_ptr);
	xassert(node_name); 

	/* round up the buffer size to reduce overhead of xrealloc */
	old_buffer_size = (node_record_count) * sizeof (struct node_record);
	old_buffer_size = 
		((int) ((old_buffer_size / BUF_SIZE) + 1)) * BUF_SIZE;
	new_buffer_size = 
		(node_record_count + 1) * sizeof (struct node_record);
	new_buffer_size = 
		((int) ((new_buffer_size / BUF_SIZE) + 1)) * BUF_SIZE;
	if (!node_record_table_ptr) {
		node_record_table_ptr = 
			(struct node_record *) xmalloc (new_buffer_size);
	} else if (old_buffer_size != new_buffer_size)
		xrealloc (node_record_table_ptr, new_buffer_size);
	node_ptr = node_record_table_ptr + (node_record_count++);
	node_ptr->name = xstrdup(node_name);
	node_ptr->config_ptr = config_ptr;
	/* these values will be overwritten when the node actually registers */
	node_ptr->cpus = config_ptr->cpus;
	node_ptr->sockets = config_ptr->sockets;
	node_ptr->cores = config_ptr->cores;
	node_ptr->threads = config_ptr->threads;
	node_ptr->real_memory = config_ptr->real_memory;
	node_ptr->tmp_disk = config_ptr->tmp_disk;
	node_ptr->select_nodeinfo = select_g_select_nodeinfo_alloc(NO_VAL);
	xassert (node_ptr->magic = NODE_MAGIC)  /* set value */;
	return node_ptr;
}


/* 
 * find_node_record - find a record for node with specified name
 * input: name - name of the desired node 
 * output: return pointer to node record or NULL if not found
 */
extern struct node_record *find_node_record (char *name) 
{
	int i;
	
	if ((name == NULL) || (name[0] == '\0')) {
		info("find_node_record passed NULL name");
		return NULL;
	}
	
	/* try to find via hash table, if it exists */
	if (node_hash_table) {
		struct node_record *node_ptr;
			
		i = _hash_index (name);
		node_ptr = node_hash_table[i];
		while (node_ptr) {
			xassert(node_ptr->magic == NODE_MAGIC);
			if (!strcmp(node_ptr->name, name)) {
				return node_ptr;
			}
			node_ptr = node_ptr->node_next;
		}

		if ((node_record_count == 1) &&
		    (strcmp(node_record_table_ptr[0].name, "localhost") == 0))
			return (&node_record_table_ptr[0]);
	       
		error ("find_node_record: lookup failure for %s", name);
	} 

	/* revert to sequential search */
	else {
		for (i = 0; i < node_record_count; i++) {
			if (!strcmp (name, node_record_table_ptr[i].name)) {
				return (&node_record_table_ptr[i]);
			} 
		}
	}
	
	/* look for the alias node record if the user put this in
	   instead of what slurm sees the node name as */
	return _find_alias_node_record (name);
}


/* 
 * init_node_conf - initialize the node configuration tables and values. 
 *	this should be called before creating any node or configuration 
 *	entries.
 * RET 0 if no error, otherwise an error code
 */
extern int init_node_conf (void) 
{
	last_node_update = time (NULL);
	int i;
	struct node_record *node_ptr;

	node_ptr = node_record_table_ptr;
	for (i=0; i< node_record_count; i++, node_ptr++)
		purge_node_rec(node_ptr);

	node_record_count = 0;
	xfree(node_record_table_ptr);
	xfree(node_hash_table);

	if (config_list)	/* delete defunct configuration entries */
		(void) _delete_config_record ();
	else {
		config_list  = list_create (_list_delete_config);
		feature_list = list_create (_list_delete_feature);
		if ((config_list == NULL) || (feature_list == NULL))
			fatal("list_create malloc failure");
	}

	return SLURM_SUCCESS;
}


/* node_fini2 - free memory associated with node records (except bitmaps) */
extern void node_fini2 (void)
{
	int i;
	struct node_record *node_ptr;

	if (config_list) {
		list_destroy(config_list);
		config_list = NULL;
		list_destroy(feature_list);
		feature_list = NULL;
	}

	node_ptr = node_record_table_ptr;
	for (i=0; i< node_record_count; i++, node_ptr++)
		purge_node_rec(node_ptr);

	xfree(node_record_table_ptr);
	xfree(node_hash_table);
	node_record_count = 0;
}


/*
 * node_name2bitmap - given a node name regular expression, build a bitmap 
 *	representation
 * IN node_names  - list of nodes
 * IN best_effort - if set don't return an error on invalid node name entries 
 * OUT bitmap     - set to bitmap, may not have all bits set on error 
 * RET 0 if no error, otherwise EINVAL
 * NOTE: the caller must bit_free() memory at bitmap when no longer required
 */
extern int node_name2bitmap (char *node_names, bool best_effort, 
			     bitstr_t **bitmap) 
{
	int rc = SLURM_SUCCESS;
	char *this_node_name;
	bitstr_t *my_bitmap;
	hostlist_t host_list;

	my_bitmap = (bitstr_t *) bit_alloc (node_record_count);
	if (my_bitmap == NULL)
		fatal("bit_alloc malloc failure");
	*bitmap = my_bitmap;
	
	if (node_names == NULL) {
		info("node_name2bitmap: node_names is NULL");
		return rc;
	}

	if ( (host_list = hostlist_create (node_names)) == NULL) {
		/* likely a badly formatted hostlist */
		error ("hostlist_create on %s error:", node_names);
		if (!best_effort)
			rc = EINVAL;
		return rc;
	}

	while ( (this_node_name = hostlist_shift (host_list)) ) {
		struct node_record *node_ptr;
		node_ptr = find_node_record (this_node_name);
		if (node_ptr) {
			bit_set (my_bitmap, (bitoff_t) (node_ptr - 
						node_record_table_ptr));
		} else {
			error ("node_name2bitmap: invalid node specified %s",
			       this_node_name);
			if (!best_effort)
				rc = EINVAL;
		}
		free (this_node_name);
	}
	hostlist_destroy (host_list);

	return rc;
}


/* Purge the contents of a node record */
extern void purge_node_rec (struct node_record *node_ptr)
{
	xfree(node_ptr->arch);
	xfree(node_ptr->comm_name);
	xfree(node_ptr->features);
	xfree(node_ptr->name);
	xfree(node_ptr->os);
	xfree(node_ptr->part_pptr);
	xfree(node_ptr->reason);
	select_g_select_nodeinfo_free(node_ptr->select_nodeinfo);
}


/* 
 * rehash_node - build a hash table of the node_record entries. 
 * NOTE: manages memory for node_hash_table
 */
extern void rehash_node (void) 
{
	int i, inx;
	struct node_record *node_ptr = node_record_table_ptr;

	xfree (node_hash_table);
	node_hash_table = xmalloc (sizeof (struct node_record *) * 
				   node_record_count);

	for (i = 0; i < node_record_count; i++, node_ptr++) {
		if ((node_ptr->name == NULL) ||
		    (node_ptr->name[0] == '\0'))
			continue;	/* vestigial record */
		inx = _hash_index (node_ptr->name);
		node_ptr->node_next = node_hash_table[inx];
		node_hash_table[inx] = node_ptr;
	}

#if _DEBUG
	_dump_hash();
#endif
	return;
}

/* Convert a node state string to it's equivalent enum value */
extern int state_str2int(const char *state_str)
{
	int state_val = NO_VAL;
	int i;

	for (i = 0; i <= NODE_STATE_END; i++) {
		if (strcasecmp(node_state_string(i), "END") == 0)
			break;
		if (strcasecmp(node_state_string(i), state_str) == 0) {
			state_val = i;
			break;
		}
	}
	if (i >= NODE_STATE_END) {
		if (strncasecmp("DRAIN", state_str, 5) == 0)
			state_val = NODE_STATE_UNKNOWN | NODE_STATE_DRAIN;
		else if (strncasecmp("FAIL", state_str, 4) == 0)
			state_val = NODE_STATE_IDLE | NODE_STATE_FAIL;
	}
	if (state_val == NO_VAL) {
		error("invalid node state %s", state_str);
		errno = EINVAL;
	}
	return state_val;
}
