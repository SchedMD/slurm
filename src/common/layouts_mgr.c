/*****************************************************************************\
 *  layouts_mgr.c - layouts manager data structures and main functions
 *****************************************************************************
 *  Initially written by Francois Chevallier <chevallierfrancois@free.fr>
 *  at Bull for slurm-2.6.
 *  Adapted by Matthieu Hautreux <matthieu.hautreux@cea.fr> for slurm-14.11.
 *
 *  This file is part of SLURM, a resource management program.
 *  For details, see <http://slurm.schedmd.com/>.
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

#include <pthread.h>
#include <string.h>
#include <strings.h>
#include <ctype.h>

#include "layouts_mgr.h"

#include "src/common/entity.h"
#include "src/common/layout.h"

#include "slurm/slurm.h"
#include "slurm/slurm_errno.h"
#include "src/common/hostlist.h"
#include "src/common/list.h"
#include "src/common/node_conf.h"
#include "src/common/plugin.h"
#include "src/common/read_config.h"
#include "src/common/slurm_protocol_api.h"
#include "src/common/xstring.h"
#include "src/common/xtree.h"
#include "src/common/xmalloc.h"

#define PATHLEN 256

/*****************************************************************************\
 *                            STRUCTURES AND TYPES                           *
\*****************************************************************************/

void free(void*);

/*
 * layouts_conf_spec_t - structure used to keep track of layouts conf details
 */
typedef struct layouts_conf_spec_st {
	char* whole_name;
	char* name;
	char* type;
} layouts_conf_spec_t;

static void layouts_conf_spec_free(void* x)
{
	layouts_conf_spec_t* spec = (layouts_conf_spec_t*)x;
	xfree(spec->whole_name);
	xfree(spec->type);
	xfree(spec->name);
	xfree(spec);
}

/*
 * layout ops - operations associated to layout plugins
 *
 * This struct is populated while opening the plugin and linking the
 * associated symbols. See layout_syms description for the name of the "public"
 * symbols associated to this structure fields.
 *
 * Notes : the layouts plugins are able to access the entities hashtable in order
 * to read/create/modify entities as necessary during the load_entities and
 * build_layout API calls.
 *
 */
typedef struct layout_ops_st {
	layouts_plugin_spec_t*	spec;
	int (*conf_done) (xhash_t* entities, layout_t* layout,
			  s_p_hashtbl_t* tbl);
	void (*entity_parsing) (entity_t* e, s_p_hashtbl_t* etbl,
				layout_t* layout);
} layout_ops_t;

/*
 * layout plugin symbols - must be synchronized with ops structure definition
 *        as previously detailed, that's why though being a global constant,
 *        it is placed in this section.
 */
const char *layout_syms[] = {
	"plugin_spec",             /* holds constants, definitions, ... */
	"layouts_p_conf_done",     /* */
	"layouts_p_entity_parsing",
};

/*
 * layout_plugin_t - it is the structure holding the plugin context of the
 *        associated layout plugin as well as the ptr to the dlsymed calls.
 *        It is used by the layouts manager to operate on the different layouts
 *        loaded during the layouts framework initialization
 */
typedef struct layout_plugin_st {
	plugin_context_t* context;
	layout_t* layout;
	char* name;
	layout_ops_t* ops;
} layout_plugin_t;

static void _layout_plugins_destroy(layout_plugin_t *lp) {
	plugin_context_destroy(lp->context);
	/* it might be interesting to also dlclose the ops here */
	layout_free(lp->layout);
	xfree(lp->name);
	xfree(lp->ops);
	xfree(lp->layout);
}
/*
 * layouts_keydef_t - entities similar keys share a same key definition
 *       in order to avoid loosing too much memory duplicating similar data
 *       like the key str itself and custom destroy/dump functions.
 *
 * The layouts manager keeps an hash table of the various keydefs and use
 * the factorized details while parsing the configuration and creating the
 * entity_data_t structs associated to the entities.
 *
 * Note custom_* functions are used if they are not NULL* and type equals
 * L_T_CUSTOM
 */
typedef struct layouts_keydef_st {
	char*			key; /* normalize to lower or upper case */
	layouts_keydef_types_t	type;
	void			(*custom_destroy)(void* value);
	char*			(*custom_dump)(void* value);
	layout_plugin_t*	plugin;
} layouts_keydef_t;

/*
 * layouts_keydef_idfunc - identity function to build an hash table of
 *        layouts_keydef_t
 */
static const char* layouts_keydef_idfunc(void* item)
{
	layouts_keydef_t* keydef = (layouts_keydef_t*)item;
	return keydef->key;
}

/*
 * layouts_mgr_t - the main structure holding all the layouts, entities and
 *        shared keydefs as well as conf elements and plugins details.
 */
typedef struct layouts_mgr_st {
	pthread_mutex_t lock;
	layout_plugin_t *plugins;
	uint32_t plugins_count;
	List    layouts_desc;  /* list of the layouts requested in conf */
	xhash_t *layouts;      /* hash tbl of loaded layout structs (by type) */
	xhash_t *entities;     /* hash tbl of loaded entity structs (by name) */
	xhash_t *keydefs;      /* info on key types, how to free them etc */
} layouts_mgr_t;

/*****************************************************************************\
 *                                  GLOBALS                                  *
\*****************************************************************************/

