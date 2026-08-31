/*****************************************************************************\
 *  serializer.c - serializer plugin interface
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
#include "src/common/pack.h"
#include "src/common/read_config.h"
#include "src/common/run_in_daemon.h"
#include "src/common/timers.h"
#include "src/common/xassert.h"
#include "src/common/xmalloc.h"
#include "src/common/xstring.h"

#include "src/interfaces/data_parser.h"
#include "src/interfaces/serializer.h"

/* Define slurm-specific aliases for use by plugins, see slurm_xlator.h. */
strong_alias(serializer_g_init, slurm_serializer_g_init);
strong_alias(serialize_g_data_to_string, slurm_serialize_g_data_to_string);
strong_alias(serialize_g_string_to_data, slurm_serialize_g_string_to_data);
strong_alias(serializer_g_fini, slurm_serializer_g_fini);
strong_alias(serializer_required, slurm_serializer_required);
strong_alias(serialize_g_parse, slurm_serialize_g_parse);
strong_alias(serialize_g_dump, slurm_serialize_g_dump);

#define SERIALIZER_MAJOR_TYPE "serializer"
#define SERIALIZER_MIME_TYPES_SYM "mime_types"
#define PMT_MAGIC 0xaaba8031
#define MIME_ARRAY_MAGIC 0xabb00031

