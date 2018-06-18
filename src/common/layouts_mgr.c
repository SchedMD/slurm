/*****************************************************************************\
 *  layouts_mgr.c - layouts manager data structures and main functions
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

#include <ctype.h>
#include <pthread.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include "layouts_mgr.h"

#include "slurm/slurm.h"
#include "slurm/slurm_errno.h"
#include "src/common/entity.h"
#include "src/common/layout.h"
#include "src/common/hostlist.h"
#include "src/common/list.h"
#include "src/common/node_conf.h"
#include "src/common/pack.h"
#include "src/common/plugin.h"
#include "src/common/read_config.h"
#include "src/common/slurm_protocol_api.h"
#include "src/common/strlcpy.h"
#include "src/common/timers.h"
#include "src/common/xstring.h"
#include "src/common/xtree.h"
#include "src/common/xmalloc.h"

#define PATHLEN 256

/* use to specify which layout callbacks to perform while loading data
 * from conf files, state files or input buffers */
#define CONF_DONE       0x00000001
#define PARSE_ENTITY    0x00000002
#define UPDATE_DONE     0x00000004
#define PARSE_RELATIONS 0x00000008

/*****************************************************************************\
 *                            STRUCTURES AND TYPES                           *
\*****************************************************************************/

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
	int (*update_done) (layout_t* layout, entity_t** e_array,
			    int e_cnt);
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
	"layouts_p_update_done",
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