/** global structure holding layouts and entities */
static layouts_mgr_t layouts_mgr = {PTHREAD_MUTEX_INITIALIZER};
static layouts_mgr_t* mgr = &layouts_mgr;

/*****************************************************************************\
 *                                  HELPERS                                  *
\*****************************************************************************/

/* entities added to the layouts mgr hash table come from the heap,
 * this funciton will help to free them while freeing the hash table */
static void _entity_free(void* item)
{
	entity_t* entity = (entity_t*) item;
	entity_free(entity);
	xfree(entity);
}

/* layouts added to the layouts mgr hash table come from the heap,
 * this funciton will help to free them while freeing the hash table */
static void _layout_free(void* item)
{
	layout_t* layout = (layout_t*) item;
	layout_free(layout);
	xfree(layout);
}

/* keydef added to the layouts mgr hash table come from the heap,
 * this funciton will help to free them while freeing the hash table */
static void _layouts_keydef_free(void* x)
{
	layouts_keydef_t* keydef = (layouts_keydef_t*)x;
	xfree(keydef->key);
	xfree(keydef);
}

/* generic xfree callback */
static void xfree_as_callback(void* p)
{
	xfree(p);
}

/* safer behavior than plain strncat */
static char* _cat(char* dest, const char* src, size_t n)
{
	size_t len;
	char* r;
	if (n == 0)
		return dest;
	len = strlen(dest);
	if (n - len - 1 <= 0) {
		dest[n - 1] = 0;
		return dest;
	}
	r = strncat(dest, src, n - len - 1);
	dest[n - 1] = 0;
	return r;
}

/* safer behavior than plain strncpy */
static char* _cpy(char* dest, const char* src, size_t n)
{
	char* r;
	if (n == 0)
		return dest;
	r = strncpy(dest, src, n - 1);
	dest[n - 1] = 0;
	return r;
}

static char* trim(char* str)
{
	char* str_modifier;
	if (!str)
		return str;
	while (*str && isspace(*str)) ++str;
	str_modifier = str + strlen(str) - 1;
	while (str_modifier >= str && isspace(*str_modifier)) {
		*str_modifier = '\0';
		--str_modifier;
	}
	return str;
}

/* check if str is in strings (null terminated string array) */
/* TODO: replace this with a xhash instead for next modification */
static int _string_in_array(const char* str, const char** strings)
{
	xassert(strings); /* if etypes no specified in plugin, no new entity
			     should be created */
	for (; *strings; ++strings) {
		if (!strcmp(str, *strings))
			return 1;
	}
	return 0;
}

static void _normalize_keydef_keycore(char* buffer, uint32_t size,
				      const char* key, const char* plugtype,
				      bool cat)
{
	int i;
	char keytmp[PATHLEN];

	for (i = 0; plugtype[i] && i < PATHLEN - 1; ++i) {
		keytmp[i] = tolower(plugtype[i]);
	}
	keytmp[i] = 0;
	if (cat) {
		_cat(buffer, keytmp, size);
	} else {
		_cpy(buffer, keytmp, size);
	}
	_cat(buffer, ".", size);
	for (i = 0; key[i] && i < PATHLEN - 1; ++i) {
		keytmp[i] = tolower(key[i]);
	}
	keytmp[i] = 0;
	_cat(buffer, keytmp, size);
}

static void _normalize_keydef_key(char* buffer, uint32_t size,
				  const char* key, const char* plugtype)
{
	_normalize_keydef_keycore(buffer, size, key, plugtype, false);
}

static void _normalize_keydef_mgrkey(char* buffer, uint32_t size,
				     const char* key, const char* plugtype)
{
	_cpy(buffer, "mgr.", size);
	_normalize_keydef_keycore(buffer, size, key, plugtype, true);
}

static void _entity_add_data(entity_t* e, const char* key, void* data)
{
	int rc;
	layouts_keydef_t* hkey = xhash_get(mgr->keydefs, key);
	xassert(hkey);
	void (*freefunc)(void* p) = xfree_as_callback;
	if (hkey->type == L_T_CUSTOM) {
		freefunc = hkey->custom_destroy;
	}
	rc = entity_add_data(e, hkey->key, data, freefunc);
	if (rc)
		xassert(rc);
}

/*****************************************************************************\
 *                                MANAGER INIT                               *
\*****************************************************************************/

static void _slurm_layouts_init_keydef(xhash_t* keydefs,
				       const layouts_keyspec_t* plugin_keyspec,
				       layout_plugin_t* plugin)
{
	char keytmp[PATHLEN];

	const layouts_keyspec_t* current;
	layouts_keydef_t* nkeydef;

	/* A layout plugin may have no data to store to entities but still
	 * being valid. */
	if (!plugin_keyspec)
		return;

	/* iterate over the keys of the plugin */
	for (current = plugin_keyspec; current->key; ++current) {
		/* if not end of list, a keyspec key is mandatory */
		_normalize_keydef_key(keytmp, PATHLEN, current->key,
				      plugin->layout->type);
		xassert(xhash_get(keydefs, keytmp) == NULL);
		nkeydef = (layouts_keydef_t*)
			xmalloc(sizeof(layouts_keydef_t));
		nkeydef->key = xstrdup(keytmp);
		nkeydef->type = current->type;
		nkeydef->custom_destroy = current->custom_destroy;
		nkeydef->custom_dump = current->custom_dump;
		nkeydef->plugin = plugin;
		xhash_add(keydefs, nkeydef);
	}

	/* then add keys managed by the layouts_mgr directly */
	switch(plugin->layout->struct_type) {
	case LAYOUT_STRUCT_TREE:
		_normalize_keydef_mgrkey(keytmp, PATHLEN, "enclosed",
					 plugin->layout->type);
		xassert(xhash_get(keydefs, keytmp) == NULL);
		nkeydef = (layouts_keydef_t*)
			xmalloc(sizeof(layouts_keydef_t));
		nkeydef->key = xstrdup(keytmp);
		nkeydef->type = L_T_STRING;
		nkeydef->plugin = plugin;
		xhash_add(keydefs, nkeydef);
		break;
	}
}

