/*****************************************************************************\
 *  serializer plugin interface
 ******************************************************************************
 *  Copyright (C) 2022 SchedMD LLC.
 *  Written by Nathan Rini <nate@schedmd.com>
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

#include "src/common/list.h"
#include "src/common/log.h"
#include "src/common/read_config.h"
#include "src/common/timers.h"
#include "src/common/xassert.h"
#include "src/common/xmalloc.h"
#include "src/common/xstring.h"

#include "src/interfaces/serializer.h"

#define SERIALIZER_MAJOR_TYPE "serializer"
#define SERIALIZER_MIME_TYPES_SYM "mime_types"
#define PMT_MAGIC 0xaaba8031

typedef struct {
	int (*data_to_string)(char **dest, size_t *length, const data_t *src,
			      serializer_flags_t flags);
	int (*string_to_data)(data_t **dest, const char *src, size_t length);
} funcs_t;

/* Must be synchronized with funcs_t above */
static const char *syms[] = {
	"serialize_p_data_to_string",
	"serialize_p_string_to_data",
};

/* serializer plugin state */
static plugins_t *plugins = NULL;

typedef struct {
	int magic;
	const char *mime_type; /* never free - const data from plugins */
	int index; /* plugin index in g_context[] */
} plugin_mime_type_t;

/* list of all of the known mime types */
static List mime_types_list = NULL;

static pthread_mutex_t init_mutex = PTHREAD_MUTEX_INITIALIZER;

static int _find_serializer_full_type(void *x, void *key)
{
	plugin_mime_type_t *pmt = x;
	const char *mime_type = key;

	xassert(pmt->magic == PMT_MAGIC);

	if (!xstrcasecmp(mime_type, pmt->mime_type))
		return 1;

	return 0;
}

static plugin_mime_type_t *_find_serializer(const char *mime_type)
{
	if (!xstrcmp("*/*", mime_type)) {
		/*
		 * default to JSON if client will accept anything to avoid
		 * finding url-encoded or any other less suitable plugin first.
		 */
		plugin_mime_type_t *pmt = _find_serializer(MIME_TYPE_JSON);

		if (pmt) {
			return pmt;
		} else {
			/* JSON must not be loaded - try first thing we find */
			return list_peek(mime_types_list);
		}

	} else {
		const int len = strlen(mime_type);

		/* check if client gave {TYPE} / * */
		if ((len > 3) && (mime_type[len] == '*') &&
		    (mime_type[len - 1] == '*')) {
			debug("%s: Partial mime_type globbing not supported",
			      __func__);
			return NULL;
		}
	}

	return list_find_first(mime_types_list, _find_serializer_full_type,
			       (void *) mime_type);
}

extern int serialize_g_data_to_string(char **dest, size_t *length,
				      const data_t *src, const char *mime_type,
				      serializer_flags_t flags)
{
	DEF_TIMERS;
	int rc;
	const funcs_t *func_ptr;
	plugin_mime_type_t *pmt = NULL;

	xassert(dest && (*dest == NULL));

	pmt = _find_serializer(mime_type);
	if (!pmt)
		return ESLURM_DATA_UNKNOWN_MIME_TYPE;

	xassert(pmt->magic == PMT_MAGIC);
	func_ptr = plugins->functions[pmt->index];

	START_TIMER;
	rc = (*func_ptr->data_to_string)(dest, length, src, flags);
	END_TIMER2(__func__);

	/* dest must never be changed on failure */
	xassert(!rc || !*dest);

	return rc;
}

extern int serialize_g_string_to_data(data_t **dest, const char *src,
				      size_t length, const char *mime_type)
{
	DEF_TIMERS;
	int rc;
	plugin_mime_type_t *pmt = NULL;
	const funcs_t *func_ptr;

	xassert(dest && (*dest == NULL));

	pmt = _find_serializer(mime_type);
	if (!pmt)
		return ESLURM_DATA_UNKNOWN_MIME_TYPE;

	xassert(pmt->magic == PMT_MAGIC);
	func_ptr = plugins->functions[pmt->index];

	START_TIMER;
	rc = (*func_ptr->string_to_data)(dest, src, length);
	END_TIMER2(__func__);

	return rc;
}

extern const char *resolve_mime_type(const char *mime_type)
{
	plugin_mime_type_t *pmt = _find_serializer(mime_type);

	if (!pmt)
		return NULL;

	return pmt->mime_type;
}

static int _register_mime_types(List mime_types_list, size_t plugin_index,
				const char **mime_type)
{
	while (*mime_type) {
		plugin_mime_type_t *pmt = xmalloc(sizeof(*pmt));

		pmt->index = plugin_index;
		pmt->mime_type = *mime_type;
		pmt->magic = PMT_MAGIC;

		list_append(mime_types_list, pmt);

		log_flag(DATA, "registered serializer plugin %s for %s",
			 plugins->types[plugin_index], pmt->mime_type);

		mime_type++;
	}

	return SLURM_SUCCESS;
}

extern int serializer_g_init(const char *plugin_list, plugrack_foreach_t listf)
{
	int rc = SLURM_SUCCESS;

	slurm_mutex_lock(&init_mutex);

	/*
	 * There will be multiple calls to serializer_g_init() to load different
	 * plugins as the code always calls serializer_g_init() to be safe.
	 */
	xassert(sizeof(funcs_t) == sizeof(void *) * ARRAY_SIZE(syms));
	rc = load_plugins(&plugins, SERIALIZER_MAJOR_TYPE, plugin_list, listf,
			  syms, ARRAY_SIZE(syms));

	if (!mime_types_list)
		mime_types_list = list_create(xfree_ptr);

	for (size_t i = 0; plugins && (i < plugins->count); i++) {
		const char **mime_types;

		xassert(plugins->handles[i] != PLUGIN_INVALID_HANDLE);

		mime_types = plugin_get_sym(plugins->handles[i],
					    SERIALIZER_MIME_TYPES_SYM);
		if (!mime_types)
			fatal_abort("%s: unable to load %s from plugin",
				    __func__, SERIALIZER_MIME_TYPES_SYM);

		_register_mime_types(mime_types_list, i, mime_types);
	}

	slurm_mutex_unlock(&init_mutex);

	return rc;
}

extern void serializer_g_fini(void)
{
#ifdef MEMORY_LEAK_DEBUG
	debug3("%s: cleaning up", __func__);
	slurm_mutex_lock(&init_mutex);
	FREE_NULL_LIST(mime_types_list);
	FREE_NULL_PLUGINS(plugins);
	slurm_mutex_unlock(&init_mutex);
#endif
}
