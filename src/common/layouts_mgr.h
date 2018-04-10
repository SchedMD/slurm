/*****************************************************************************\
 *  layouts_mgr.h - layouts manager data structures and main functions
 *****************************************************************************
 *  Initially written by Francois Chevallier <chevallierfrancois@free.fr>
 *  at Bull for slurm-2.6.
 *  Adapted by Matthieu Hautreux <matthieu.hautreux@cea.fr> for slurm-14.11.
 *  Enhanced by Matthieu Hautreux <matthieu.hautreux@cea.fr> for slurm-15.x.
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

#ifndef __LAYOUTS_MGR_1NRINRSD__INC__
#define __LAYOUTS_MGR_1NRINRSD__INC__

#include "src/common/list.h"
#include "src/common/xhash.h"
#include "src/common/xtree.h"
#include "src/common/pack.h"
#include "src/common/parse_config.h"

#include "src/common/layout.h"
#include "src/common/entity.h"

/*
 * Layouts are managed through a "layouts manager" of type layouts_mgr_t.
 *
 * The layouts_mgr_t manages the layouts and entities loaded through the list
 * of layouts specified in the Slurm configuration file (slurm.conf)
 *
 * At startup, Slurm initialize one layouts_mgr_t using layouts_init()
 * and then load the required layouts defined in the configuration using
 * layouts_load_config().
 *
 * The different layouts and entities can then be queried using either
 * layouts_get_layout() and layouts_get_entity().
 *
 * Note that each entity contains a list of nodes appearing inside the
 * associated layouts.
 */

/*
 * Potential enhancement to complete: agregate specified plugin etypes in a
 *      xhash in the mgr, avoiding same string to be duplicated again and again.
 *      (in short: apply the same logic for etypes as for entity data keys.)
 */

typedef enum layouts_keydef_types_en {
	L_T_ERROR = 0,
	L_T_STRING,
	L_T_LONG,
	L_T_UINT16,
	L_T_UINT32,
	L_T_BOOLEAN,
	L_T_FLOAT,
	L_T_DOUBLE,
	L_T_LONG_DOUBLE,
	L_T_CUSTOM,
} layouts_keydef_types_t;

/* keyspec flags */
#define KEYSPEC_RDONLY        0x00000001

#define KEYSPEC_UPDATE_CHILDREN_SUM   0x00010000
#define KEYSPEC_UPDATE_CHILDREN_AVG   0x00020000
#define KEYSPEC_UPDATE_CHILDREN_MIN   0x00040000
#define KEYSPEC_UPDATE_CHILDREN_MAX   0x00080000
#define KEYSPEC_UPDATE_CHILDREN_COUNT 0x00110000
#define KEYSPEC_UPDATE_CHILDREN_MASK  0x00FF0000

#define KEYSPEC_UPDATE_PARENTS_SUM    0x01000000
#define KEYSPEC_UPDATE_PARENTS_AVG    0x02000000
#define KEYSPEC_UPDATE_PARENTS_MIN    0x04000000
#define KEYSPEC_UPDATE_PARENTS_MAX    0x08000000
#define KEYSPEC_UPDATE_PARENTS_FSHARE 0x11000000
#define KEYSPEC_UPDATE_PARENTS_MASK   0xFF000000

typedef struct layouts_keyspec_st {
	char*			key;
	layouts_keydef_types_t	type;
	uint32_t                flags;
	char*			ref_key; /* reference key to use for update
					  * NULL means use the same key in my
					  * neighborhood */
	void			(*custom_destroy)(void*);
	char*			(*custom_dump)(void*);
} layouts_keyspec_t;

typedef struct layouts_plugin_spec_st {
	const s_p_options_t*		options;
	const layouts_keyspec_t*	keyspec;
	int				struct_type;
	const char**			etypes;
	bool				automerge;
	bool				autoupdate;
} layouts_plugin_spec_t;

/*****************************************************************************\
 *                             PLUGIN FUNCTIONS                              *
\*****************************************************************************/

/*
 * layouts_init - intialize the layouts mgr, load the required plugins
 *        and initialize the internal hash tables for entities, keydefs and
 *        layouts.
 *
 * Return SLURM_SUCCESS or SLURM_ERROR if all the required layouts were not
 * loaded correctly.
 *
 * Notes: this call do not try to read and parse the layouts configuration
 * files. It only loads the layouts plugins, dlsym the layout API and conf
 * elements to prepare the reading and parsing performed in the adhoc call
 * layouts_load_config()
 *
 */
int layouts_init(void);