static void _layout_plugins_destroy(layout_plugin_t *lp)
{
	plugin_context_destroy(lp->context);
	/* it might be interesting to also dlclose the ops here */
	xfree(lp->name);
	xfree(lp->ops);
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
	char*			key; /* lower case key prefixed by the
					"%layout_type%." string */
	char*			shortkey; /* original key as defined in
					     the layout keys definition */
	layouts_keydef_types_t	type;
	uint32_t                flags;
	void			(*custom_destroy)(void* value);
	char*			(*custom_dump)(void* value);
	layout_plugin_t*	plugin;
	char*			ref_key; /* lower case reference key prefixed by
					    the "%layout_type%." might be NULL 
					    if not defined. */
	char*			ref_shortkey; /* original ref key as defined in
						 the layout keys definition,
						 might be null too. */

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
	bool	init_done;	/* Set if memory allocated for arrays/List */
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
static layouts_mgr_t layouts_mgr = {PTHREAD_MUTEX_INITIALIZER, false};
static layouts_mgr_t* mgr = &layouts_mgr;

/*****************************************************************************\
 *                                  HELPERS                                  *
\*****************************************************************************/

/* entities added to the layouts mgr hash table come from the heap,
 * this function will help to free them while freeing the hash table */
static void _entity_free(void* item)
{
	entity_t* entity = (entity_t*) item;
	entity_free(entity);
	xfree(entity);
}

/* layouts added to the layouts mgr hash table come from the heap,
 * this function will help to free them while freeing the hash table */
static void _layout_free(void* item)
{
	layout_t* layout = (layout_t*) item;
	layout_free(layout);
	xfree(layout);
}

/* keydef added to the layouts mgr hash table come from the heap,
 * this function will help to free them while freeing the hash table */
static void _layouts_keydef_free(void* x)
{
	layouts_keydef_t* keydef = (layouts_keydef_t*)x;
	xfree(keydef->key);
	xfree(keydef->shortkey);
	xfree(keydef->ref_key);
	xfree(keydef->ref_shortkey);
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

static char* _trim(char* str)
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
		if (!xstrcmp(str, *strings))
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
		strlcpy(buffer, keytmp, size);
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
	strlcpy(buffer, "mgr.", size);
	_normalize_keydef_keycore(buffer, size, key, plugtype, true);
}

static void _entity_add_data(entity_t* e, const char* key, void* data)
{
	layouts_keydef_t* hkey = xhash_get(mgr->keydefs, key);
	xassert(hkey);
	void (*freefunc)(void* p) = xfree_as_callback;
	if (hkey && hkey->type == L_T_CUSTOM) {
		freefunc = hkey->custom_destroy;
	}
	entity_set_data_ref(e, hkey->key, data, freefunc);
}

/*
 * used in both automerge and autoupdate calls when dealing with
 * advanced operations (SUM,MIN,MAX,AVG,...) while setting new key values
 */
#define _entity_update_kv_helper(type_t, operator)			\
	type_t* lvalue = (type_t*) oldvalue;				\
	type_t* rvalue = (type_t*) value;				\
	uint32_t* divider;						\
	switch (operator) {						\
	case S_P_OPERATOR_SET:						\
		*lvalue = *rvalue;					\
		break;							\
	case S_P_OPERATOR_ADD:						\
		*lvalue += *rvalue;					\
		break;							\
	case S_P_OPERATOR_SUB:						\
		*lvalue -= *rvalue;					\
		break;							\
	case S_P_OPERATOR_MUL:						\
		*lvalue *= *rvalue;					\
		break;							\
	case S_P_OPERATOR_DIV:						\
		if (*rvalue != (type_t) 0)				\
			*lvalue /= *rvalue;				\
		else {							\
			error("layouts: entity_update: "		\
			      "key=%s val=0 operator="			\
			      "DIV !! skipping !!",			\
			      keydef->key);				\
		}							\
		break;							\
	case S_P_OPERATOR_AVG:						\
		divider = (uint32_t*) value;				\
		if (*divider != (uint32_t) 0)				\
			*lvalue /= (type_t) *divider;			\
		else {							\
			error("layouts: entity_update: "		\
			      "key=%s val=0 operator="			\
			      "AVG !! skipping !!",			\
			      keydef->key);				\
		}							\
		break;							\
	case S_P_OPERATOR_SET_IF_MIN:					\
		if (*rvalue < *lvalue)					\
			*lvalue = *rvalue;				\
		break;							\
	case S_P_OPERATOR_SET_IF_MAX:					\
		if (*rvalue > *lvalue)					\
			*lvalue = *rvalue;				\
		break;							\
	default:							\
		break;							\
	}

static int _layouts_autoupdate_layout(layout_t* layout);
static int _layouts_autoupdate_layout_if_allowed(layout_t* layout);

/*****************************************************************************\
 *                       LAYOUTS INTERNAL LOCKLESS API                       *
\*****************************************************************************/

layouts_keydef_t* _layouts_entity_get_kv_keydef(layout_t* l, entity_t* e,
						char* key)
{
	char keytmp[PATHLEN];
	if (l == NULL || e == NULL || key == NULL)
		return NULL;
	_normalize_keydef_key(keytmp, PATHLEN, key, l->type);
	return xhash_get(mgr->keydefs, keytmp);
}

int _layouts_entity_get_kv_type(layout_t* l, entity_t* e, char* key)
{
	layouts_keydef_t* keydef;
	keydef = _layouts_entity_get_kv_keydef(l, e, key);
	if (keydef != NULL) {
		return keydef->type;
	}
	return SLURM_ERROR;
}

int _layouts_entity_get_kv_flags(layout_t* l, entity_t* e, char* key)
{
	layouts_keydef_t* keydef;
	keydef = _layouts_entity_get_kv_keydef(l, e, key);
	if (keydef != NULL) {
		return keydef->flags;
	}
	return SLURM_ERROR;
}

int _layouts_entity_get_kv_size(layout_t* l, entity_t* e, char* key, size_t *size)
{
	layouts_keydef_t* keydef;
	keydef = _layouts_entity_get_kv_keydef(l, e, key);
	if (keydef != NULL) {
		switch(keydef->type) {
		case L_T_ERROR:
			return SLURM_ERROR;
		case L_T_STRING:
			*size = sizeof(void*);
			break;
		case L_T_CUSTOM:
			*size = sizeof(void*);
			break;
		case L_T_LONG:
			*size = sizeof(long);
			break;
		case L_T_UINT16:
			*size = sizeof(uint16_t);
			break;
		case L_T_UINT32:
			*size = sizeof(uint32_t);
			break;
		case L_T_BOOLEAN:
			*size = sizeof(bool);
			break;
		case L_T_FLOAT:
			*size = sizeof(float);
			break;
		case L_T_DOUBLE:
			*size = sizeof(double);
			break;
		case L_T_LONG_DOUBLE:
			*size = sizeof(long double);
			break;
		}
	} else
		return SLURM_ERROR;
	return SLURM_SUCCESS;
}

bool _layouts_entity_check_kv_keytype(layout_t* l, entity_t* e, char* key,
				      layouts_keydef_types_t key_type)
{
	layouts_keydef_types_t real_type;
	if (l == NULL || e == NULL || key == NULL)
		return SLURM_ERROR;
	if (key_type) {
		real_type = _layouts_entity_get_kv_type(l, e, key);
		return (real_type == key_type);
	}
	/* no key type provided, consider that as a no-check request */
	return true;
}

int _layouts_entity_push_kv(layout_t* l, entity_t* e, char* key)
{
	/* a more advanced implementation should only pull what is necessary
	 * instead of forcing a full autoupdate */
	return _layouts_autoupdate_layout_if_allowed(l);
}

int _layouts_entity_pull_kv(layout_t* l, entity_t* e, char* key)
{
	/* a more advanced implementation should only pull what is necessary
	 * instead of forcing a full autoupdate */
	return _layouts_autoupdate_layout_if_allowed(l);
}

int _layouts_entity_set_kv(layout_t* l, entity_t* e, char* key, void* value,
			   layouts_keydef_types_t key_type)
{
	void* data;
	size_t size;
	layouts_keydef_types_t real_type;
	char key_keydef[PATHLEN];

	if (l == NULL || e == NULL || key == NULL || value == NULL)
		return SLURM_ERROR;

	real_type = _layouts_entity_get_kv_type(l, e, key);
	if (key_type > 0 && real_type != key_type)
		return SLURM_ERROR;

	_normalize_keydef_key(key_keydef, PATHLEN, key, l->type);

	switch(real_type) {
	case L_T_ERROR:
		return SLURM_ERROR;
	case L_T_STRING:
		data = xstrdup(value);
		return entity_set_data_ref(e, key_keydef, data,
					   xfree_as_callback);
	case L_T_CUSTOM:
		/* TBD : add a custom_set call */
		value = NULL;
		return SLURM_ERROR;
	case L_T_LONG:
		size = sizeof(long);
		break;
	case L_T_UINT16:
		size = sizeof(uint16_t);
		break;
	case L_T_UINT32:
		size = sizeof(uint32_t);
		break;
	case L_T_BOOLEAN:
		size = sizeof(bool);
		break;
	case L_T_FLOAT:
		size = sizeof(float);
		break;
	case L_T_DOUBLE:
		size = sizeof(double);
		break;
	case L_T_LONG_DOUBLE:
		size = sizeof(long double);
		break;
	default:
		value = NULL;
		return SLURM_ERROR;
	}
	return entity_set_data(e, key_keydef, value, size);
}

int _layouts_entity_set_kv_ref(layout_t* l, entity_t* e, char* key, void* value,
			       layouts_keydef_types_t key_type)
{
	int rc = SLURM_ERROR;
	char key_keydef[PATHLEN];

	if (l == NULL || e == NULL || key == NULL || value == NULL)
		return rc;

	if (!_layouts_entity_check_kv_keytype(l, e, key, key_type))
		return rc;

	_normalize_keydef_key(key_keydef, PATHLEN, key, l->type);
	return entity_set_data_ref(e, key_keydef, value, xfree_as_callback);
}

int _layouts_entity_setpush_kv(layout_t* l, entity_t* e, char* key, void* value,
			       layouts_keydef_types_t key_type)
{
	int rc = SLURM_ERROR;
	if (_layouts_entity_set_kv(l, e, key, value, key_type) == SLURM_SUCCESS)
		rc = _layouts_entity_push_kv(l, e, key);
	return rc;
}

int _layouts_entity_setpush_kv_ref(layout_t* l, entity_t* e, char* key,
				   void* value, layouts_keydef_types_t key_type)
{
	int rc = SLURM_ERROR;
	if (_layouts_entity_set_kv_ref(l, e, key, value, key_type) ==
	    SLURM_SUCCESS)
		rc = _layouts_entity_push_kv(l, e, key);
	return rc;
}

int _layouts_entity_get_kv(layout_t* l, entity_t* e, char* key, void* value,
			   layouts_keydef_types_t key_type)
{
	void* data;
	size_t size;
	layouts_keydef_types_t real_type;
	char key_keydef[PATHLEN];
	char ** pstr;

	if (l == NULL || e == NULL || key == NULL || value == NULL)
		return SLURM_ERROR;

	real_type = _layouts_entity_get_kv_type(l, e, key);
	if (key_type > 0 && real_type != key_type)
		return SLURM_ERROR;

	_normalize_keydef_key(key_keydef, PATHLEN, key, l->type);

	data = entity_get_data_ref(e, key_keydef);
	if (data == NULL) {
		return SLURM_ERROR;
	}

	switch(real_type) {
	case L_T_STRING:
		pstr = (char**) value;
		*pstr = xstrdup(data);
		return SLURM_SUCCESS;
	case L_T_CUSTOM:
		/* TBD : add a custom_get call */
		pstr = (char**) value;
		*pstr = NULL;
		return SLURM_ERROR;
	case L_T_LONG:
		size = sizeof(long);
		break;
	case L_T_UINT16:
		size = sizeof(uint16_t);
		break;
	case L_T_UINT32:
		size = sizeof(uint32_t);
		break;
	case L_T_BOOLEAN:
		size = sizeof(bool);
		break;
	case L_T_FLOAT:
		size = sizeof(float);
		break;
	case L_T_DOUBLE:
		size = sizeof(double);
		break;
	case L_T_LONG_DOUBLE:
		size = sizeof(long double);
		break;
	case L_T_ERROR:
	default:
		return SLURM_ERROR;
	}
	memcpy(value, data, size);
	return SLURM_SUCCESS;
}

int _layouts_entity_get_mkv(layout_t* l, entity_t* e, char* keys, void* value,
			    size_t length, layouts_keydef_types_t key_type)
{
	char *key = NULL;
	hostlist_t kl;
	size_t processed = 0;
	size_t elt_size = sizeof(void*);;
	int rc = 0;

	/* expand in order the requested keys (in hostlist format)
	 * and iterate over each one of them, collecting the different
	 * values into the provided buffer.
	 * if no more space is available in the buffer, then just count
	 * the missing elements for the exit code.
	 * the first error encountered fakes a full buffer to just add
	 * the remaining keys to the missing elements count before
	 * exiting. */
	kl = hostlist_create(keys);
	while ((key = hostlist_shift(kl))) {
		if (processed >= length) {
			rc++;
		} else if (_layouts_entity_get_kv_size(l, e, key, &elt_size) ||
			   (processed + elt_size) > length ||
			   _layouts_entity_get_kv(l, e, key, value, key_type)) {
			rc++;
			processed = length;
		} else {
			value += elt_size;
			processed += elt_size;
		}
		free(key);
	}
	hostlist_destroy(kl);

	return rc;
}

int _layouts_entity_get_kv_ref(layout_t* l, entity_t* e,
			       char* key, void** value,
			       layouts_keydef_types_t key_type)
{
	int rc = SLURM_ERROR;
	char key_keydef[PATHLEN];
	void* data;

	if (l == NULL || e == NULL || key == NULL || value == NULL)
		return rc;

	if (!_layouts_entity_check_kv_keytype(l, e, key, key_type))
		return rc;

	_normalize_keydef_key(key_keydef, PATHLEN, key, l->type);
	data = entity_get_data_ref(e, key_keydef);
	if (data != NULL) {
		*value = data;
		rc = SLURM_SUCCESS;
	}
	return rc;
}

int _layouts_entity_get_mkv_ref(layout_t* l, entity_t* e, char* keys,
				void* value, size_t length,
				layouts_keydef_types_t key_type)
{
	char *key = NULL;
	hostlist_t kl;
	size_t processed = 0;
	size_t elt_size = sizeof(void*);
	int rc = 0;

	/* expand in order the requested keys (in hostlist format)
	 * and iterate over each one of them, collecting the different
	 * references into the provided buffer.
	 * if no more space is available in the buffer, then just count
	 * the missing elements for the exit code.
	 * the first error encountered fakes a full buffer to just add
	 * the remaining keys to the missing elements count before
	 * exiting. */
	kl = hostlist_create(keys);
	while ((key = hostlist_shift(kl))) {
		if (processed >= length) {
			rc++;
		} else if (_layouts_entity_get_kv_ref(l, e, key, value, key_type)) {
			rc++;
			processed = length;
		} else {
			value += elt_size;
			processed += elt_size;
		}
		free(key);
	}
	hostlist_destroy(kl);

	return rc;
}

int _layouts_entity_pullget_kv(layout_t* l, entity_t* e, char* key, void* value,
			       layouts_keydef_types_t key_type)
{
	int rc = SLURM_ERROR;
	if (!_layouts_entity_check_kv_keytype(l, e, key, key_type))
		return rc;
	if (_layouts_entity_pull_kv(l, e, key) == SLURM_SUCCESS)
		rc = _layouts_entity_get_kv(l, e, key, value, key_type);
	return rc;
}

int _layouts_entity_pullget_kv_ref(layout_t* l, entity_t* e,
				   char* key, void** value,
				   layouts_keydef_types_t key_type)
{
	int rc = SLURM_ERROR;
	if (!_layouts_entity_check_kv_keytype(l, e, key, key_type))
		return rc;
	if (_layouts_entity_pull_kv(l, e, key) == SLURM_SUCCESS)
		rc = _layouts_entity_get_kv_ref(l, e, key, value, key_type);
	return rc;
}

/*****************************************************************************\
 *                                MANAGER INIT                               *
\*****************************************************************************/

static void _layouts_init_keydef(xhash_t* keydefs,
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
		nkeydef->shortkey = xstrdup(current->key);
		nkeydef->type = current->type;
		nkeydef->flags = current->flags;
		nkeydef->custom_destroy = current->custom_destroy;
		nkeydef->custom_dump = current->custom_dump;
		nkeydef->plugin = plugin;
		if (current->ref_key != NULL) {
			_normalize_keydef_key(keytmp, PATHLEN, current->ref_key,
					      plugin->layout->type);
			nkeydef->ref_key = xstrdup(keytmp);
			nkeydef->ref_shortkey = xstrdup(current->ref_key);
		} else {
			nkeydef->ref_key = NULL;
			nkeydef->ref_shortkey = NULL;
		}
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
		nkeydef->shortkey = xstrdup("Enclosed");
		nkeydef->type = L_T_STRING;
		nkeydef->plugin = plugin;
		xhash_add(keydefs, nkeydef);
		break;
	}
}