static int _slurm_layouts_init_layouts_walk_helper(void* x, void* arg)
{
	layouts_conf_spec_t* spec = (layouts_conf_spec_t*)x;
	int* i = (int*)arg;
	layout_plugin_t* plugin = &mgr->plugins[*i];
	const char* plugin_type = "layouts";
	char plugin_name[PATHLEN];
	void* inserted_item;
	plugin_context_t* plugin_context;
	snprintf(plugin_name, PATHLEN,
		 "layouts/%s_%s", spec->type, spec->name);
	plugin->ops = (layout_ops_t*)xmalloc(sizeof(layout_ops_t));
	debug2("layouts: loading %s...", spec->whole_name);
	plugin->context = plugin_context = plugin_context_create(
		plugin_type,
		plugin_name,
		(void**)plugin->ops,
		layout_syms,
		sizeof(layout_syms));
	if (!plugin_context) {
		error("layouts: error loading %s.", plugin_name);
		return SLURM_ERROR;
	}
	if (!plugin->ops->spec) {
		error("layouts: plugin_spec must be valid (%s plugin).",
		      plugin_name);
		return SLURM_ERROR;

	}
	plugin->name = xstrdup(spec->whole_name);
	plugin->layout = (layout_t*)xmalloc(sizeof(layout_t));
	layout_init(plugin->layout, spec->name, spec->type, 0,
		    plugin->ops->spec->struct_type);
	inserted_item = xhash_add(mgr->layouts, plugin->layout);
	if (inserted_item)
		xassert(inserted_item == plugin->layout);
	_slurm_layouts_init_keydef(mgr->keydefs,
				   plugin->ops->spec->keyspec,
				   plugin);
	++*i;
	return SLURM_SUCCESS;
}

static void _layouts_mgr_parse_global_conf(layouts_mgr_t* mgr)
{
	char* layouts;
	char* parser;
	char* saveptr;
	char* slash;
	layouts_conf_spec_t* nspec;

	mgr->layouts_desc = list_create(layouts_conf_spec_free);
	layouts = slurm_get_layouts();
	parser = strtok_r(layouts, ",", &saveptr);
	while (parser) {
		nspec = (layouts_conf_spec_t*)xmalloc(
			sizeof(layouts_conf_spec_t));
		nspec->whole_name = xstrdup(trim(parser));
		slash = strchr(parser, '/');
		if (slash) {
			*slash = 0;
			nspec->type = xstrdup(trim(parser));
			nspec->name = xstrdup(trim(slash+1));
		} else {
			nspec->type = xstrdup(trim(parser));
			nspec->name = xstrdup("default");
		}
		list_append(mgr->layouts_desc, nspec);
		parser = strtok_r(NULL, ",", &saveptr);
	}
	xfree(layouts);
}

static void layouts_mgr_init(layouts_mgr_t* mgr)
{
	_layouts_mgr_parse_global_conf(mgr);

	mgr->layouts = xhash_init(layout_hashable_identify_by_type,
				  (xhash_freefunc_t)_layout_free, NULL, 0);
	mgr->entities = xhash_init(entity_hashable_identify,
				   (xhash_freefunc_t)_entity_free, NULL, 0);
	mgr->keydefs = xhash_init(layouts_keydef_idfunc,
				  _layouts_keydef_free, NULL, 0);
}

static void layouts_mgr_free(layouts_mgr_t* mgr)
{
	/* free the configuration details */
	FREE_NULL_LIST(mgr->layouts_desc);

	/* FIXME: can we do a faster free here ? since each node removal will
	 * modify either the entities or layouts for back (or forward)
	 * references. */
	xhash_free(mgr->layouts);
	xhash_free(mgr->entities);
	xhash_free(mgr->keydefs);
}

/*****************************************************************************\
 *                               CONFIGURATION                               *
\*****************************************************************************/

static char* _conf_get_filename(const char* type)
{
	char path[PATHLEN];
	char* final_path;
	_cpy(path, "layouts.d/", PATHLEN);
	_cat(path, type, PATHLEN);
	_cat(path, ".conf", PATHLEN);
	final_path = get_extra_conf_path(path);
	return final_path;
}