/*
 * layouts_fini - uninitialize the layouts mgr and free the internal
 *        hash tables.
 */
int layouts_fini(void);

/*
 * layouts_load_config - use the layouts plugins details loaded during
 *        layouts_init() and read+parse the different layouts
 *        configuration files, creating the entities and the relational
 *        structures associated the eaf of them.
 *
 * IN recover - update entities information with the latest available
 *              information depending upon value
 *              0 = use no saved state information, rebuild everything from
 *		    layouts conf files contents
 *              1 = recover saved entities information
 *              2 = recover saved entities information
 *
 * Return SLURM_SUCCESS or SLURM_ERROR if all the required layouts were not
 * loaded correctly.
 */
int layouts_load_config(int recover);

/*
 * layouts_get_layout - return the layout from a given type
 *
 * Return a pointer to the layout_t struct of the layout or NULL if not found
 */
layout_t* layouts_get_layout(const char* type);

/*
 * layouts_get_entity - return the entity from a given name
 *
 * Return a pointer to the entity_t struct of the entity or NULL if not found
 */
entity_t* layouts_get_entity(const char* name);

/*
 * layouts_pack_layout - pack the layout of the target type into the provided
 *        buffer.
 *
 * The buffer will be appended with multiple strings representing an expanded
 * form of its configuration element, terminated by a "\0" string.
 *
 * Return SLURM_SUCCES or SLURM_ERROR in case of failure
 */
int layouts_pack_layout(char *l_type, char *entities, char *type,
			uint32_t flags, Buf buffer);

/*
 * layouts_update_layout - update a particular layout loading the information
 *        provided in the input buffer.
 *
 * The buffer must contain multiple strings corresponding to the different
 * configuration lines similar to those that can be put in a configuration
 * file that will be parsed and integrated.
 *
 * Note that the entities key/value entries will be updated only.
 *
 * Return SLURM_SUCCES or SLURM_ERROR in case of failure
 */
int layouts_update_layout(char *l_type, Buf buffer);

/*
 * layouts_state_save_layout - save the state of a particular layout
 *        in the adhoc file in slurm state save location.
 *
 * The file produced will be an ASCII file created from the configuration
 * strings packed using layouts_pack_layout(). Thus it will be the expanded
 * form of the current configuration of the layout that could be used as
 * a perfect updated replacement of the layout configuration file.
 *
 * Return SLURM_SUCCES or SLURM_ERROR in case of failure
 */
int layouts_state_save_layout(char* l_type);

/*
 * layouts_state_save - save the state of all the loaded layouts iterating
 *        over each one of them and applying layouts_state_save_layout().
 *
 * Return SLURM_SUCCES or SLURM_ERROR in case of failure
 */
int layouts_state_save(void);

/*
 * layouts_entity_get_kv_type - get the type of the value associated with a key
 *        of an entity in a particular layout.
 *
 * The returned type is a member of the layouts_keydef_types_t enum :
 * L_T_ERROR, L_T_STRING, L_T_LONG, L_T_UINT16, ...
 *
 * Return the requested type or SLURM_ERROR in case of failure
 */
int layouts_entity_get_kv_type(char* layout, char* entity,
			       char* key);

/*
 * layouts_entity_get_kv_flags - get the keyspec flags associated with the
 *        targeted key/value pair of an entity in a particular layout.
 *
 * Return the associated flags or SLURM_ERROR in case of failure
 */
int layouts_entity_get_kv_flags(char* layout, char* entity,
				char* key);

/*
 * layouts_entity_push_kv - update the layout internal states to take into
 *        account the current state of the targeted key/value pair.
 *
 * This ensures that the child and/or parents of the targeted entity in the
 * targeted layout are synchronized with the current value associated with
 * the key.
 *
 * Note: this call only makes sense when the targeted k/v is a k/v that helps
 *       to dynamically compute its parents and/or children. It is a
 *       no-op otherwise that just returns SLURM_SUCCESS.
 *
 * Return SLURM_SUCCES or SLURM_ERROR in case of failure
 */
int layouts_entity_push_kv(char* layout, char* entity,
			   char* key);

/*
 * layouts_entity_pull_kv - synchronize the targeted key/value pair based on
 *        the states of their neighborhood in the targeted layout.
 *
 * This ensures that the K/V is up-to-date and correspond to the values that
 *        its neighborhood in the layout think it should have.
 *
 * Note: this call only makes sense when the targeted k/v is a k/v that is
 *       dynamically computed based on its parents and/or children. It is a
 *       no-op otherwise that just returns SLURM_SUCCESS.
 *
 * Return SLURM_SUCCES or SLURM_ERROR in case of failure
 */
