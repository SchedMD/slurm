/*****************************************************************************\
 *  tpu_conf.c - TPU accelerator configuration (tpu.yaml) handling.
 *****************************************************************************
 *  Copyright (C) 2026 Google LLC.
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

#include "src/common/tpu_conf.h"

#include "src/common/list.h"
#include "src/common/read_config.h"
#include "src/common/slurm_protocol_api.h"
#include "src/common/xmalloc.h"
#include "src/common/xstring.h"

typedef struct {
	const char *pci_device_id;
	const char *vendor_id;
} find_pci_args_t;

extern void tpu_conf_type_entry_destroy(void *x)
{
	tpu_type_entry_t *entry = x;

	if (!entry)
		return;

	xfree(entry->pci_device_id);
	xfree(entry->shape_per_node);
	xfree(entry->shape_per_segment);
	xfree(entry->type);
	xfree(entry->vendor_id);
	xfree(entry);
}

extern void tpu_conf_destroy(tpu_conf_t *conf)
{
	if (!conf)
		return;

	FREE_NULL_LIST(conf->tpu_types);
	xfree(conf);
}

static int _match_type(void *x, void *key)
{
	tpu_type_entry_t *entry = x;

	return !xstrcmp(entry->type, (const char *) key);
}

extern tpu_type_entry_t *tpu_conf_find_type(tpu_conf_t *conf, const char *type)
{
	if (!conf || !conf->tpu_types || !type)
		return NULL;

	return list_find_first_ro(conf->tpu_types, _match_type, (void *) type);
}

static int _match_pci_id(void *x, void *key)
{
	tpu_type_entry_t *entry = x;
	find_pci_args_t *args = key;

	return (entry->vendor_id && entry->pci_device_id &&
		!xstrcasecmp(entry->vendor_id, args->vendor_id) &&
		!xstrcasecmp(entry->pci_device_id, args->pci_device_id));
}

extern tpu_type_entry_t *tpu_conf_find_pci_id(tpu_conf_t *conf,
					      const char *vendor_id,
					      const char *pci_device_id)
{
	find_pci_args_t args = {
		.vendor_id = vendor_id,
		.pci_device_id = pci_device_id,
	};

	if (!conf || !conf->tpu_types || !vendor_id || !pci_device_id)
		return NULL;

	return list_find_first_ro(conf->tpu_types, _match_pci_id, &args);
}

static int _append_type_name(void *x, void *arg)
{
	tpu_type_entry_t *entry = x;
	char **str = arg;

	xstrfmtcat(*str, "%s%s", (*str ? "," : ""), entry->type);

	return SLURM_SUCCESS;
}

extern char *tpu_conf_type_list_str(tpu_conf_t *conf)
{
	char *str = NULL;

	if (conf && conf->tpu_types)
		(void) list_for_each_ro(conf->tpu_types, _append_type_name,
					&str);

	if (!str)
		str = xstrdup("(none configured)");

	return str;
}

static int _finalize_entry(void *x, void *arg)
{
	tpu_type_entry_t *entry = x;
	char *err = NULL;

	if (tpu_conf_type_entry_finalize(entry, &err)) {
		error("%s", err);
		xfree(err);
		return SLURM_ERROR;
	}

	return SLURM_SUCCESS;
}

static int _match_duplicate(void *x, void *key)
{
	tpu_type_entry_t *other = x;
	tpu_type_entry_t *entry = key;

	if (other == entry)
		return 0;

	return (!xstrcmp(other->type, entry->type) ||
		(other->pci_device_id && entry->pci_device_id &&
		 !xstrcasecmp(other->vendor_id, entry->vendor_id) &&
		 !xstrcasecmp(other->pci_device_id, entry->pci_device_id)));
}

/*
 * Lookups return the first match, so a family name or a PCI id declared twice
 * would silently hide the second declaration.
 */
static int _check_duplicate(void *x, void *arg)
{
	tpu_type_entry_t *entry = x;
	list_t *tpu_types = arg;
	tpu_type_entry_t *other;

	if (!(other = list_find_first_ro(tpu_types, _match_duplicate, entry)))
		return SLURM_SUCCESS;

	if (!xstrcmp(other->type, entry->type))
		error("%s declares accelerator family %s more than once",
		      TPU_CONF_FILE, entry->type);
	else
		error("%s declares PCI device %s:%s for both %s and %s",
		      TPU_CONF_FILE, entry->vendor_id, entry->pci_device_id,
		      other->type, entry->type);

	return SLURM_ERROR;
}

extern int tpu_conf_load(tpu_conf_t **conf_ptr)
{
	tpu_conf_t *conf = NULL;
	int rc;

	xassert(conf_ptr);

	*conf_ptr = NULL;

	if ((rc = CONF_PARSE(TPU_CONF_PTR, TPU_CONF_FILE, conf))) {
		tpu_conf_destroy(conf);
		return rc;
	}

	/*
	 * An empty or comment-only file parses successfully but yields no
	 * object. Treat that the same as a missing file so callers only have
	 * to handle one "nothing configured" case.
	 */
	if (!conf || !conf->tpu_types || list_is_empty(conf->tpu_types)) {
		tpu_conf_destroy(conf);
		return ENOENT;
	}

	if (list_for_each(conf->tpu_types, _finalize_entry, NULL) < 0) {
		tpu_conf_destroy(conf);
		return EINVAL;
	}

	if (list_for_each_ro(conf->tpu_types, _check_duplicate,
			     conf->tpu_types) < 0) {
		tpu_conf_destroy(conf);
		return EINVAL;
	}

	*conf_ptr = conf;

	return SLURM_SUCCESS;
}