static s_p_hashtbl_t* _conf_make_hashtbl(int struct_type,
					 const s_p_options_t* layout_options)
{
	s_p_hashtbl_t* tbl = NULL;
	s_p_hashtbl_t* tbl_relational = NULL;
	s_p_hashtbl_t* tbl_layout = NULL;
	s_p_options_t* relational_options = NULL;

	/* generic line option */
	static s_p_options_t global_options_entity[] = {
		{"Entity", S_P_STRING},
		{"Type", S_P_STRING},
		{NULL}
	};
	static s_p_options_t global_options[] = {
		{"Priority", S_P_UINT32},
		{"Entity", S_P_EXPLINE, NULL, NULL, global_options_entity},
		{NULL}
	};

	/* available for constructing a tree */
	static s_p_options_t tree_options_entity[] = {
		{"Enclosed", S_P_PLAIN_STRING},
		{NULL}
	};
	static s_p_options_t tree_options[] = {
		{"Root", S_P_STRING},
		{"Entity", S_P_EXPLINE, NULL, NULL, tree_options_entity},
		{NULL}
	};

	xassert(layout_options);

	switch(struct_type) {
	case LAYOUT_STRUCT_TREE:
		relational_options = tree_options;
		break;
	default:
		fatal("layouts: does not know what relation structure to"
		      "use for type %d", struct_type);
	}

	tbl = s_p_hashtbl_create(global_options);
	tbl_relational = s_p_hashtbl_create(relational_options);
	tbl_layout = s_p_hashtbl_create(layout_options);

	s_p_hashtbl_merge_keys(tbl, tbl_relational);
	s_p_hashtbl_merge_keys(tbl, tbl_layout);

	s_p_hashtbl_destroy(tbl_relational);
	s_p_hashtbl_destroy(tbl_layout);

	return tbl;
}

#define _layouts_load_merge(type_t, s_p_get_type) { \
	type_t newvalue; \
	type_t** oldvalue; \
	if (!s_p_get_type(&newvalue, option_key, etbl)) { \
		/* no value to merge/create */ \
		continue; \
	} \
	oldvalue = (type_t**)entity_get_data(e, key_keydef); \
	if (oldvalue) { \
		**oldvalue = newvalue; \
	} else { \
		type_t* newalloc = (type_t*)xmalloc(sizeof(type_t)); \
		*newalloc = newvalue; \
		_entity_add_data(e, key_keydef, newalloc); \
	} \
}

#define _layouts_merge_check(type1, type2) \
	(entity_option->type == type1 && keydef->type == type2)

static void _layouts_load_automerge(layout_plugin_t* plugin, entity_t* e,
		s_p_hashtbl_t* etbl)
{
	const s_p_options_t* layout_option;
	const s_p_options_t* entity_option;
	layouts_keydef_t* keydef;
	char key_keydef[PATHLEN];
	char* option_key;

	for (layout_option = plugin->ops->spec->options;
		layout_option && strcasecmp("Entity", layout_option->key);
		++layout_option);
	xassert(layout_option);

	for (entity_option = layout_option->line_options;
			entity_option->key;
			++entity_option) {
		option_key = entity_option->key;
		_normalize_keydef_key(key_keydef, PATHLEN, option_key,
				plugin->layout->type);
		keydef = xhash_get(mgr->keydefs, key_keydef);
		if (!keydef) {
			/* key is not meant to be automatically handled,
			 * ignore it for this function */
			continue;
		}
		if (_layouts_merge_check(S_P_LONG, L_T_LONG)) {
			_layouts_load_merge(long, s_p_get_long);
		} else if (_layouts_merge_check(S_P_UINT16, L_T_UINT16)) {
			_layouts_load_merge(uint16_t, s_p_get_uint16);
		} else if (_layouts_merge_check(S_P_UINT32, L_T_UINT32)) {
			_layouts_load_merge(uint32_t, s_p_get_uint32);
		} else if (_layouts_merge_check(S_P_BOOLEAN, L_T_BOOLEAN)) {
			_layouts_load_merge(bool, s_p_get_boolean);
		} else if (_layouts_merge_check(S_P_LONG, L_T_LONG)) {
			_layouts_load_merge(long, s_p_get_long);
		} else if (_layouts_merge_check(S_P_STRING, L_T_STRING)) {
			char* newvalue;
			if (s_p_get_string(&newvalue, option_key, etbl)) {
				_entity_add_data(e, key_keydef, newvalue);
			}
		}
	}
}

/* extract Enlosed= attributes providing the relational structures info */
static void _layouts_parse_relations(layout_plugin_t* plugin, entity_t* e,
				     s_p_hashtbl_t* entity_tbl)
{
	char* e_enclosed;
	char** e_already_enclosed;
	char key[PATHLEN];
	switch(plugin->layout->struct_type) {
	case LAYOUT_STRUCT_TREE:
		if (s_p_get_string(&e_enclosed, "Enclosed", entity_tbl)) {
			_normalize_keydef_mgrkey(key, PATHLEN, "enclosed",
					plugin->layout->type);
			e_already_enclosed = (char**)entity_get_data(e, key);
			if (e_already_enclosed) {
				/* FC expressed warnings about that section,
				 * should be checked more */
				*e_already_enclosed = xrealloc(
						*e_already_enclosed,
						strlen(*e_already_enclosed) +
						strlen(e_enclosed) + 2);
				strcat(*e_already_enclosed, ",");
				strcat(*e_already_enclosed, e_enclosed);
				xfree(e_enclosed);
			} else {
				_entity_add_data(e, key, e_enclosed);
			}
		}
		break;
	}
}