int layouts_entity_pull_kv(char* layout, char* entity,
			   char* key);

/*
 * layouts_entity_set_kv - update an entity with a new value for a particular
 *        key in the targeted layout.
 *
 * The input key_type will force the call to check types consistency between
 * the requester and the underlying keyspec associated with the key. To skip
 * that check the caller will have to pass a 0 value.
 *
 * Note : in case the key/value is already set for the entity, the content of
 * the provided buffer will override the current content. In case the key/value
 * already exists, it will be xfree and a new memory allocation will be
 * performed and the content of the provided buffer dumped into it.
 *
 * Return SLURM_SUCCES or SLURM_ERROR in case of failure
 */
int layouts_entity_set_kv(char* layout, char* entity,
			  char* key, void* value,
			  layouts_keydef_types_t key_type);

/*
 * layouts_entity_set_kv_ref - replace an entity key value with a new memory
 *        area for a particular key in the targeted layout
 *
 * The input key_type will force the call to check types consistency between
 * the requester and the underlying keyspec associated with the key. To skip
 * that check the caller will have to pass a 0 value.
 *
 * Note : in case the key/value is already set for the entity, the older value
 * will be free and the provided buffer will be associated to the new value.
 * Once done, the caller must not free the provided buffer has it will then
 * be owned by the layout logic and will be free automatically when the layout
 * framework will be unloaded or at a next call to that function.
 *
 * Return SLURM_SUCCES or SLURM_ERROR in case of failure
 */
int layouts_entity_set_kv_ref(char* layout, char* entity,
			      char* key, void* value,
			      layouts_keydef_types_t key_type);

/*
 * layouts_entity_setpush_kv - combination of layouts_entity_set_kv and
 *        layouts_entity_push_kv to update an entity with a new value and force
 *        the synchronization of its neighborhood in the layout.
 *
 * The input key_type will force the call to check types consistency between
 * the requester and the underlying keyspec associated with the key. To skip
 * that check the caller will have to pass a 0 value.
 *
 * Note: see layouts_entity_push_kv.
 *
 * Return SLURM_SUCCES or SLURM_ERROR in case of failure
 */
int layouts_entity_setpush_kv(char* layout, char* entity,
			      char* key, void* value,
			      layouts_keydef_types_t key_type);

/*
 * layouts_entity_setpush_kv - combination of layouts_entity_set_kv_ref and
 *        layouts_entity_push_kv to replace an entity key value with a new
 *        memory area and force the synchronization of its neighborhood in
 *        the layout.
 *
 * The input key_type will force the call to check types consistency between
 * the requester and the underlying keyspec associated with the key. To skip
 * that check the caller will have to pass a 0 value.
 *
 * Note: see layouts_entity_push_kv.
 *
 * Return SLURM_SUCCES or SLURM_ERROR in case of failure
 */
int layouts_entity_setpush_kv_ref(char* layout, char* entity,
				  char* key, void* value,
				  layouts_keydef_types_t key_type);

/*
 * layouts_entity_get_kv - get the value associated with a key of an entity
 *        in a particular layout.
 *
 * The input key_type will force the call to check types consistency between
 * the requester and the underlying keyspec associated with the key. To skip
 * that check the caller will have to pass a 0 value.
 *
 * Note : the destination buffer will be filled with the content of the
 * value associated with the requested key in the entity except for these
 * types for which :
 *   L_T_STRING  : value must be the address of the char* that will be
 *                 xstrduped with the key value. The char* will have to be
 *                 xfree() after that.
 *   L_T_CUSTOM : value must be the address of the char* that will result
 *                of the custom_dump function. The char* will have to be
 *                xfree() after that.
 *   L_T_ERROR : will return SLURM_ERROR in all cases.
 *
 * Return SLURM_SUCCES or SLURM_ERROR in case of failure
 */
int layouts_entity_get_kv(char* layout, char* entity,
			  char* key, void* value,
			  layouts_keydef_types_t key_type);

/*
 * layouts_entity_get_kv_ref - get a pointer to the value associated with a key
 *        of an entity in a particular layout.
 *
 * The input key_type will force the call to check types consistency between
 * the requester and the underlying keyspec associated with the key. To skip
 * that check the caller will have to pass a 0 value.
 *
 * Note : this call must be used with caution as the pointer could be free
 * sooner or later by the underlying layout engine in reply to the execution
 * of the layouts_entity_set_kv_ref().
 *
 * Return SLURM_SUCCES or SLURM_ERROR in case of failure
 */
