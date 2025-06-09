/*****************************************************************************\
 *  serializer plugin interface
 ******************************************************************************
 *  Copyright (C) SchedMD LLC.
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

#include "slurm/slurm_errno.h"

#include "src/common/list.h"
#include "src/common/log.h"
#include "src/common/read_config.h"
#include "src/common/timers.h"
#include "src/common/xassert.h"
#include "src/common/xmalloc.h"
#include "src/common/xstring.h"

#include "src/interfaces/serializer.h"

/* Define slurm-specific aliases for use by plugins, see slurm_xlator.h. */
strong_alias(serializer_g_init, slurm_serializer_g_init);
strong_alias(serialize_g_data_to_string, slurm_serialize_g_data_to_string);
strong_alias(serialize_g_string_to_data, slurm_serialize_g_string_to_data);
strong_alias(serializer_g_fini, slurm_serializer_g_fini);
strong_alias(serializer_required, slurm_serializer_required);

#define SERIALIZER_MAJOR_TYPE "serializer"
#define SERIALIZER_MIME_TYPES_SYM "mime_types"
#define PMT_MAGIC 0xaaba8031
#define MIME_ARRAY_MAGIC 0xabb00031

typedef struct {
	int (*init)(serializer_flags_t flags);
	void (*fini)(void);
	int (*data_to_string)(char **dest, size_t *length, const data_t *src,
			      serializer_flags_t flags);
	int (*string_to_data)(data_t **dest, const char *src, size_t length);
} funcs_t;

typedef struct {
	int magic; /* MIME_ARRAY_MAGIC */
	int index;
} mime_type_array_args_t;