static int _layouts_read_config_post(layout_plugin_t* plugin,
		s_p_hashtbl_t* tbl)
{
	char* root_nodename;
	entity_t* e;
	xtree_node_t* root_node,* inserted_node;
	xtree_t* tree;
	switch(plugin->layout->struct_type) {
	case LAYOUT_STRUCT_TREE:
		tree = layout_get_tree(plugin->layout);
		xassert(tree);
		if (!s_p_get_string(&root_nodename, "Root", tbl)) {
			error("layouts: unable to construct the layout tree, "
			      "no root node specified");
			xfree(root_nodename);
			return SLURM_ERROR;
		}
		e = xhash_get(mgr->entities, trim(root_nodename));
		if (!e) {
			error("layouts: unable to find specified root "
			      "entity `%s'", trim(root_nodename));
			xfree(root_nodename);
			return SLURM_ERROR;
		}
		xfree(root_nodename);
		root_node = xtree_add_child(tree, NULL, e, XTREE_APPEND);
		if (root_node)
			xassert(root_node);
		inserted_node = list_append(e->nodes, root_node);
		if (inserted_node)
			xassert(inserted_node == root_node);
		break;
	}
	return SLURM_SUCCESS;
}

/*
 * _layouts_read_config - called after base entities are loaded successfully
 *
 * This function is the stage 1 of the layouts loading stage, where we collect
 * info on all the entities and store them in a global hash table.
 * Entities that do not already exist are created, otherwise updated.
 *
 * Information concerning the relations among entities provided by the
 * 'Enclosed' conf pragma are also extracted here for further usage in stage 2.
 *
 * When layout plugins callbacks are called, relational structures among
 * entities are not yet built.
 */
static int _layouts_read_config(layout_plugin_t* plugin)
{
	s_p_hashtbl_t* tbl = NULL;
	s_p_hashtbl_t** entities_tbl = NULL;
	s_p_hashtbl_t* entity_tbl = NULL;
	int entities_tbl_count = 0, i;
	int rc = SLURM_ERROR;
	char* filename = NULL;

	uint32_t l_priority;

	entity_t* e;
	char* e_name = NULL;
	char* e_type = NULL;

	if (!plugin->ops->spec->options) {
		/* no option in this layout plugin, nothing to parse */
		return SLURM_SUCCESS;
	}

	tbl = _conf_make_hashtbl(plugin->layout->struct_type,
				 plugin->ops->spec->options);
	filename = _conf_get_filename(plugin->layout->type);
	if (!filename) {
		fatal("layouts: cannot find configuration file for "
		      "required layout '%s'", plugin->name);
	}
	if (s_p_parse_file(tbl, NULL, filename, false) == SLURM_ERROR) {
		fatal("layouts: something went wrong when opening/reading "
		      "'%s': %m", filename);
	}
	debug3("layouts: configuration file '%s' is loaded", filename);

	if (s_p_get_uint32(&l_priority, "Priority", tbl)) {
		plugin->layout->priority = l_priority;
	}

	/* get the config hash tables of the defined entities */
	if (!s_p_get_expline(&entities_tbl, &entities_tbl_count,
				"Entity", tbl)) {
		error("layouts: no valid Entity found, can not append any "
		      "information nor construct relations for %s/%s",
		      plugin->layout->type, plugin->layout->name);
		goto cleanup;
	}

	/* stage 1: create the described entities or update them */
	for (i = 0; i < entities_tbl_count; ++i) {
		entity_tbl = entities_tbl[i];
		xfree(e_name);
		xfree(e_type);
		if (!s_p_get_string(&e_name, "Entity", entity_tbl)) {
			error("layouts: no name associated to entity[%d], "
			      "skipping...", i);
			continue;
		}

		/* look for the entity in the entities hash table*/
		e = xhash_get(mgr->entities, e_name);
		if (!e) {
			/* if the entity does not already exists, create it */
			if (!s_p_get_string(&e_type, "Type", entity_tbl)) {
				error("layouts: entity '%s' does not already "
				      "exists and no type was specified, "
				      "skipping", e_name);
				continue;
			}
			if (!_string_in_array(e_type,
					      plugin->ops->spec->etypes)) {
				error("layouts: entity '%s' type (%s) is "
				      "invalid, skipping", e_name, e_type);
				continue;
			}

			e = (entity_t*)xmalloc(sizeof(entity_t));
			entity_init(e, e_name, e_type);
			xhash_add(mgr->entities, e);

		} else if (s_p_get_string(&e_type, "Type", entity_tbl)) {
			/* if defined, check that the type is consistent */
			if (!_string_in_array(e_type,
					      plugin->ops->spec->etypes)) {
				error("layouts: entity '%s' type (%s) is "
				      "invalid, skipping", e_name, e_type);
				continue;
			}
			if (!strcmp(e_type, e->type)) {
				error("layouts: entity '%s' type (%s) differs "
				      "from already registered entity type (%s)"
				      "skipping", e_name, e_type, e->type);
				continue;
			}
		}

		/* look for "Enclosed" pragmas identifying the relations
		 * among entities and kep that along with the entity for
		 * stage 2 */
		_layouts_parse_relations(plugin, e, entity_tbl);

		/*
		 * if the layout plugin requests automerge, try to automatically
		 * parse the conf hash table using the s_p_option_t description
		 * of the plugin, creating the key/vlaue with the right value
		 * type and adding them to the entity key hash table.
		 */
		if (plugin->ops->spec->automerge) {
			_layouts_load_automerge(plugin, e, entity_tbl);
		}

		/*
		 * in case the automerge was not sufficient, the layout parsing
		 * callback is called for further actions.
		 */
		if (plugin->ops->entity_parsing) {
			plugin->ops->entity_parsing(e, entity_tbl,
						    plugin->layout);
		}
	}

	/* post-read-and-build (post stage 1)
	 * ensure that a Root entity was defined and set it as the root of
	 * the relational structure of the layout.
	 * fails in case of error as a root is mandatory to walk the relational
	 * structure of the layout */
	if (_layouts_read_config_post(plugin, tbl) != SLURM_SUCCESS) {
		goto cleanup;
	}

	/*
	 * call the layout plugin conf_done callback for further
	 * layout specific actions.
	 */
	if (plugin->ops->conf_done) {
		if (!plugin->ops->conf_done(mgr->entities, plugin->layout,
					    tbl)) {
			error("layouts: plugin %s/%s has an error parsing its"
			      " configuration", plugin->layout->type,
			      plugin->layout->name);
			goto cleanup;
		}
	}

	rc = SLURM_SUCCESS;

cleanup:
	s_p_hashtbl_destroy(tbl);
	xfree(filename);

	return rc;
}