int layouts_entity_get_kv_ref(char* layout, char* entity,
			      char* key, void** pvalue,
			      layouts_keydef_types_t key_type);

/*
 * layouts_entity_pullget_kv - combination of layouts_entity_pull_kv and
 *        layouts_entity_get_kv to retrieve the up-to-date value of a particular
 *        entity key in the targeted layout.
 *
 * The input key_type will force the call to check types consistency between
 * the requester and the underlying keyspec associated with the key. To skip
 * that check the caller will have to pass a 0 value.
 *
 * Note: see layouts_entity_pull_kv.
 *
 * Return SLURM_SUCCES or SLURM_ERROR in case of failure
 */
int layouts_entity_pullget_kv(char* layout, char* entity,
			      char* key, void* value,
			      layouts_keydef_types_t key_type);

/*
 * layouts_entity_pullget_kv - combination of layouts_entity_pull_kv_ref and
 *        layouts_entity_get_kv to retrieve a reference to the up-to-date value
 *        of a particular entity key in the targeted layout.
 *
 * The input key_type will force the call to check types consistency between
 * the requester and the underlying keyspec associated with the key. To skip
 * that check the caller will have to pass a 0 value.
 *
 * Note: see layouts_entity_pull_kv_ref.
 *
 * Return SLURM_SUCCES or SLURM_ERROR in case of failure
 */
int layouts_entity_pullget_kv_ref(char* layout, char* entity,
				  char* key, void** value,
				  layouts_keydef_types_t key_type);

/*
 * layouts_entity_get_mkv - get the values associated with a set of keys of an
 *        entity in a particular layout.
 *
 * The input key_type will force the call to check types consistency between
 * the requester and the underlying keyspec associated with the key. To skip
 * that check the caller will have to pass a 0 value. This is mandatory for
 * cases where the keyspecs of the requested keys do not share the same type.
 *
 * Note : the destination buffer will be sequentially filled with the content of
 * the values associated with the requested keys in the entity.
 * If the length of the buffer is too small, the remaining references will not
 * be added and the counter of missed keys incremented as necessary.
 * The first encountered error terminates the logic and the missing elements
 * counter will reflect all the unprocessed elements including the faulty one.

 * Special care must be taken for the following types of key :
 *   L_T_STRING  : a char* will be added to the buffer. It will be xstrduped
 *                 with the associated key value. The char* will have to be
 *                 xfree() after that.
 *   L_T_CUSTOM : a char* will be added to the buffer. It will be xstrduped
 *                with the result of the custom_dump function. It will have to
 *                be xfree() after that.
 *   L_T_ERROR : will generate an error that will force the function to return
 *               the count of missing elements (at least 1, depending on where
 *               this type first appeared in the ordered list of keys to get.
 *
 * Note: keys correspond to a list of keys that can be represented as
 * an hostlist expression (i.e. keys[1-10]).
 *
 * Return SLURM_SUCCES or the count of missed keys/references
 */
int layouts_entity_get_mkv(char* layout, char* entity,
			   char* keys, void* value, size_t length,
			   layouts_keydef_types_t key_type);

/*
 * layouts_entity_get_mkv_ref - get a set of pointers to the values associated
 *        with a set of keys of an entity in a particular layout.
 *
 * The input key_type will force the call to check types consistency between
 * the requester and the underlying keyspecs associated with the keys. To skip
 * that check the caller will have to pass a 0 value. This is mandatory for cases
 * where the keyspecs of the requested keys do not share the same type.
 *
 * The output buffer will be filled with the different references.
 * If the length of the buffer is too small, the remaining references will not
 * be added and the counter of missed keys incremented as necessary.
 * The first encountered error terminates the logic and the missing elements
 * counter will reflect all the unprocessed elements including the faulty one.
 *
 * Note: this call must be used with caution as the pointers could be free
 * sooner or later by the underlying layout engine in reply to the execution
 * of the layouts_entity_set_kv_ref().
 *
 * Note: keys correspond to a list of keys that can be represented as
 * an hostlist expression (i.e. keys[1-10]).
 *
 * Return SLURM_SUCCES or the count of missed keys/references
 */
int layouts_entity_get_mkv_ref(char* layout, char* entity,
			       char* keys, void* buffer, size_t length,
			       layouts_keydef_types_t key_type);

#endif /* end of include guard: __LAYOUTS_MGR_1NRINRSD__INC__ */