/* Must be synchronized with funcs_t above */
static const char *syms[] = {
	"serialize_p_init",
	"serialize_p_fini",
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
static list_t *mime_types_list = NULL;
static const char **mime_array = NULL;

static pthread_mutex_t init_mutex = PTHREAD_MUTEX_INITIALIZER;
#ifndef NDEBUG
/* Track when the plugins should no longer be changed */
static bool should_not_change = false;
#endif /* !NDEBUG */

static const struct {
	char *string;
	serializer_flags_t flag;
} flags[] = {
	{ "compact", SER_FLAGS_COMPACT },
	{ "pretty", SER_FLAGS_PRETTY },
	{ "complex", SER_FLAGS_COMPLEX },
	{ "no_tag", SER_FLAGS_NO_TAG },
};

static serializer_flags_t _parse_flag(const char *flag)
{
	for (int i = 0; i < ARRAY_SIZE(flags); i++)
		if (!xstrcasecmp(flag, flags[i].string))
			return flags[i].flag;

	return SER_FLAGS_NONE;
}

static int _parse_config(const char *config, serializer_flags_t *flags)
{
	int rc = SLURM_SUCCESS;
	char *token = NULL, *save_ptr = NULL;
	char *toklist = xstrdup(config);

	token = strtok_r(toklist, ",", &save_ptr);
	while (token) {
		serializer_flags_t flag = SER_FLAGS_NONE;

		if (!token[0])
			continue;

		if ((flag = _parse_flag(token)) == SER_FLAGS_NONE) {
			debug("%s: Unknown flag \"%s\" in \"%s\"",
			      __func__, token, config);
			rc = EINVAL;
		}

		*flags |= flag;

		token = strtok_r(NULL, ",", &save_ptr);
	}
	xfree(toklist);

	return rc;
}

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

	xassert(dest && ((*dest == NULL) || (*dest[0] == '\0')));

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

extern const char *resolve_mime_type(const char *mime_type,
				     const char **plugin_ptr)
{
	plugin_mime_type_t *pmt = _find_serializer(mime_type);

	if (!pmt)
		return NULL;

	*plugin_ptr = plugins->types[pmt->index];
	return pmt->mime_type;
}

static int _register_mime_types(list_t *mime_types_list, size_t plugin_index,
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

extern const char **get_mime_type_array(void)
{
#ifndef NDEBUG
	slurm_mutex_lock(&init_mutex);
	should_not_change = true;
	xassert(mime_array);
	slurm_mutex_unlock(&init_mutex);
#endif /* !NDEBUG */

	return mime_array;
}

extern int serializer_g_init(void)
{
	int rc = SLURM_SUCCESS;
	serializer_flags_t flags = SER_FLAGS_NONE;

	slurm_mutex_lock(&init_mutex);
	if (plugins) {
		slurm_mutex_unlock(&init_mutex);
		return rc;
	}

	xassert(!should_not_change);

	xassert(sizeof(funcs_t) == sizeof(void *) * ARRAY_SIZE(syms));
	rc = load_plugins(&plugins, SERIALIZER_MAJOR_TYPE, NULL, NULL, syms,
			  ARRAY_SIZE(syms));

	if (rc)
		fatal("%s: Unable to load serializer plugins: %s",
		      __func__, slurm_strerror(rc));

	if (!mime_types_list)
		mime_types_list = list_create(xfree_ptr);

	xrecalloc(mime_array, (plugins->count + 1), sizeof(*mime_array));

	for (size_t i = 0; plugins && (i < plugins->count) && !rc; i++) {
		const char *config = NULL;
		const char **mime_types;
		const funcs_t *func_ptr = plugins->functions[i];

		xassert(plugins->handles[i] != PLUGIN_INVALID_HANDLE);

		mime_types = plugin_get_sym(plugins->handles[i],
					    SERIALIZER_MIME_TYPES_SYM);
		if (!mime_types)
			fatal_abort("%s: unable to load %s from plugin",
				    __func__, SERIALIZER_MIME_TYPES_SYM);

		/* First mime_type is always considered primary */
		mime_array[i] = mime_types[0];

		_register_mime_types(mime_types_list, i, mime_types);

		if (!xstrcmp(plugins->types[i], MIME_TYPE_JSON_PLUGIN)) {
			if (running_in_slurmrestd())
				config = getenv("SLURMRESTD_JSON");
			if (!config)
				config = getenv(ENV_CONFIG_JSON);
		}

		if (!xstrcmp(plugins->types[i], MIME_TYPE_YAML_PLUGIN)) {
			if (running_in_slurmrestd())
				config = getenv("SLURMRESTD_YAML");
			if (!config)
				config = getenv(ENV_CONFIG_YAML);
		}

		if (config && config[0] && (rc = _parse_config(config, &flags)))
			fatal("Unable to parse serializer \"%s\" flags: %s",
			      config, slurm_strerror(rc));

		rc = (*func_ptr->init)(flags);
	}

	slurm_mutex_unlock(&init_mutex);

	return rc;
}

extern void serializer_required(const char *mime_type)
{
	serializer_g_init();

	slurm_mutex_lock(&init_mutex);
	if (!_find_serializer(mime_type))
		fatal("%s: could not find plugin for %s", __func__, mime_type);
	slurm_mutex_unlock(&init_mutex);
}

extern void serializer_g_fini(void)
{
#ifndef NDEBUG
	/* There should not be a init() and then fini() and then init() again */
	slurm_mutex_lock(&init_mutex);
	should_not_change = true;
	slurm_mutex_unlock(&init_mutex);
#endif /* !NDEBUG */

#ifdef MEMORY_LEAK_DEBUG
	debug3("%s: cleaning up", __func__);
	slurm_mutex_lock(&init_mutex);

	for (size_t i = 0; plugins && (i < plugins->count); i++) {
		const funcs_t *func_ptr = plugins->functions[i];
		xassert(plugins->handles[i] != PLUGIN_INVALID_HANDLE);
		(*func_ptr->fini)();
	}

	xfree(mime_array);
	FREE_NULL_LIST(mime_types_list);
	FREE_NULL_PLUGINS(plugins);
	slurm_mutex_unlock(&init_mutex);
#endif
}