typedef struct _layouts_build_xtree_walk_st {
	char* enclosed_key;
	xtree_t* tree;
} _layouts_build_xtree_walk_t;

uint8_t _layouts_build_xtree_walk(xtree_node_t* node,
					 uint8_t which,
					 uint32_t level,
					 void* arg)
{
	_layouts_build_xtree_walk_t* p = (_layouts_build_xtree_walk_t*)arg;
	entity_t* e;
	char** enclosed_str;
	char* enclosed_name;
	hostlist_t enclosed_hostlist;
	entity_t* enclosed_e;
	xtree_node_t* enclosed_node,* inserted_node;

	xassert(arg);

	e = xtree_node_get_data(node);
	xassert(e);

	/*
	 * FIXME: something goes wrong with the order...
	 * after a first growing, the first new child is called with preorder.
	 *
	 * for now, testing each time and use enclosed_str to know if it has
	 * been processed.
	 */
	if (which != XTREE_GROWING && which != XTREE_PREORDER)
		return 1;

	enclosed_str = (char**)entity_get_data(e, p->enclosed_key);

	if (enclosed_str) {
		enclosed_hostlist = hostlist_create(*enclosed_str);
		xfree(*enclosed_str);
		entity_delete_data(e, p->enclosed_key);
		while ((enclosed_name = hostlist_shift(enclosed_hostlist))) {
			enclosed_e = xhash_get(mgr->entities, enclosed_name);
			if (!enclosed_e) {
				error("layouts: entity '%s' specified in "
				      "enclosed entities of entity '%s' "
				      "not found, ignoring.",
				      enclosed_name, e->name);
				free(enclosed_name);
				continue;
			}
			free(enclosed_name);
			enclosed_node = xtree_add_child(p->tree, node,
							enclosed_e,
							XTREE_APPEND);
			if (enclosed_node)
				xassert(enclosed_node);
			inserted_node = list_append(enclosed_e->nodes,
						    enclosed_node);
			if (inserted_node)
				xassert(inserted_node == enclosed_node);
		}
		hostlist_destroy(enclosed_hostlist);
	}

	return 1;
}

/*
 * _layouts_build_relations - called after _layouts_read_config to create the
 *        relational structure of the layout according to the topological
 *        details parsed in stage 1. This is the stage 2 of the layouts
 *        configuration load.
 *
 * This function is the stage 2 of the layouts loading stage, where we use
 * the relational details extracted from the parsing stage (Enclosed pragmas
 * and Root entity) to build the relational structure of the layout.
 *
 */
static int _layouts_build_relations(layout_plugin_t* plugin)
{
	xtree_t* tree;
	xtree_node_t* root_node;
	char key[PATHLEN];
	switch(plugin->layout->struct_type) {
	case LAYOUT_STRUCT_TREE:
		tree = layout_get_tree(plugin->layout);
		xassert(tree);
		root_node = xtree_get_root(tree);
		_normalize_keydef_mgrkey(key, PATHLEN, "enclosed",
					 plugin->layout->type);
		_layouts_build_xtree_walk_t p = {
			key,
			tree
		};
		xtree_walk(tree,
			   root_node,
			   0,
			   XTREE_LEVEL_MAX,
			   _layouts_build_xtree_walk,
			   &p);
		break;
	}
	return SLURM_SUCCESS;
}

/*
 * For debug purposes, dump functions helping to print the layout mgr
 * internal states in a file after the load.
 */