static void _debug_output_keydefs (void* item, void* args)
{
	layouts_keydef_t* keydef = (layouts_keydef_t*) item;
	debug3("layouts/keydefs: loaded: %s flags=0x%08lx refkey=%s",
	       keydef->key, (long unsigned int) keydef->flags,
	       (keydef->ref_key == NULL) ? "-":keydef->ref_key);

}

static int _layouts_init_layouts_walk_helper(void* x, void* arg)
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
	if ((inserted_item = xhash_add(mgr->layouts, plugin->layout)))
		xassert(inserted_item == plugin->layout);
	_layouts_init_keydef(mgr->keydefs,
			     plugin->ops->spec->keyspec,
			     plugin);
	xhash_walk(mgr->keydefs, _debug_output_keydefs, NULL);
	++*i;
	return SLURM_SUCCESS;
}

static void _layouts_mgr_parse_global_conf(layouts_mgr_t* mgr)
{
	char* layouts;
	char* parser;
	char* saveptr = NULL;
	char* slash;
	layouts_conf_spec_t* nspec;

	mgr->layouts_desc = list_create(layouts_conf_spec_free);
	layouts = slurm_get_layouts();
	parser = strtok_r(layouts, ",", &saveptr);
	while (parser) {
		nspec = (layouts_conf_spec_t*)xmalloc(
			sizeof(layouts_conf_spec_t));
		nspec->whole_name = xstrdup(_trim(parser));
		slash = strchr(parser, '/');
		if (slash) {
			*slash = 0;
			nspec->type = xstrdup(_trim(parser));
			nspec->name = xstrdup(_trim(slash+1));
		} else {
			nspec->type = xstrdup(_trim(parser));
			nspec->name = xstrdup("default");
		}
		list_append(mgr->layouts_desc, nspec);
		parser = strtok_r(NULL, ",", &saveptr);
	}
	xfree(layouts);
}

static void _layouts_mgr_free(layouts_mgr_t* mgr)
{
	/* free the configuration details */
	FREE_NULL_LIST(mgr->layouts_desc);

	/* FIXME: can we do a faster free here? since each node removal will
	 * modify either the entities or layouts for back (or forward)
	 * references. */
	xhash_free(mgr->layouts);
	xhash_free(mgr->entities);
	xhash_free(mgr->keydefs);
	mgr->init_done = false;
}

static void _layouts_mgr_init(layouts_mgr_t* mgr)
{
	if (mgr->init_done)
		_layouts_mgr_free(mgr);
	mgr->init_done = true;
	_layouts_mgr_parse_global_conf(mgr);
	mgr->layouts = xhash_init(layout_hashable_identify_by_type,
				  _layout_free);
	mgr->entities = xhash_init(entity_hashable_identify,
				   _entity_free);
	mgr->keydefs = xhash_init(layouts_keydef_idfunc,
				  _layouts_keydef_free);
}

/*****************************************************************************\
 *                               CONFIGURATION                               *
\*****************************************************************************/

static char* _conf_get_filename(const char* type)
{
	char path[PATHLEN];
	char* final_path;
	strlcpy(path, "layouts.d/", PATHLEN);
	_cat(path, type, PATHLEN);
	_cat(path, ".conf", PATHLEN);
	final_path = get_extra_conf_path(path);
	return final_path;
}