extern bool tpu_conf_shape_parse(const char *shape, tpu_geometry_t *geom)
{
	const char *p = shape;
	char *end = NULL;
	uint64_t chips = 1;

	xassert(geom);

	memset(geom, 0, sizeof(*geom));

	if (!shape || !shape[0])
		return false;

	while (true) {
		unsigned long val;

		if (geom->dim_cnt >= TPU_MAX_DIMS)
			return false;

		/*
		 * strtoul() accepts leading whitespace and a sign, and wraps
		 * negatives around to huge positives. Require canonical
		 * positive integers with no leading zeros.
		 */
		if ((*p < '1') || (*p > '9'))
			return false;

		val = strtoul(p, &end, 10);
		if (!val || (val > UINT32_MAX))
			return false;

		chips *= val;
		if (chips > UINT32_MAX)
			return false;

		geom->dim[geom->dim_cnt++] = val;

		if (!*end)
			break;
		if (*end != 'x')
			return false;
		p = end + 1;
	}

	return true;
}

extern bool tpu_conf_layout_parse(const char *layout, char **family,
				  tpu_geometry_t *geom)
{
	const char *sep;

	xassert(family);
	xassert(geom);

	*family = NULL;
	memset(geom, 0, sizeof(*geom));

	if (!layout)
		return false;

	if (!(sep = xstrchr(layout, '=')) || (sep == layout))
		return false;

	if (!tpu_conf_shape_parse(sep + 1, geom))
		return false;

	*family = xstrndup(layout, sep - layout);

	return true;
}

extern char *tpu_conf_geometry_str(const tpu_geometry_t *geom)
{
	char *str = NULL;

	for (int i = 0; i < geom->dim_cnt; i++)
		xstrfmtcat(str, "%s%u", (i ? "x" : ""), geom->dim[i]);

	if (!str)
		str = xstrdup("(empty)");

	return str;
}

extern uint32_t tpu_conf_geometry_chips(const tpu_geometry_t *geom)
{
	uint32_t chips = 1;

	if (!geom->dim_cnt)
		return 0;

	for (int i = 0; i < geom->dim_cnt; i++)
		chips *= geom->dim[i];

	return chips;
}

extern int tpu_conf_type_entry_finalize(tpu_type_entry_t *entry, char **err)
{
	uint32_t segment_chips;

	xassert(entry);
	xassert(err && !*err);

	/* A family with no type can never be matched by a --layout request */
	if (!entry->type || !entry->type[0]) {
		xstrfmtcat(*err, "An accelerator family in %s has no type",
			   TPU_CONF_FILE);
		return SLURM_ERROR;
	}

	if (!entry->vendor_id)
		entry->vendor_id = xstrdup(TPU_DEFAULT_VENDOR_ID);

	if (!tpu_conf_shape_parse(entry->shape_per_node, &entry->node_geom)) {
		xstrfmtcat(
			*err,
			"Accelerator family %s has a malformed shape_per_node '%s'",
			entry->type, entry->shape_per_node);
		return SLURM_ERROR;
	}

	if (!tpu_conf_shape_parse(entry->shape_per_segment,
				  &entry->segment_geom)) {
		xstrfmtcat(
			*err,
			"Accelerator family %s has a malformed shape_per_segment '%s'",
			entry->type, entry->shape_per_segment);
		return SLURM_ERROR;
	}

	if (entry->node_geom.dim_cnt != entry->segment_geom.dim_cnt) {
		xstrfmtcat(
			*err,
			"Accelerator family %s describes a %d-dimensional node but a %d-dimensional segment",
			entry->type, entry->node_geom.dim_cnt,
			entry->segment_geom.dim_cnt);
		return SLURM_ERROR;
	}

	/* Accelerator fabrics are 2D meshes or 3D tori */
	if (entry->node_geom.dim_cnt < 2) {
		xstrfmtcat(
			*err,
			"Accelerator family %s describes a %d-dimensional fabric, only 2- and 3-dimensional fabrics are supported",
			entry->type, entry->node_geom.dim_cnt);
		return SLURM_ERROR;
	}

	entry->dimensions = entry->node_geom.dim_cnt;
	entry->chips_per_node = tpu_conf_geometry_chips(&entry->node_geom);
	segment_chips = tpu_conf_geometry_chips(&entry->segment_geom);

	/*
	 * A segment is built out of whole nodes, so the node shape has to
	 * tile it exactly along every axis.
	 */
	for (int i = 0; i < entry->dimensions; i++) {
		if (entry->segment_geom.dim[i] % entry->node_geom.dim[i]) {
			xstrfmtcat(
				*err,
				"Accelerator family %s segment %s is not a whole number of %s nodes",
				entry->type, entry->shape_per_segment,
				entry->shape_per_node);
			return SLURM_ERROR;
		}
	}

	entry->nodes_per_segment = segment_chips / entry->chips_per_node;

	if (!entry->nodes_per_pod ||
	    (entry->nodes_per_pod % entry->nodes_per_segment)) {
		xstrfmtcat(
			*err,
			"Accelerator family %s has nodes_per_pod %u, which is not a whole number of %u node segments",
			entry->type, entry->nodes_per_pod,
			entry->nodes_per_segment);
		return SLURM_ERROR;
	}

	return SLURM_SUCCESS;
}