#if 0
static char* _dump_data_key(layouts_keydef_t* keydef, void* value)
{
	char val;
	if (!keydef) {
		return xstrdup("ERROR_bad_keydef");
	}
	switch(keydef->type) {
	case L_T_ERROR:
		return xstrdup("ERROR_keytype!");
	case L_T_STRING:
		return xstrdup((char*)value);
	case L_T_LONG:
		return xstrdup_printf("%ld", *(long*)value);
	case L_T_UINT16:
		return xstrdup_printf("%u", *(uint16_t*)value);
	case L_T_UINT32:
		return xstrdup_printf("%ul", *(uint32_t*)value);
	case L_T_BOOLEAN:
		val = *(bool*)value;
		return xstrdup_printf("%s", val ? "true" : "false");
	case L_T_FLOAT:
		return xstrdup_printf("%f", *(float*)value);
	case L_T_DOUBLE:
		return xstrdup_printf("%f", *(double*)value);
	case L_T_LONG_DOUBLE:
		return xstrdup_printf("%Lf", *(long double*)value);
	case L_T_CUSTOM:
		if (keydef->custom_dump) {
			return keydef->custom_dump(value);
		}
		return xstrdup_printf("custom_ptr(%p)", value);
	}
	return NULL;
}

static void _dump_entity_data(void* item, void* arg)
{
	entity_data_t* data = (entity_data_t*)item;
	FILE* fdump = (FILE*)arg;
	layouts_keydef_t* keydef;
	char* data_dump;

	xassert(data);
	keydef = xhash_get(mgr->keydefs, data->key);
	xassert(keydef);
	data_dump = _dump_data_key(keydef, data->value);

	fprintf(fdump, "data %s (type: %d): %s\n",
		data->key, keydef->type, data_dump);

	xfree(data_dump);
}

static void _dump_entities(void* item, void* arg)
{
	entity_t* entity = (entity_t*)item;
	FILE* fdump = (FILE*)arg;
	fprintf(fdump, "-- entity %s --\n", entity->name);
	fprintf(fdump, "type: %s\nnode count: %d\nptr: %p\n",
		entity->type, list_count(entity->nodes), entity->ptr);
	xhash_walk(entity->data, _dump_entity_data, fdump);
}

static uint8_t _dump_layout_tree(xtree_node_t* node, uint8_t which,
				 uint32_t level, void* arg)
{
	FILE* fdump = (FILE*)arg;
	entity_t* e;
	if (which != XTREE_PREORDER && which != XTREE_LEAF) {
		return 1;
	}
	e = xtree_node_get_data(node);
	if (!e) {
		fprintf(fdump, "NULL_entity\n");
	}
	else {
		fprintf(fdump, "%*s%s\n", level, " ", e->name);
	}
	return 1;
}

static void _dump_layouts(void* item, void* arg)
{
	layout_t* layout = (layout_t*)item;
	FILE* fdump = (FILE*)arg;
	fprintf(fdump, "-- layout %s --\n"
		"type: %s\n"
		"priority: %u\n"
		"struct_type: %d\n"
		"relational ptr: %p\n",
		layout->name,
		layout->type,
		layout->priority,
		layout->struct_type,
		layout->tree);
	switch(layout->struct_type) {
	case LAYOUT_STRUCT_TREE:
		fprintf(fdump, "struct_type(string): tree, count: %d\n"
			"entities list:\n",
			xtree_get_count(layout->tree));
		xtree_walk(layout->tree, NULL, 0, XTREE_LEVEL_MAX,
			   _dump_layout_tree, fdump);
		break;
	}
}
#endif

/*****************************************************************************\
 *                             SLURM LAYOUTS API                             *
\*****************************************************************************/

int slurm_layouts_init(void)
{
	int i = 0;
	uint32_t layouts_count;

	debug3("layouts: slurm_layouts_init()...");

	if (mgr->plugins) {
		return SLURM_SUCCESS;
	}

	slurm_mutex_lock(&layouts_mgr.lock);

	layouts_mgr_init(&layouts_mgr);
	layouts_count = list_count(layouts_mgr.layouts_desc);
	if (layouts_count == 0)
		info("layouts: no layout to initialize");
	else
		info("layouts: %d layout(s) to initialize", layouts_count);

	mgr->plugins = xmalloc(sizeof(layout_plugin_t) * layouts_count);
	list_for_each(layouts_mgr.layouts_desc,
			_slurm_layouts_init_layouts_walk_helper,
			&i);
	mgr->plugins_count = i;

	if (mgr->plugins_count != layouts_count) {
		error("layouts: only %d/%d layouts loaded, aborting...",
		      mgr->plugins_count, layouts_count);
		for (i = 0; i < mgr->plugins_count; i++) {
			_layout_plugins_destroy(&mgr->plugins[i]);
		}
		xfree(mgr->plugins);
		mgr->plugins = NULL;
	} else if (layouts_count > 0) {
		info("layouts: slurm_layouts_init done : %d layout(s) "
		     "initialized", layouts_count);
	}

	slurm_mutex_unlock(&layouts_mgr.lock);

	return mgr->plugins_count == layouts_count ?
		SLURM_SUCCESS : SLURM_ERROR;
}

int slurm_layouts_fini(void)
{
	int i;

	debug3("layouts: slurm_layouts_fini()...");

	slurm_mutex_lock(&mgr->lock);

	for (i = 0; i < mgr->plugins_count; i++) {
		_layout_plugins_destroy(&mgr->plugins[i]);
	}
	xfree(mgr->plugins);
	mgr->plugins = NULL;
	mgr->plugins_count = 0;

	layouts_mgr_free(mgr);

	slurm_mutex_unlock(&mgr->lock);

	info("layouts: all layouts are now unloaded.");

	return SLURM_SUCCESS;
}