static char* _state_get_filename(const char* type)
{
	return xstrdup_printf("%s/layouts_state_%s",
			      slurmctld_conf.state_save_location,
			      type);
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
		{"Enclosed", S_P_STRING},
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
		fatal("layouts: does not know what relation structure to "
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

#define _layouts_load_merge(type_t, s_p_get_type) {			\
		type_t  rvalue;						\
		type_t* value = &rvalue;				\
		type_t* oldvalue;					\
		slurm_parser_operator_t operator = S_P_OPERATOR_SET;	\
		if (!s_p_get_type(&rvalue, option_key, etbl)) {		\
			/* no value to merge/create */			\
			continue;					\
		}							\
		s_p_get_operator(&operator, option_key, etbl);		\
		oldvalue = (type_t*)entity_get_data_ref(e, key_keydef); \
		if (oldvalue) {						\
			_entity_update_kv_helper(type_t, operator);	\
		} else {						\
			type_t* newalloc = (type_t*)			\
				xmalloc(sizeof(type_t));		\
			*newalloc = *value;				\
			_entity_add_data(e, key_keydef, newalloc);	\
		}							\
	}								\

#define _layouts_merge_check(type1, type2)			\
	(entity_option->type == type1 && keydef->type == type2)

static void _layouts_load_automerge(layout_plugin_t* plugin, entity_t* e,
				    s_p_hashtbl_t* etbl, uint32_t flags)
{
	const s_p_options_t* layout_option;
	const s_p_options_t* entity_option;
	layouts_keydef_t* keydef;
	char key_keydef[PATHLEN];
	char* option_key;

	for (layout_option = plugin->ops->spec->options;
	     layout_option && xstrcasecmp("Entity", layout_option->key);
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
		/* do not perform automerge on updates for read-only keys */
		if (flags & UPDATE_DONE &&
		    keydef->flags & KEYSPEC_RDONLY) {
			debug4("layouts: do not try to merge RDONLY key '%s'",
			       keydef->key);
			continue;
		}
		if (_layouts_merge_check(S_P_LONG, L_T_LONG)) {
			_layouts_load_merge(long, s_p_get_long);
		} else if (_layouts_merge_check(S_P_UINT16, L_T_UINT16)) {
			_layouts_load_merge(uint16_t, s_p_get_uint16);
		} else if (_layouts_merge_check(S_P_UINT32, L_T_UINT32)) {
			_layouts_load_merge(uint32_t, s_p_get_uint32);
		} else if (_layouts_merge_check(S_P_BOOLEAN, L_T_BOOLEAN)) {
			bool newvalue;
			if (s_p_get_boolean(&newvalue, option_key, etbl)) {
				bool *newalloc = xmalloc(sizeof(bool));
				*newalloc = newvalue;
				_entity_add_data(e, key_keydef, newalloc);
			}
		} else if (_layouts_merge_check(S_P_LONG, L_T_LONG)) {
			_layouts_load_merge(long, s_p_get_long);
		} else if (_layouts_merge_check(S_P_FLOAT, L_T_FLOAT)) {
			_layouts_load_merge(float, s_p_get_float);
		} else if (_layouts_merge_check(S_P_DOUBLE, L_T_DOUBLE)) {
			_layouts_load_merge(double, s_p_get_double);
		} else if (_layouts_merge_check(S_P_LONG_DOUBLE,
						L_T_LONG_DOUBLE)) {
			_layouts_load_merge(long double, s_p_get_long_double);
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
	char* e_already_enclosed;
	char* e_new_enclosed;
	char key[PATHLEN];
	switch(plugin->layout->struct_type) {
	case LAYOUT_STRUCT_TREE:
		if (s_p_get_string(&e_enclosed, "Enclosed", entity_tbl)) {
			_normalize_keydef_mgrkey(key, PATHLEN, "enclosed",
						 plugin->layout->type);
			e_already_enclosed = (char*)
				entity_get_data_ref(e, key);
			if (e_already_enclosed) {
				e_new_enclosed = (char*) xmalloc(
					strlen(e_already_enclosed) +
					strlen(e_enclosed) + 2);
				strcat(e_new_enclosed, e_already_enclosed);
				strcat(e_new_enclosed, ",");
				strcat(e_new_enclosed, e_enclosed);
				xfree(e_enclosed);
				e_enclosed = e_new_enclosed;
			}
			_entity_add_data(e, key, e_enclosed);
		}
		break;
	}
}

static int _layouts_read_config_post(layout_plugin_t* plugin,
				     s_p_hashtbl_t* tbl)
{
	char* root_nodename;
	entity_t* e;
	entity_node_t* enode;
	xtree_node_t* root_node;
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
		e = xhash_get(mgr->entities, _trim(root_nodename));
		if (!e) {
			error("layouts: unable to find specified root "
			      "entity `%s'", _trim(root_nodename));
			xfree(root_nodename);
			return SLURM_ERROR;
		}
		xfree(root_nodename);

		if (!(enode = entity_add_node(e, plugin->layout)))
			xassert(enode);
		if (!(root_node = xtree_add_child(
			      tree, NULL, enode, XTREE_APPEND)))
			xassert(root_node);
		enode->node = (void*) root_node;
		break;
	}
	return SLURM_SUCCESS;
}

/*
 * _layouts_load_config_common - called by layouts_read_config,
 *       layouts_read_state or layouts_update_config with either a
 *       filename or a buffer as well as a flag to indicate if it
 *       is a full load or not (state save only)
 */
static int _layouts_load_config_common(layout_plugin_t* plugin,
				       char* filename, Buf buffer,
				       uint32_t flags)
{
	s_p_hashtbl_t* tbl = NULL;
	s_p_hashtbl_t** entities_tbl = NULL;
	s_p_hashtbl_t* entity_tbl = NULL;
	int entities_tbl_count = 0, i;
	entity_t** updated_entities = NULL;
	int rc = SLURM_SUCCESS;

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
	if (filename) {
		if (s_p_parse_file(tbl, NULL, filename, false) == SLURM_ERROR) {
			info("layouts: something went wrong when opening/reading "
			      "'%s': %m", filename);
			rc = SLURM_ERROR;
			goto cleanup;
		}
		debug3("layouts: configuration file '%s' is loaded", filename);
	} else if (buffer) {
		if (s_p_parse_buffer(tbl, NULL, buffer, false) == SLURM_ERROR) {
			error("layouts: something went wrong when parsing "
			      "buffer : %m");
			rc = SLURM_ERROR;
			goto cleanup;
		}
		debug3("layouts: buffer loaded");
	} else {
		error("layouts: invalid usage of _layouts_load_config_common");
		rc = SLURM_ERROR;
		goto cleanup;
	}

	if (s_p_get_uint32(&l_priority, "Priority", tbl)) {
		plugin->layout->priority = l_priority;
	}

	/* get the config hash tables of the defined entities */
	if (!s_p_get_expline(&entities_tbl, &entities_tbl_count,
			     "Entity", tbl)) {
		error("layouts: no valid Entity found, can not append any "
		      "information nor construct relations for %s/%s",
		      plugin->layout->type, plugin->layout->name);
		rc = SLURM_ERROR;
		goto cleanup;
	}

	/* stage 0: xmalloc an array of entity_t* to save the updated entity_t
	 * and give their references in the update_done layout callback */
	updated_entities = (entity_t**)
		xmalloc(entities_tbl_count*sizeof(entity_t*));

	/* stage 1: create the described entities or update them */
	for (i = 0; i < entities_tbl_count; ++i) {
		updated_entities[i] = NULL;
		entity_tbl = entities_tbl[i];
		xfree(e_name);
		xfree(e_type);
		if (!s_p_get_string(&e_name, "Entity", entity_tbl)) {
			info("layouts: no name associated to entity[%d], "
			      "skipping...", i);
			rc = SLURM_ERROR;
			continue;
		}

		/* look for the entity in the entities hash table*/
		e = xhash_get(mgr->entities, e_name);
		if (!e) {
			/* if the entity does not already exists, create it */
			if (!s_p_get_string(&e_type, "Type", entity_tbl)) {
				info("layouts: entity '%s' does not already "
				     "exists and no type was specified, "
				     "skipping", e_name);
				rc = SLURM_ERROR;
				continue;
			}
			if (!_string_in_array(e_type,
					      plugin->ops->spec->etypes)) {
				info("layouts: entity '%s' type (%s) is "
				     "invalid, skipping", e_name, e_type);
				rc = SLURM_ERROR;
				continue;
			}

			e = (entity_t*)xmalloc(sizeof(entity_t));
			entity_init(e, e_name, e_type);
			xhash_add(mgr->entities, e);

		} else if (s_p_get_string(&e_type, "Type", entity_tbl)) {
			/* if defined, check that the type is consistent */
			if (!_string_in_array(e_type,
					      plugin->ops->spec->etypes)) {
				info("layouts: entity '%s' type (%s) is "
				     "invalid, skipping", e_name, e_type);
				rc = SLURM_ERROR;
				continue;
			}
			if (!e->type || xstrcmp(e_type, e->type)) {
				info("layouts: entity '%s' type (%s) differs "
				     "from already registered entity type (%s)"
				     " skipping", e_name, e_type, e->type);
				rc = SLURM_ERROR;
				continue;
			}
		}

		/* ** Full load config only (flags==0) **
		 * look for "Enclosed" pragmas identifying the relations
		 * among entities and kep that along with the entity for
		 * stage 2 */
		if (flags & PARSE_RELATIONS)
			_layouts_parse_relations(plugin, e, entity_tbl);

		/*
		 * if the layout plugin requests automerge, try to automatically
		 * parse the conf hash table using the s_p_option_t description
		 * of the plugin, creating the key/vlaue with the right value
		 * type and adding them to the entity key hash table.
		 */
		if (plugin->ops->spec->automerge) {
			_layouts_load_automerge(plugin, e, entity_tbl, flags);
		}

		/*
		 * in case the automerge was not sufficient, the layout parsing
		 * callback is called for further actions.
		 */
		if ((flags & PARSE_ENTITY) && plugin->ops->entity_parsing) {
			plugin->ops->entity_parsing(e, entity_tbl,
						    plugin->layout);
		}

		/* add the entity ref to the array for further usage when
		 * calling the update_done layout callback */
		updated_entities[i] = e;
	}
	xfree(e_name);
	xfree(e_type);

	/* ** Full load config only (flags==0) **
	 * post-read-and-build (post stage 1)
	 * ensure that a Root entity was defined and set it as the root of
	 * the relational structure of the layout.
	 * fails in case of error as a root is mandatory to walk the relational
	 * structure of the layout */
	if ((flags & CONF_DONE) &&
	    _layouts_read_config_post(plugin, tbl) != SLURM_SUCCESS) {
		goto cleanup;
	}

	/* ** Full load config only (flags==0) **
	 * call the layout plugin conf_done callback for further
	 * layout specific actions.
	 */
	if ((flags & CONF_DONE) && plugin->ops->conf_done) {
		if (!plugin->ops->conf_done(mgr->entities, plugin->layout,
					    tbl)) {
			error("layouts: plugin %s/%s has an error parsing its"
			      " configuration", plugin->layout->type,
			      plugin->layout->name);
			rc = SLURM_ERROR;
			goto cleanup;
		}
	}

	/*
	 * In case we are processing an update (not a startup configuration)
	 * if the layout plugin requests autoupdate, call the autoupdate
	 * function on the current layout in order to set the inherited values
	 * according to the newly modified ones.
	 * (in startup configuration, the autoupdate is performed in stage 3
	 *  when the relational structures are available)
	 */
	if ((flags & UPDATE_DONE) && plugin->ops->spec->autoupdate) {
		_layouts_autoupdate_layout(plugin->layout);
	}

	/*
	 * Call the layout plugin update_done callback for further
	 * layout specific actions.
	 * Note : some entries of the updated_entities array might be NULL
	 * reflecting an issue while trying to analyze the corresponding
	 * parsed hash table.
	 */
	if ((flags & UPDATE_DONE) && plugin->ops->update_done) {
		if (!plugin->ops->update_done(plugin->layout, updated_entities,
					      entities_tbl_count)) {
			error("layouts: plugin %s/%s has an error reacting to"
			      " entities update", plugin->layout->type,
			      plugin->layout->name);
			rc = SLURM_ERROR;
			goto cleanup;
		}
	}
	xfree(updated_entities);

cleanup:
	s_p_hashtbl_destroy(tbl);

	return rc;
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
	int rc;
	char* filename = _conf_get_filename(plugin->layout->type);
	if (!filename) {
		fatal("layouts: cannot find configuration file for "
		      "required layout '%s'", plugin->name);
	}
	rc = _layouts_load_config_common(plugin, filename, NULL,
					 CONF_DONE |
					 PARSE_ENTITY | PARSE_RELATIONS);
	xfree(filename);
	return rc;
}

/*
 * _layouts_read_state - called to restore saved state of layout entities
 *
 * This function is the stage 1.1 of the layouts loading stage, where we collect
 * info on all the entities stored in the state of the layout and store/update
 * them in the global hash table.
 *
 * Information concerning the relations among entities provided by the
 * 'Enclosed' conf pragma are not taken into account for now and will be those
 * loaded during stage 1.
 *
 * No layout plugins callbacks are called when doing that for now.
 */
static int _layouts_read_state(layout_plugin_t* plugin)
{
	int rc = SLURM_SUCCESS;
	struct stat stat_buf;
	char *filename = _state_get_filename(plugin->layout->type);
	if (!filename) {
		error("layouts: unable to build read state filename of layout"
		      " '%s/%s'", plugin->layout->type, plugin->layout->name);
		return SLURM_ERROR;
	}
	/* check availability of the file otherwise it will later block
	 * waiting for a file to appear (in s_p_parse_file) */
	if (stat(filename, &stat_buf) < 0) {
		debug("layouts: skipping non existent state file for '%s/%s'",
		      plugin->layout->type, plugin->layout->name);
	} else {
		rc = _layouts_load_config_common(plugin, filename, NULL,
						 PARSE_ENTITY);
	}
	xfree(filename);
	return rc;
}

static int _layouts_update_state(layout_plugin_t* plugin, Buf buffer)
{
	return _layouts_load_config_common(plugin, NULL, buffer,
					   PARSE_ENTITY | UPDATE_DONE);
}

typedef struct _layouts_build_xtree_walk_st {
	layout_t* layout;
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
	entity_node_t* enode;
	char* enclosed_str;
	char* enclosed_name;
	hostlist_t enclosed_hostlist;
	entity_t* enclosed_e;
	xtree_node_t* enclosed_node;

	xassert(arg);

	/* get the entity from the entity node associated with the tree node */
	enode = (entity_node_t*) xtree_node_get_data(node);
	xassert(enode);
	e = enode->entity;
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

	enclosed_str = (char*) entity_get_data_ref(e, p->enclosed_key);

	if (enclosed_str) {
		enclosed_hostlist = hostlist_create(enclosed_str);
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
			/* create an entity node associated to the entity
			 * for this layout */
			enode = entity_add_node(enclosed_e, p->layout);
			xassert(enode);
			/* add it to the tree, getting an xtree_node_t ref */
			if (!(enclosed_node = xtree_add_child(
				      p->tree, node, enode, XTREE_APPEND)))
				xassert(enclosed_node);
			/* store the xtree_node_t ref in the entity node. It
			 * will be used to access this layout tree from the
			 * entity when necessary through the entity node */
			enode->node = enclosed_node;
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
			plugin->layout,
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

/*****************************************************************************\
 *                                  STATE DUMP                               *
\*****************************************************************************/

/*
 * _pack_args_t : helper struct/type used when passing args among the various
 * functions used when packing layouts into a buffer as a set of strings.
 */
typedef struct _pack_args {
	Buf        buffer;
	char       *current_line;
	layout_t   *layout;
	hostlist_t list_entities;
	char       *type;
	uint32_t   all;
	uint32_t   flags;
	uint32_t   record_count;
} _pack_args_t;

/*
 * _pack_data_key : internal function used to get the key=val
 * string representation of a particular entity data value
 */
static char* _pack_data_key(layouts_keydef_t* keydef, void* value)
{
	char val;
	if (!keydef) {
		return NULL;
	}
	switch(keydef->type) {
	case L_T_ERROR:
		return NULL;
	case L_T_STRING:
		return xstrdup_printf("%s=%s", keydef->shortkey,
				      (char*)value);
	case L_T_LONG:
		return xstrdup_printf("%s=%ld", keydef->shortkey,
				      *(long*)value);
	case L_T_UINT16:
		return xstrdup_printf("%s=%u", keydef->shortkey,
				      *(uint16_t*)value);
	case L_T_UINT32:
		return xstrdup_printf("%s=%"PRIu32, keydef->shortkey,
				      *(uint32_t*)value);
	case L_T_BOOLEAN:
		val = *(bool*)value;
		return xstrdup_printf("%s=%s", keydef->shortkey,
				      val ? "true" : "false");
	case L_T_FLOAT:
		return xstrdup_printf("%s=%f", keydef->shortkey,
				      *(float*)value);
	case L_T_DOUBLE:
		return xstrdup_printf("%s=%f", keydef->shortkey,
				      *(double*)value);
	case L_T_LONG_DOUBLE:
		return xstrdup_printf("%s=%Lf", keydef->shortkey,
				      *(long double*)value);
	case L_T_CUSTOM:
		if (keydef->custom_dump) {
			return keydef->custom_dump(value);
		} else
			return NULL;
	}
	return NULL;
}

/*
 * _pack_entity_layout_data : internal function used to append the
 * key/value of a entity data element corresponding to the targeted
 * layout when walking an entity list of entity data elements
 *
 * - append the " %key%=%val%" to the char* received as an input arg member
 *
 */
static void _pack_entity_layout_data(void* item, void* arg)
{
	entity_data_t* data;
	_pack_args_t *pargs;

	layouts_keydef_t* keydef;
	char *data_dump;

	xassert(item);
	xassert(arg);

	data = (entity_data_t*) item;
	pargs = (_pack_args_t *) arg;

	/* the pack args must contain a valid char* to append to */
	xassert(pargs->current_line);

	/* we must be able to get the keydef associated to the data key */
	xassert(data);
	keydef = xhash_get(mgr->keydefs, data->key);
	xassert(keydef);

	/* only dump keys related to the targeted layout */
	if (!xstrncmp(keydef->plugin->layout->type, pargs->layout->type,
		      PATHLEN)) {
		data_dump = _pack_data_key(keydef, data->value);
		/* avoid printing any error in case of NULL pointer returned */
		if (data_dump) {
			xstrcat(pargs->current_line, " ");
			xstrcat(pargs->current_line, data_dump);
			xfree(data_dump);
		}
	}

	return;
}

/*
 * _pack_layout_tree : internal function used when walking a layout tree
 *
 * - print one line per entity with the following pattern :
 *  Entity=%name% [Type=%type%] [Enclosed=%childrens%] [key1=val1 ...]
 *
 * - potentially print an header line if the entity is the root like :
 *  Root=%name%
 *
 */
static uint8_t _pack_layout_tree(xtree_node_t* node, uint8_t which,
				 uint32_t level, void* arg)
{
	_pack_args_t *pargs;
	xtree_node_t* child;
	entity_node_t* enode;
	hostlist_t enclosed;
	char *enclosed_str = NULL, *e_name = NULL, *e_type = NULL;
	Buf buffer;
	char *strdump, *str = NULL;

	/* only need to work for preorder and leaf cases */
	if (which != XTREE_PREORDER && which != XTREE_LEAF) {
		return 1;
	}

	/* get the buffer we need to pack the data too */
	pargs = (_pack_args_t *) arg;
	buffer = pargs->buffer;

	/* aggregate children names to build the Enclosed=.. value */
	if (which == XTREE_PREORDER) {
		enclosed = hostlist_create(NULL);
		child = node->start;
		while (child) {
			enode = (entity_node_t*) xtree_node_get_data(child);
			if (!enode || !enode->entity) {
				hostlist_push(enclosed, "NULL");
			} else {
				hostlist_push(enclosed, enode->entity->name);
			}
			child = child->next;
		}
		hostlist_uniq(enclosed);
		if (hostlist_count(enclosed) > 0) {
			enclosed_str = hostlist_ranged_string_xmalloc(enclosed);
		}
		hostlist_destroy(enclosed);
	}

	/* get the entity associated to this xtree node */
	enode = (entity_node_t*) xtree_node_get_data(node);
	if (!enode || !enode->entity) {
		e_name = (char*) "NULL";
		e_type = NULL;
	} else {
		e_name = enode->entity->name;
		e_type = enode->entity->type;
	}

	/* print this entity as root if necessary */
	if (level == 0 && !(pargs->flags & LAYOUTS_DUMP_NOLAYOUT)
	    && pargs->type == NULL) {
		if (pargs->all != 0 ||
		    pargs->list_entities == NULL ||
		    hostlist_find(pargs->list_entities, e_name) != -1) {
			str = xstrdup_printf("Root=%s\n", e_name);
			packstr(str, buffer);
			pargs->record_count++;
			xfree(str);
		}
	}

	/* print entity name and type when possible */
	str = xstrdup_printf("Entity=%s", e_name);
	if (e_type) {
		strdump = xstrdup_printf("%s Type=%s", str, e_type);
		xfree(str);
		str = strdump;
	}

	/* add entity keys matching the layout to the current str */
	pargs->current_line = str;
	if (enode && enode->entity) {
		xhash_walk(enode->entity->data, _pack_entity_layout_data,
			   pargs);
	}
	/* the current line might have been extended/remalloced, so
	 * we need to sync it again in str for further actions */
	str = pargs->current_line;
	pargs->current_line = NULL;

	/* don't print enclosed if no_relation option */
	if ((pargs->flags & LAYOUTS_DUMP_NOLAYOUT)
	    && enclosed_str != NULL
	    && pargs->list_entities == NULL) {
		xfree(enclosed_str);
		xfree(str);
		return 1;
	}

	/* don't print non enclosed if no "entities char*" option */
	if (pargs->all == 0
	    && pargs->list_entities == NULL
	    && enclosed_str == NULL ) {
		xfree(str);
		return 1;
	}

	/* don't print entities if not named in "entities char*" */
	if (pargs->all == 0
	    && pargs->list_entities != NULL
	    && hostlist_find(pargs->list_entities, e_name) == -1) {
		xfree(str);
		return 1;
	}

	/* don't print entities if not type of "type char*" */
	if (pargs->type != NULL
	    && (e_type == NULL || xstrcasecmp(e_type, pargs->type)!=0)) {
		xfree(str);
		return 1;
	}

	/* print enclosed entities if any */
	if (!enclosed_str) {
		xstrcat(str, "\n");
	} else {
		strdump = xstrdup_printf("%s Enclosed=%s\n", str, enclosed_str);
		xfree(enclosed_str);
		xfree(str);
		str = strdump;
	}

	packstr(str, buffer);
	pargs->record_count++;
	xfree(str);

	return 1;
}

/* helper function used by layouts_save_state when walking through
 * the various layouts to save their state in Slurm state save location */
static void _state_save_layout(void* item, void* arg)
{
	layout_t* layout = (layout_t*)item;
	layouts_state_save_layout(layout->type);
}

/*****************************************************************************\
 *                            ENTITIES KVs AUTOUPDATE                        *
\*****************************************************************************/

/*
 * helper structure used when walking the tree of relational nodes in order
 * to automatically update the entities KVs based on their inheritance
 * relationships
 */
typedef struct _autoupdate_tree_args {
	entity_node_t* enode;
	uint8_t which;
	uint32_t level;
} _autoupdate_tree_args_t;

/*
 * helper function used to update a particular KV value of an entity according
 * to a particular operator looking for the right type to apply during the 
 * operation
 */
static int _autoupdate_entity_kv(layouts_keydef_t* keydef,
				 layouts_keydef_t* ref_keydef,
				 slurm_parser_operator_t operator,
				 void* oldvalue, void* value)
{
	int rc = SLURM_ERROR;

	if (keydef->type != ref_keydef->type)
		return rc;

	if (keydef->type == L_T_LONG) {
		_entity_update_kv_helper(long, operator);
	} else if (keydef->type == L_T_UINT16) {
		_entity_update_kv_helper(uint16_t, operator);
	} else if (keydef->type == L_T_UINT32) {
		_entity_update_kv_helper(uint32_t, operator);
	} else if (keydef->type == L_T_FLOAT) {
		_entity_update_kv_helper(float, operator);
	} else if (keydef->type == L_T_DOUBLE) {
		_entity_update_kv_helper(double, operator);
	} else if (keydef->type == L_T_LONG_DOUBLE) {
		_entity_update_kv_helper(long double, operator);
	} else {
		// L_T_BOOLEAN, L_T_STRING, L_T_CUSTOM not yet supported
		return rc;
	}

	return SLURM_SUCCESS;
}

/*
 * helper function used to update KVs of an entity using its xtree_node
 * looking for known inheritance in the neighborhood (parents/children) */
static void _tree_update_node_entity_data(void* item, void* arg)
{
	uint32_t action;
	entity_data_t* data;
	_autoupdate_tree_args_t *pargs;
	layouts_keydef_t* keydef;
	layouts_keydef_t* ref_keydef;
	slurm_parser_operator_t operator;
	xtree_node_t *node, *child;
	entity_node_t *enode, *cnode;
	void* oldvalue;
	void* value;
	uint32_t count;
	int setter;

	xassert(item);
	xassert(arg);

	data = (entity_data_t*) item;
	pargs = (_autoupdate_tree_args_t *) arg;
	cnode = pargs->enode;

	/* we must be able to get the keydef associated to the data key */
	xassert(data);
	keydef = xhash_get(mgr->keydefs, data->key);
	xassert(keydef);

	/* only work on keys that depend of their neighborhood */
	if (!(keydef->flags & KEYSPEC_UPDATE_CHILDREN_MASK) &&
	    !(keydef->flags & KEYSPEC_UPDATE_PARENTS_MASK)) {
		return;
	}

	/* if children dependant and we are at leaf level, nothing to do */
	if (keydef->flags & KEYSPEC_UPDATE_CHILDREN_MASK &&
	    pargs->which == XTREE_LEAF)
		return;

	/* only work on keys related to the targeted layout */
	if (xstrncmp(keydef->plugin->layout->type, pargs->enode->layout->type,
		     PATHLEN)) {
		return;
	}

	/* get ref_key (identical if not defined) */
	if (keydef->ref_key != NULL) {
		ref_keydef = xhash_get(mgr->keydefs, keydef->ref_key);
		if (!ref_keydef) {
			debug2("layouts: autoupdate: key='%s': invalid "
			       "ref_key='%s'", keydef->key, keydef->ref_key);
			return;
		}
	} else {
		ref_keydef = keydef;
	}

	/* process parents aggregation
	 * for now, xtree only provides one parent so any update op
	 * (MAX, MIN, FSHARE, ...) is a setter */
	if ((action = keydef->flags & KEYSPEC_UPDATE_PARENTS_MASK) &&
	    (pargs->which == XTREE_PREORDER || pargs->which == XTREE_LEAF) &&
	    (node = ((xtree_node_t*)pargs->enode->node)->parent) != NULL ) {

		/* get current node value reference */
		oldvalue = entity_get_data_ref(cnode->entity, keydef->key);
		if (!oldvalue)
			return;

		/* get siblings count */
		child = node->start;
		count = 0;
		while (child) {
			count++;
			child = child->next;
		}

		/* get parent node KV data ref */
		enode = (entity_node_t*) xtree_node_get_data(node);
		value = entity_get_data_ref(enode->entity, ref_keydef->key);
		if (!value)
			return;

		/* only set operation currently provided for parents except
		 * for fshare action */
		_autoupdate_entity_kv(keydef, ref_keydef, S_P_OPERATOR_SET,
				      oldvalue, value);
		if (action == KEYSPEC_UPDATE_PARENTS_FSHARE) {
			_autoupdate_entity_kv(keydef, ref_keydef,
					      S_P_OPERATOR_AVG,
					      oldvalue, (void*) &count);
		}

		return;
	}

	/* process children aggregation */
	if ((action = keydef->flags & KEYSPEC_UPDATE_CHILDREN_MASK) &&
	    pargs->which == XTREE_ENDORDER) {

		/* get current node value reference */
		oldvalue = entity_get_data_ref(cnode->entity, keydef->key);
		if (!oldvalue)
			return;

		/* get children count */
		node = (xtree_node_t*)cnode->node;
		child = node->start;
		count = 0;
		while (child) {
			count++;
			child = child->next;
		}

		/* no action if no children */
		if (count == 0)
			return;

		/* if count action, do what is necessary and return */
		if (action == KEYSPEC_UPDATE_CHILDREN_COUNT) {
			_autoupdate_entity_kv(keydef, ref_keydef,
					      S_P_OPERATOR_SET,
					      oldvalue, (void*) &count);
			return;
		}

		/* iterate on the children */
		setter = 1;
		child = node->start;
		while (child) {
			/* get child node KV data ref */
			enode = (entity_node_t*) xtree_node_get_data(child);
			value = entity_get_data_ref(enode->entity,
						    ref_keydef->key);

			if (!value) {
				/* try next child */
				child = child-> next;
				continue;
			}

			switch (action) {
			case KEYSPEC_UPDATE_CHILDREN_SUM:
			case KEYSPEC_UPDATE_CHILDREN_AVG:
				/* first child is a setter */
				if (setter) {
					operator = S_P_OPERATOR_SET;
					setter = 0;
				}
				else
					operator = S_P_OPERATOR_ADD;
				break;
			case KEYSPEC_UPDATE_CHILDREN_MIN:
				operator = S_P_OPERATOR_SET_IF_MIN;
				break;
			case KEYSPEC_UPDATE_CHILDREN_MAX:
				operator = S_P_OPERATOR_SET_IF_MAX;
				break;
			default:
				/* should not be called! */
				return;
			}

			/* update the value according to the operator */
			_autoupdate_entity_kv(keydef, ref_keydef, operator,
					      oldvalue, value);

			/* then next child */
			child = child-> next;
		}

		/* if average action, do what is necessary before return */
		if (action == KEYSPEC_UPDATE_CHILDREN_AVG) {
			_autoupdate_entity_kv(keydef, ref_keydef,
					      S_P_OPERATOR_AVG,
					      oldvalue, (void*) &count);
			return;
		}

		return;
	}

}

/*
 * _autoupdate_layout_tree : internal function used when automatically
 * updating elements of a layout tree using _layouts_autoupdate_layout */
static uint8_t _autoupdate_layout_tree(xtree_node_t* node, uint8_t which,
				       uint32_t level, void* arg)
{
	entity_node_t* cnode;
	_autoupdate_tree_args_t sync_args;

	/* only need to work for preorder, leaf and endorder cases */
	if (which != XTREE_PREORDER &&
	    which != XTREE_LEAF &&
	    which != XTREE_ENDORDER) {
		return 1;
	}

	/* extract current node entity_node to next browsing */
	cnode = (entity_node_t*) xtree_node_get_data(node);
	if (!cnode)
		return 1;

	/* prepare downcall args */
	sync_args.enode = cnode;
	sync_args.which = which;
	sync_args.level = level;

	/* iterate over the K/V of the entity, syncing them according
	 * to their autoupdate flags */
	xhash_walk(cnode->entity->data, _tree_update_node_entity_data,
		   &sync_args);

	return 1;
}

/* helper function used to automatically update a layout internal
 * entities KVs based on inheritance relations (parents/children) */
static int _layouts_autoupdate_layout(layout_t* layout)
{
	/* autoupdate according to the layout struct type */
	switch(layout->struct_type) {
	case LAYOUT_STRUCT_TREE:
		xtree_walk(layout->tree, NULL, 0,
			   XTREE_LEVEL_MAX,
			   _autoupdate_layout_tree, NULL);
		break;
	}

	return SLURM_SUCCESS;
}

/* helper function used to automatically update a layout internal
 * entities KVs based on inheritance relations (parents/children)
 * only when allowed by the associated plugin */
static int _layouts_autoupdate_layout_if_allowed(layout_t* layout)
{
	int i, rc = SLURM_ERROR;
	/* look if the corresponding layout plugin enables autoupdate */
	for (i = 0; i < mgr->plugins_count; i++) {
		if (mgr->plugins[i].layout == layout) {
			/* no autoupdate allowed, return success */
			if (!mgr->plugins[i].ops->spec->autoupdate)
				rc = SLURM_SUCCESS;
			else
				rc = _layouts_autoupdate_layout(layout);
			break;
		}
	}
	return rc;
}

/*****************************************************************************\
 *                                   DEBUG DUMP                              *
\*****************************************************************************/

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
	entity_node_t* enode;
	if (which != XTREE_PREORDER && which != XTREE_LEAF) {
		return 1;
	}
	enode = (entity_node_t*) xtree_node_get_data(node);
	if (!enode || !enode->entity) {
		fprintf(fdump, "NULL_entity\n");
	}
	else {
		fprintf(fdump, "%*s%s\n", level, " ", enode->entity->name);
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

int layouts_init(void)
{
	int i = 0;
	uint32_t layouts_count;

	debug3("layouts: layouts_init()...");

	if (mgr->plugins) {
		return SLURM_SUCCESS;
	}

	slurm_mutex_lock(&layouts_mgr.lock);

	_layouts_mgr_init(&layouts_mgr);
	layouts_count = list_count(layouts_mgr.layouts_desc);
	if (layouts_count == 0)
		info("layouts: no layout to initialize");
	else
		info("layouts: %d layout(s) to initialize", layouts_count);

	mgr->plugins = xmalloc(sizeof(layout_plugin_t) * layouts_count);
	list_for_each(layouts_mgr.layouts_desc,
		      _layouts_init_layouts_walk_helper,
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
		info("layouts: layouts_init done : %d layout(s) "
		     "initialized", layouts_count);
	}

	slurm_mutex_unlock(&layouts_mgr.lock);

	return mgr->plugins_count == layouts_count ?
		SLURM_SUCCESS : SLURM_ERROR;
}

int layouts_fini(void)
{
	int i;

	debug3("layouts: layouts_fini()...");

	/* push layouts states to the state save location */
	layouts_state_save();

	slurm_mutex_lock(&mgr->lock);

	/*
	 * free the layouts before destroying the plugins,
	 * otherwise we will get trouble xfreeing the layouts whose
	 * memory is owned by the plugins structs
	 */
	_layouts_mgr_free(mgr);

	for (i = 0; i < mgr->plugins_count; i++) {
		_layout_plugins_destroy(&mgr->plugins[i]);
	}
	xfree(mgr->plugins);
	mgr->plugins = NULL;
	mgr->plugins_count = 0;

	slurm_mutex_unlock(&mgr->lock);

	info("layouts: all layouts are now unloaded.");

	return SLURM_SUCCESS;
}

int layouts_load_config(int recover)
{
	int i, rc, inx;
	struct node_record *node_ptr;
	layout_t *layout;
	uint32_t layouts_count;
	entity_t *entity;
	entity_node_t *enode;
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
		debug3("layouts: loading node %s", node_ptr->name);
		xassert (node_ptr->magic == NODE_MAGIC);
		xassert (node_ptr->config_ptr->magic == CONFIG_MAGIC);

		/* init entity structure on the heap */
		entity = (entity_t*) xmalloc(sizeof(struct entity_st));
		entity_init(entity, node_ptr->name, "Node");
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
		enode = entity_add_node(entity, layout);
		ptr = xtree_add_child(layout->tree, layout->tree->root,
				      (void*)enode, XTREE_APPEND);
		if (!ptr) {
			error("layouts: unable to add entity of node %s"
			      "in the hashtable, aborting", node_ptr->name);
			entity_free(entity);
			xfree(entity);
			rc = SLURM_ERROR;
			break;
		} else {
			enode->node = ptr;
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
		if (recover) {
			debug("layouts: loading stage 1.1 (restore state)");
			for (i = 0; i < mgr->plugins_count; ++i) {
				debug3("layouts: reading state of %s",
				       mgr->plugins[i].name);
				_layouts_read_state(&mgr->plugins[i]);
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
		debug("layouts: loading stage 3");
		for (i = 0; i < mgr->plugins_count; ++i) {
			debug3("layouts: autoupdating %s",
			       mgr->plugins[i].name);
			if (mgr->plugins[i].ops->spec->autoupdate) {
				if (_layouts_autoupdate_layout(mgr->plugins[i].
							       layout) !=
				    SLURM_SUCCESS) {
					rc = SLURM_ERROR;
					break;
				}
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

layout_t* layouts_get_layout_nolock(const char* type)
{
	return (layout_t*)xhash_get(mgr->layouts, type);
}

layout_t* layouts_get_layout(const char* type)
{
	layout_t *layout = NULL;
	slurm_mutex_lock(&mgr->lock);
	layout = layouts_get_layout_nolock(type);
	slurm_mutex_unlock(&mgr->lock);
	return layout;
}

entity_t* layouts_get_entity_nolock(const char* name)
{
	return (entity_t*)xhash_get(mgr->entities, name);
}

entity_t* layouts_get_entity(const char* name)
{
	entity_t* e;
	slurm_mutex_lock(&mgr->lock);
	e = layouts_get_entity_nolock(name);
	slurm_mutex_unlock(&mgr->lock);
	return e;
}


int layouts_pack_layout(char *l_type, char *char_entities, char *type,
			uint32_t flags, Buf buffer)
{
	_pack_args_t pargs;
	layout_t* layout;
	int orig_offset, fini_offset;
	char *str;

	slurm_mutex_lock(&mgr->lock);

	layout = layouts_get_layout_nolock(l_type);
	if (layout == NULL) {
		slurm_mutex_unlock(&mgr->lock);
		info("unable to get layout of type '%s'", l_type);
		return SLURM_ERROR;
	}
	/* initialize args for recursive packing */
	pargs.buffer = buffer;
	pargs.layout = layout;
	pargs.current_line = NULL;
	pargs.all = 0;
	pargs.list_entities = NULL;
	if (char_entities != NULL) {
		if (xstrcmp(char_entities, "*") == 0)
			pargs.all = 1;
		else
			pargs.list_entities = hostlist_create(char_entities);
	}
	pargs.type = type;
	pargs.flags = flags;
	pargs.record_count = 0;
	orig_offset = get_buf_offset(buffer);
	pack32(pargs.record_count, buffer);

	/* start by packing the layout priority in case we are dumping state */
	if (pargs.flags & LAYOUTS_DUMP_STATE) {
		str = xstrdup_printf("Priority=%u\n", layout->priority);
		packstr(str, buffer);
		pargs.record_count++;
		xfree(str);
	}

	/* pack according to the layout struct type */
	switch (layout->struct_type) {
	case LAYOUT_STRUCT_TREE:
		xtree_walk(layout->tree, NULL, 0, XTREE_LEVEL_MAX,
			   _pack_layout_tree, &pargs);
		break;
	}

	if (pargs.list_entities != NULL)
		slurm_hostlist_destroy(pargs.list_entities);

	fini_offset = get_buf_offset(buffer);
	set_buf_offset(buffer, orig_offset);
	pack32(pargs.record_count, buffer);
	set_buf_offset(buffer, fini_offset);

	slurm_mutex_unlock(&mgr->lock);

	return SLURM_SUCCESS;
}

int layouts_update_layout(char *l_type, Buf buffer)
{
	int i, rc;
	slurm_mutex_lock(&mgr->lock);
	for (i = 0; i < mgr->plugins_count; i++) {
		if (!xstrcmp(mgr->plugins[i].name, l_type)) {
			rc = _layouts_update_state((layout_plugin_t*)
						   &mgr->plugins[i],
						   buffer);
			slurm_mutex_unlock(&mgr->lock);
			return rc;
		}
	}
	info("%s: no plugin matching layout=%s, skipping", __func__, l_type);
	slurm_mutex_unlock(&mgr->lock);
	return SLURM_ERROR;
}

int layouts_autoupdate_layout(char *l_type)
{
	int rc = SLURM_ERROR;
	layout_t* layout;

	slurm_mutex_lock(&mgr->lock);
	layout = layouts_get_layout_nolock(l_type);
	if (layout == NULL) {
		info("unable to get layout of type '%s'", l_type);
	} else {
		rc = _layouts_autoupdate_layout(layout);
	}
	slurm_mutex_unlock(&mgr->lock);

	return rc;
}

int layouts_state_save_layout(char* l_type)
{
	int error_code = 0, log_fd, offset;
	char *old_file = NULL, *new_file = NULL, *reg_file = NULL;
	static int high_buffer_size = (16 * 1024);
	Buf buffer = init_buf(high_buffer_size);
	FILE* fdump;
	uint32_t utmp32, record_count = 0;
	char *tmp_str = NULL;

	DEF_TIMERS;
	START_TIMER;

	/* pack the targeted layout into a tmp buffer */
	error_code = layouts_pack_layout(l_type, "*", NULL,
					 LAYOUTS_DUMP_STATE, buffer);
	if (error_code != SLURM_SUCCESS) {
		error("unable to save layout[%s] state", l_type);
		return error_code;
	}

	/* rewind the freshly created buffer to unpack it into a file */
	offset = get_buf_offset(buffer);
	high_buffer_size = MAX(high_buffer_size, offset);
	set_buf_offset(buffer, 0);

	/* create working files */
	reg_file = _state_get_filename(l_type);
	old_file = xstrdup_printf("%s.old", reg_file);
	new_file = xstrdup_printf("%s.new", reg_file);
	log_fd = creat(new_file, 0600);
	if (log_fd < 0 || !(fdump = fdopen(log_fd, "w"))) {
		error("Can't save state, create file %s error %m",
		      new_file);
		error_code = errno;
	} else {
		/* extract the amount of records and then proceed
		 * then dump packed strings into the temporary file */
		safe_unpack32(&record_count, buffer);
		debug("layouts/%s: dumping %u records into state file",
		      l_type, record_count);
		while (get_buf_offset(buffer) < offset) {
			safe_unpackstr_xmalloc(&tmp_str, &utmp32, buffer);
			if (tmp_str != NULL) {
				if (*tmp_str == '\0') {
					xfree(tmp_str);
					break;
				}
				fprintf(fdump, "%s", tmp_str);
				xfree(tmp_str);
				continue;
			}
		unpack_error:
			break;
		}
		fflush(fdump);
		fsync(log_fd);
		fclose(fdump);
	}
	if (error_code)
		(void) unlink(new_file);
	else {			/* file shuffle */
		(void) unlink(old_file);
		if (link(reg_file, old_file))
			debug4("unable to create link for %s -> %s: %m",
			       reg_file, old_file);
		(void) unlink(reg_file);
		if (link(new_file, reg_file))
			debug4("unable to create link for %s -> %s: %m",
			       new_file, reg_file);
		(void) unlink(new_file);
	}
	xfree(old_file);
	xfree(reg_file);
	xfree(new_file);

	free_buf(buffer);

	END_TIMER2("layouts_state_save_layout");

	return SLURM_SUCCESS;
}

int layouts_state_save(void)
{
	DEF_TIMERS;
	START_TIMER;
	xhash_walk(mgr->layouts,  _state_save_layout, NULL);
	END_TIMER2("layouts_state_save");
	return SLURM_SUCCESS;
}

#define _layouts_entity_wrapper(func, l, e, r...)			\
	layout_t* layout;						\
	entity_t* entity;						\
	int rc;								\
	slurm_mutex_lock(&mgr->lock);					\
	layout = layouts_get_layout_nolock(l);				\
	entity = layouts_get_entity_nolock(e);				\
	rc = func(layout, entity, ##r);					\
	slurm_mutex_unlock(&mgr->lock);					\
	return rc;							\

int layouts_entity_get_kv_type(char* l, char* e, char* key)
{
	_layouts_entity_wrapper(_layouts_entity_get_kv_type,l,e,key);
}

int layouts_entity_get_kv_flags(char* l, char* e, char* key)
{
	_layouts_entity_wrapper(_layouts_entity_get_kv_flags, l, e, key);
}

int layouts_entity_push_kv(char* l, char* e, char* key)
{
	_layouts_entity_wrapper(_layouts_entity_push_kv, l, e, key);
}

int layouts_entity_pull_kv(char* l, char* e, char* key)
{
	_layouts_entity_wrapper(_layouts_entity_pull_kv, l, e, key);
}

int layouts_entity_set_kv(char* l, char* e, char* key, void* value,
			  layouts_keydef_types_t key_type)
{
	_layouts_entity_wrapper(_layouts_entity_set_kv, l, e,
				key, value, key_type);
}

int layouts_entity_set_kv_ref(char* l, char* e, char* key, void* value,
			      layouts_keydef_types_t key_type)
{
	_layouts_entity_wrapper(_layouts_entity_set_kv_ref, l, e,
				key, value, key_type);
}

int layouts_entity_setpush_kv(char* l, char* e, char* key, void* value,
			      layouts_keydef_types_t key_type)
{
	_layouts_entity_wrapper(_layouts_entity_setpush_kv, l, e,
				key, value, key_type);
}

int layouts_entity_setpush_kv_ref(char* l, char* e, char* key, void* value,
				  layouts_keydef_types_t key_type)
{
	_layouts_entity_wrapper(_layouts_entity_setpush_kv_ref, l, e,
				key, value, key_type);
}

int layouts_entity_get_kv(char* l, char* e, char* key, void* value,
			  layouts_keydef_types_t key_type)
{
	_layouts_entity_wrapper(_layouts_entity_get_kv, l, e,
				key, value, key_type);
}

int layouts_entity_get_mkv(char* l, char* e, char* keys, void* value,
			   size_t size, layouts_keydef_types_t key_type)
{
	_layouts_entity_wrapper(_layouts_entity_get_mkv, l, e,
				keys, value, size, key_type);
}

int layouts_entity_get_kv_ref(char* l, char* e, char* key, void** value,
			      layouts_keydef_types_t key_type)
{
	_layouts_entity_wrapper(_layouts_entity_get_kv_ref, l, e,
				key, value, key_type);
}

int layouts_entity_get_mkv_ref(char* l, char* e, char* keys, void* value,
			       size_t size, layouts_keydef_types_t key_type)
{
	_layouts_entity_wrapper(_layouts_entity_get_mkv_ref, l, e,
				keys, value, size, key_type);
}

int layouts_entity_pullget_kv(char* l, char* e, char* key, void* value,
			      layouts_keydef_types_t key_type)
{
	_layouts_entity_wrapper(_layouts_entity_pullget_kv, l, e,
				key, value, key_type);
}

int layouts_entity_pullget_kv_ref(char* l, char* e, char* key, void** value,
				  layouts_keydef_types_t key_type)
{
	_layouts_entity_wrapper(_layouts_entity_pullget_kv_ref, l, e,
				key, value, key_type);
}