extern int tpu_conf_layout_validate(const tpu_type_entry_t *entry,
				    const tpu_geometry_t *geom, uint32_t *nodes,
				    char **err)
{
	char *shape;
	uint32_t chips;
	int rc = SLURM_ERROR;

	xassert(entry);
	xassert(geom);
	xassert(nodes);
	xassert(err && !*err);

	*nodes = 0;

	shape = tpu_conf_geometry_str(geom);

	if (!entry->dimensions || !entry->chips_per_node) {
		xstrfmtcat(
			*err,
			"Accelerator family %s is not usable; check tpu.yaml",
			entry->type);
		goto cleanup;
	}

	if (geom->dim_cnt != entry->dimensions) {
		xstrfmtcat(
			*err,
			"Accelerator family %s is %u-dimensional and needs a %s layout, got %s",
			entry->type, entry->dimensions,
			(entry->dimensions == 2) ? "<x>x<y>" : "<x>x<y>x<z>",
			shape);
		goto cleanup;
	}

	/*
	 * Nodes are allocated whole, and a node's chips have a fixed shape, so
	 * the node shape must tile the request along every axis. Comparing
	 * chip totals instead would accept tpu7x=1x2x4 (eight chips, two
	 * nodes) even though the first axis is not a multiple of 2.
	 */
	for (int i = 0; i < geom->dim_cnt; i++) {
		if (geom->dim[i] % entry->node_geom.dim[i]) {
			xstrfmtcat(
				*err,
				"A %s layout must be a whole number of %s nodes along every axis, got %s",
				entry->type, entry->shape_per_node, shape);
			goto cleanup;
		}
	}

	chips = tpu_conf_geometry_chips(geom);
	*nodes = chips / entry->chips_per_node;

	if (*nodes == 1) {
		/*
		 * A single-node request only needs to match shape_per_node,
		 * which the divisibility check above already verified.
		 */
		rc = SLURM_SUCCESS;
		goto cleanup;
	}

	/*
	 * Multi-node request dimensions must be in non-decreasing order
	 * (e.g. 2x2x4, not 4x2x2); dimensions are rejected rather than
	 * reordered.
	 */
	for (int i = 1; i < geom->dim_cnt; i++) {
		if (geom->dim[i - 1] > geom->dim[i]) {
			xstrfmtcat(
				*err,
				"Malformed TPU layout %s: dimensions must be in non-decreasing order",
				shape);
			goto cleanup;
		}
	}

	if (*nodes <= entry->nodes_per_segment) {
		/*
		 * Within a single segment, every axis must divide
		 * shape_per_segment evenly.
		 */
		for (int i = 0; i < geom->dim_cnt; i++) {
			if (entry->segment_geom.dim[i] % geom->dim[i]) {
				xstrfmtcat(
					*err,
					"A %s layout of %u nodes or fewer must divide the %s segment along every axis, got %s",
					entry->type, entry->nodes_per_segment,
					entry->shape_per_segment, shape);
				goto cleanup;
			}
		}

		/* The longest axis cannot exceed twice the shortest axis. */
		if (geom->dim[geom->dim_cnt - 1] > (2 * geom->dim[0])) {
			xstrfmtcat(
				*err,
				"A %s layout of %u nodes or fewer must have its largest dimension at most twice its smallest, got %s",
				entry->type, entry->nodes_per_segment, shape);
			goto cleanup;
		}
	} else {
		/*
		 * Multi-segment requests are stitched together from whole
		 * segments over OCS, so every axis must be a multiple of
		 * shape_per_segment.
		 */
		for (int i = 0; i < geom->dim_cnt; i++) {
			if (geom->dim[i] % entry->segment_geom.dim[i]) {
				xstrfmtcat(
					*err,
					"A %s layout larger than %u nodes must be a whole number of %s segments along every axis, got %s",
					entry->type, entry->nodes_per_segment,
					entry->shape_per_segment, shape);
				goto cleanup;
			}
		}
	}

	if (*nodes > entry->nodes_per_pod) {
		xstrfmtcat(*err,
			   "A request for %u nodes exceeds the %u-node %s pod",
			   *nodes, entry->nodes_per_pod, entry->type);
		goto cleanup;
	}

	rc = SLURM_SUCCESS;

cleanup:
	xfree(shape);
	if (rc != SLURM_SUCCESS)
		*nodes = 0;

	return rc;
}