int slurm_layouts_load_config(void)
{
	int i, rc, inx;
	struct node_record *node_ptr;
	layout_t *layout;
	uint32_t layouts_count;
	entity_t *entity;
	void *ptr;

	info("layouts: loading entities/relations information");
	rc = SLURM_SUCCESS;

	slurm_mutex_lock(&mgr->lock);
	if (xhash_count(layouts_mgr.entities)) {
		slurm_mutex_unlock(&mgr->lock);
		return rc;
	}

	/*
	 * create a base layout to contain the configured nodes
	 * Notes : it might be moved to its own external layout in the
	 * slurm source layouts directory.
	 */
	layout = (layout_t*) xmalloc(sizeof(layout_t));
	layout_init(layout, "slurm", "base", 0, LAYOUT_STRUCT_TREE);
	if (xtree_add_child(layout->tree, NULL, NULL, XTREE_APPEND) == NULL) {
		error("layouts: unable to create base layout tree root"
		      ", aborting");
		goto exit;
	}

	/*
	 * generate and store the slurm node entities,
	 * add them to the base layout at the same time
	 */
	for (inx = 0, node_ptr = node_record_table_ptr; inx < node_record_count;
	     inx++, node_ptr++) {
		xassert (node_ptr->magic == NODE_MAGIC);
		xassert (node_ptr->config_ptr->magic == CONFIG_MAGIC);

		/* init entity structure on the heap */
		entity = (entity_t*) xmalloc(sizeof(struct entity_st));
		entity_init(entity, node_ptr->name, 0);
		entity->ptr = node_ptr;

		/* add to mgr entity hashtable */
		if (xhash_add(layouts_mgr.entities,(void*)entity) == NULL) {
			error("layouts: unable to add entity of node %s"
			      "in the hashtable, aborting", node_ptr->name);
			entity_free(entity);
			xfree(entity);
			rc = SLURM_ERROR;
			break;
		}

		/* add to the base layout (storing a callback ref to the
		 * layout node pointing to it) */
		ptr = xtree_add_child(layout->tree, layout->tree->root,
				      (void*)entity, XTREE_APPEND);
		if (!ptr) {
			error("layouts: unable to add entity of node %s"
			      "in the hashtable, aborting", node_ptr->name);
			entity_free(entity);
			xfree(entity);
			rc = SLURM_ERROR;
			break;
		} else {
			debug3("layouts: loading node %s", node_ptr->name);
			entity_add_node(entity, layout, ptr);
		}
	}
	debug("layouts: %d/%d nodes in hash table, rc=%d",
	      xhash_count(layouts_mgr.entities), node_record_count, rc);

	if (rc != SLURM_SUCCESS)
		goto exit;

	/* add the base layout to the layouts manager dedicated hashtable */
	if (xhash_add(layouts_mgr.layouts, (void*)layout) == NULL) {
		error("layouts: unable to add base layout into the hashtable");
		layout_free(layout);
		rc = SLURM_ERROR;
	}

	/* check that we get as many layouts as initiliazed plugins
	 * as layouts are added and referenced by type.
	 * do +1 in the operation as the base layout is currently managed
	 * separately.
	 * If this base layout is moved to a dedicated plugin and automatically
	 * added to the mgr layouts_desc at init, the +1 will have to be
	 * removed here as it will be counted as the other plugins in the sum
	 */
	layouts_count = xhash_count(layouts_mgr.layouts);
	if ( layouts_count != mgr->plugins_count + 1) {
		error("layouts: %d/%d layouts added to hashtable, aborting",
		      layouts_count, mgr->plugins_count+1);
		rc = SLURM_ERROR;
	}

exit:
	if (rc != SLURM_SUCCESS) {
		layout_free(layout);
		xfree(layout);
	} else {
		debug("layouts: loading stage 1");
		for (i = 0; i < mgr->plugins_count; ++i) {
			debug3("layouts: reading config for %s",
			       mgr->plugins[i].name);
			if (_layouts_read_config(&mgr->plugins[i]) !=
			    SLURM_SUCCESS) {
				rc = SLURM_ERROR;
				break;
			}
		}
		debug("layouts: loading stage 2");
		for (i = 0; i < mgr->plugins_count; ++i) {
			debug3("layouts: creating relations for %s",
			       mgr->plugins[i].name);
			if (_layouts_build_relations(&mgr->plugins[i]) !=
			    SLURM_SUCCESS) {
				rc = SLURM_ERROR;
				break;
			}
		}
	}

/*
 * For debug purposes, print the layout mgr internal states
 * in a file after the load.
 */
#if 0
	/* temporary section to test layouts */
	FILE* fdump = fopen("/tmp/slurm-layouts-dump.txt", "wb");

	xhash_walk(mgr->entities, _dump_entities, fdump);
	xhash_walk(mgr->layouts,  _dump_layouts,  fdump);

	if (fdump)
		fclose(fdump);
#endif

	slurm_mutex_unlock(&mgr->lock);

	return rc;
}

layout_t* slurm_layouts_get_layout(const char* type)
{
	layout_t* layout = (layout_t*)xhash_get(mgr->layouts, type);
	return layout;
}

entity_t* slurm_layouts_get_entity(const char* name)
{
	entity_t* e = (entity_t*)xhash_get(mgr->entities, name);
	return e;
}