typedef struct {
	int (*init)(serializer_flags_t flags);
	void (*fini)(void);
	int (*data_to_string)(char **dest, size_t *length, data_t *src,
			      serializer_flags_t flags);
	int (*string_to_data)(data_t **dest, const char *src, size_t length);
	int (*dump)(serialize_dump_state_t **state_ptr, data_parser_t *parser,
		    data_parser_type_t type, void *src, ssize_t src_bytes,
		    buf_t *dst, serializer_flags_t flags);
	int (*parse)(serialize_parse_state_t **state_ptr, data_parser_t *parser,
		     data_parser_type_t type, void *dst, ssize_t dst_bytes,
		     buf_t *src);
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
	"serialize_p_dump",
	"serialize_p_parse",
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

/*
 * Parse comma-separated serializer flags into *flags.
 *
 * Parsing always yields a layout a plugin can render: an unknown flag is
 * dropped, and as compact and pretty select the same layout, the last named
 * wins. serializer/json xassert()s when handed both.
 *
 * Warn rather than reject: a flag only changes how output is rendered, while
 * rejecting takes down every daemon and client initializing a serializer,
 * none of them near the configuration at fault.
 *
 * src names where the flags came from, as the same flags arrive from both
 * slurm.conf and the environment.
 */
static void _parse_config(const char *config, serializer_flags_t *flags,
			  const char *src)
{
	char *token = NULL, *save_ptr = NULL;
	char *toklist = xstrdup(config);

	token = strtok_r(toklist, ",", &save_ptr);
	while (token) {
		serializer_flags_t flag = _parse_flag(token);

		if (flag == SER_FLAGS_NONE) {
			warning_in_daemon(
				"Ignoring unknown %s flag \"%s\" in \"%s\"",
				src, token, config);
		} else {
			/* A layout flag, and a different one is already set */
			if ((flag & SER_FLAGS_LAYOUT_MASK) &&
			    (*flags & SER_FLAGS_LAYOUT_MASK & ~flag)) {
				warning_in_daemon(
					"%s flag \"%s\" overrides the layout already selected in \"%s\"",
					src, token, config);
				*flags &= ~SER_FLAGS_LAYOUT_MASK;
			}

			*flags |= flag;
		}

		token = strtok_r(NULL, ",", &save_ptr);
	}
	xfree(toklist);
}

/* Parse SerializerParameters comma-separated flags, SER_FLAGS_NONE if unset */
static serializer_flags_t _conf_flags(void)
{
	serializer_flags_t flags = SER_FLAGS_NONE;
	const char *params = slurm_conf.serializer_params;

	if (params && params[0])
		_parse_config(params, &flags, "SerializerParameters");

	return flags;
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

extern int serialize_g_data_to_string(char **dest, size_t *length, data_t *src,
				      const char *mime_type,
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

	if (plugin_ptr)
		*plugin_ptr = plugins->types[pmt->index];
	return pmt->mime_type;
}

/*
 * Report a MIME type that went to an earlier plugin.
 *
 * Two plugins Slurm ships claiming one type is a packaging decision that an
 * administrator can not act on, and a serializer initializes in every daemon
 * and on every step, so only warn when SerializerPlugins named the plugins and
 * editing that list is a real fix. Every plugin is explicitly named whenever
 * SerializerPlugins is set, as load_plugins() then builds the plugin list from
 * that string alone.
 *
 * slurmstepd is never sent SerializerPlugins (see slurmstepd_init.c), so it
 * always takes the quiet path no matter what is configured.
 */
static void _log_skipped_mime_type(const char *mime_type, size_t holder,
				   size_t skipped)
{
	if (slurm_conf.serializer_plugins && slurm_conf.serializer_plugins[0])
		warning_in_daemon(
			"SerializerPlugins: MIME type \"%s\" is served by serializer plugin %s: skipping plugin %s",
			mime_type, plugins->types[holder],
			plugins->types[skipped]);
	else
		log_flag(DATA, "MIME type \"%s\" served by serializer plugin %s: skipping plugin %s",
			 mime_type, plugins->types[holder],
			 plugins->types[skipped]);
}

static const char *_register_mime_types(list_t *mime_types_list,
					size_t plugin_index,
					const char **mime_type)
{
	const char *first = NULL;

	while (*mime_type) {
		plugin_mime_type_t *pmt;

		/*
		 * Only one plugin may serve a MIME type. Keep the plugin that
		 * already claimed it and skip this one for that type only: a
		 * plugin may lose one MIME type and still serve another.
		 */
		if ((pmt = list_find_first(mime_types_list,
					   _find_serializer_full_type,
					   (void *) *mime_type))) {
			_log_skipped_mime_type(*mime_type, pmt->index,
					       plugin_index);
		} else {
			pmt = xmalloc(sizeof(*pmt));
			pmt->index = plugin_index;
			pmt->mime_type = *mime_type;
			pmt->magic = PMT_MAGIC;

			list_append(mime_types_list, pmt);

			log_flag(DATA, "registered serializer plugin %s for %s",
				 plugins->types[plugin_index], pmt->mime_type);

			/*
			 * First mime_type successfully registered is the
			 * primary. A plugin that loses mime_types[0] to
			 * another plugin has its next type promoted.
			 */
			if (!first)
				first = *mime_type;
		}

		mime_type++;
	}

	return first;
}

/*
 * Is the plugin at plugin_index the one serving mime_type?
 *
 * Only one plugin may serve a MIME type and the first to register it wins, so
 * ownership must be resolved from the registration list and not from the
 * plugin name: a plugin skipped for a type must not be handed that type's
 * configuration, which it would never apply to anything.
 *
 * Only plugins [0, plugin_index] have registered while serializer_g_init() is
 * still walking the list, which is enough: a plugin not holding the type by
 * its own iteration lost it to an earlier one and can never gain it back.
 */
static bool _serves_mime_type(size_t plugin_index, const char *mime_type)
{
	const plugin_mime_type_t *pmt = _find_serializer(mime_type);

	return (pmt && ((size_t) pmt->index == plugin_index));
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
	int rc = SLURM_SUCCESS, mi = 0;
	serializer_flags_t conf_flags = SER_FLAGS_NONE;

	slurm_mutex_lock(&init_mutex);
	if (plugins) {
		slurm_mutex_unlock(&init_mutex);
		return rc;
	}

	xassert(!should_not_change);

	xassert(sizeof(funcs_t) == sizeof(void *) * ARRAY_SIZE(syms));
	rc = load_plugins(&plugins, SERIALIZER_MAJOR_TYPE,
			  slurm_conf.serializer_plugins, NULL, syms,
			  ARRAY_SIZE(syms));

	if (rc)
		fatal("%s: Unable to load serializer plugins: %s",
		      __func__, slurm_strerror(rc));

	if (!mime_types_list)
		mime_types_list = list_create(xfree_ptr);

	xrecalloc(mime_array, (plugins->count + 1), sizeof(*mime_array));

	conf_flags = _conf_flags();

	for (size_t i = 0; plugins && (i < plugins->count) && !rc; i++) {
		const char *config = NULL, *config_src = NULL;
		const char **mime_types = NULL, *mime_type = NULL;
		const funcs_t *func_ptr = plugins->functions[i];
		serializer_flags_t flags = conf_flags;

		xassert(plugins->handles[i] != PLUGIN_INVALID_HANDLE);

		mime_types = plugin_get_sym(plugins->handles[i],
					    SERIALIZER_MIME_TYPES_SYM);
		if (!mime_types)
			fatal_abort("%s: unable to load %s from plugin",
				    __func__, SERIALIZER_MIME_TYPES_SYM);

		if ((mime_type = _register_mime_types(mime_types_list, i,
						      mime_types)))
			mime_array[mi++] = mime_type;

		if (_serves_mime_type(i, MIME_TYPE_JSON)) {
			if (running_in_slurmrestd()) {
				config_src = "SLURMRESTD_JSON";
				config = getenv(config_src);
			}
			if (!config) {
				config_src = ENV_CONFIG_JSON;
				config = getenv(config_src);
			}
		} else if (_serves_mime_type(i, MIME_TYPE_YAML)) {
			if (running_in_slurmrestd()) {
				config_src = "SLURMRESTD_YAML";
				config = getenv(config_src);
			}
			if (!config) {
				config_src = ENV_CONFIG_YAML;
				config = getenv(config_src);
			}
		}

		/* Env vars override SerializerParameters flags for this plugin */
		if (config && config[0]) {
			flags = SER_FLAGS_NONE;
			_parse_config(config, &flags, config_src);
		}

		rc = (*func_ptr->init)(flags);
	}

	if (!rc && !_find_serializer(MIME_TYPE_JSON))
		warning_in_daemon("No serializer plugin loaded for %s.",
				  MIME_TYPE_JSON);

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

extern int serialize_g_parse(serialize_parse_state_t **state_ptr,
			     data_parser_t *parser, data_parser_type_t type,
			     void *dst, ssize_t dst_bytes, buf_t *src,
			     const char *mime_type)
{
	DEF_TIMERS;
	int rc = EINVAL;
	const funcs_t *func_ptr = NULL;
	plugin_mime_type_t *pmt = NULL;

	xassert(dst);
	xassert(dst_bytes > 0);
	xassert(!src || (src->magic == BUF_MAGIC));

	pmt = _find_serializer(mime_type);
	if (!pmt)
		return ESLURM_DATA_UNKNOWN_MIME_TYPE;

	xassert(pmt->magic == PMT_MAGIC);
	func_ptr = plugins->functions[pmt->index];

	START_TIMER;
	rc = (*func_ptr->parse)(state_ptr, parser, type, dst, dst_bytes, src);
	END_TIMER2(__func__);

	return rc;
}

extern int serialize_g_dump(serialize_dump_state_t **state_ptr,
			    data_parser_t *parser, data_parser_type_t type,
			    void *src, ssize_t src_bytes, buf_t *dst,
			    const char *mime_type, serializer_flags_t flags)
{
	DEF_TIMERS;
	int rc;
	const funcs_t *func_ptr;
	plugin_mime_type_t *pmt = NULL;

	xassert(src);
	xassert(src_bytes > 0);
	/*
	 * A NULL dst is the caller abandoning an incomplete dump, which always
	 * releases state_ptr. Only the plugin can release the state, so this
	 * must dispatch instead of rejecting the call, and there is no buffer
	 * to verify until there is a buffer.
	 */
	xassert(!dst || (dst->magic == BUF_MAGIC));

	pmt = _find_serializer(mime_type);
	if (!pmt)
		return ESLURM_DATA_UNKNOWN_MIME_TYPE;

	xassert(pmt->magic == PMT_MAGIC);
	func_ptr = plugins->functions[pmt->index];

	START_TIMER;
	rc = (*func_ptr->dump)(state_ptr, parser, type, src, src_bytes, dst,
			       flags);
	END_TIMER2(__func__);

	return rc;
}
