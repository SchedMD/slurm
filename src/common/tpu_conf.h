/*****************************************************************************\
 *  tpu_conf.h - TPU accelerator configuration (tpu.yaml) definitions.
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

#ifndef _TPU_CONF_H
#define _TPU_CONF_H

#include <stdbool.h>
#include <stdint.h>

#include "src/common/list.h"

/* Name of the configuration file, resolved against $SYSCONFDIR. */
#define TPU_CONF_FILE "tpu.yaml"

/* Default PCI vendor ID for Google TPU devices. */
#define TPU_DEFAULT_VENDOR_ID "0x1ae0"

/* Maximum number of dimensions any supported accelerator fabric has. */
#define TPU_MAX_DIMS 3

/* A chip geometry, e.g. "2x2x4". */
typedef struct {
	uint32_t dim[TPU_MAX_DIMS];
	int dim_cnt;
} tpu_geometry_t;

/*
 * Description of a single TPU accelerator family, as declared by one entry of
 * the "tpu_types" list in tpu.yaml.
 *
 * Family names are strictly canonical (e.g. "tpu7x", "v6e"); no aliases or
 * shorthand spellings are accepted. See tpu.yaml.5.
 *
 * The fabric is described by two shapes rather than by chip and node counts.
 * A count cannot express a shape -- 16 chips at 4 chips per node is 4 nodes,
 * which could be 2x2x4 or 1x4x4 -- and the shapes are what the geometry rules
 * are actually stated in terms of.
 */
typedef struct {
	/* Configured in tpu.yaml: */
	uint32_t nodes_per_pod; /* pod limit; largest allocatable job */
	char *pci_device_id; /* PCI device id, e.g. "0x0076" */
	char *shape_per_node; /* chips attached to one node, e.g. "2x2x1" */
	char *shape_per_segment; /* one segment, e.g. "4x4x4" */
	char *type; /* canonical family name, e.g. "tpu7x" */
	char *vendor_id; /* PCI vendor id, e.g. "0x1ae0" */

	/* Derived by tpu_type_entry_finalize(): */
	uint32_t chips_per_node;
	uint32_t dimensions; /* 2 for a 2D mesh, 3 for a 3D torus */
	tpu_geometry_t node_geom;
	uint32_t nodes_per_segment;
	tpu_geometry_t segment_geom;
} tpu_type_entry_t;

typedef struct {
	list_t *tpu_types; /* list of tpu_type_entry_t* */
} tpu_conf_t;

/*
 * list_create() destructor for a tpu_type_entry_t.
 * IN x - tpu_type_entry_t* to free
 */
extern void tpu_type_entry_destroy(void *x);

/*
 * Free a tpu_conf_t and everything it owns.
 * IN conf - configuration to free, may be NULL
 */
extern void tpu_conf_destroy(tpu_conf_t *conf);

/*
 * Look up an accelerator family by its canonical name.
 * IN conf - parsed tpu.yaml contents, may be NULL
 * IN type - canonical family name to find, may be NULL
 * RET the matching entry, or NULL if not configured. The returned pointer is
 *	owned by conf and must not be freed by the caller.
 */
extern tpu_type_entry_t *tpu_conf_find_type(tpu_conf_t *conf, const char *type);

/*
 * Build a comma separated list of the configured family names, for use in
 * error messages directed at end users.
 * IN conf - parsed tpu.yaml contents, may be NULL
 * RET xmalloc()'d string, caller must xfree(). Never NULL.
 */
extern char *tpu_conf_type_list_str(tpu_conf_t *conf);

/*
 * Look up an accelerator family by the PCI vendor and device id of its chips.
 *
 * This is the reverse of tpu_conf_find_type(), for gres/tpu, which learns the
 * vendor and device id from sysfs and needs to name the family they belong to.
 *
 * IN conf - parsed tpu.yaml contents, may be NULL
 * IN vendor_id - vendor id as read from sysfs, e.g. "0x1ae0". Compared case
 *	insensitively; may be NULL.
 * IN pci_device_id - device id as read from sysfs, e.g. "0x0076". Compared
 *	case insensitively; may be NULL.
 * RET the matching entry, or NULL if no configured family claims that vendor
 *	and device id. The returned pointer is owned by conf and must not be
 *	freed.
 */
extern tpu_type_entry_t *tpu_conf_find_pci_id(tpu_conf_t *conf,
					      const char *vendor_id,
					      const char *pci_device_id);

/*
 * Read and parse $SYSCONFDIR/tpu.yaml.
 * OUT conf_ptr - set to the parsed configuration on success, NULL otherwise.
 *	Caller must tpu_conf_destroy().
 * RET SLURM_SUCCESS, or ENOENT if the file is absent or declares no accelerator
 *	families, or EINVAL if an entry is invalid or a family or PCI device is
 *	declared twice, or another error from the data_parser.
 */
extern int tpu_conf_load(tpu_conf_t **conf_ptr);

/*
 * Parse an entry's configured shapes and fill in its derived fields.
 *
 * Called for every entry by tpu_conf_load(), so a bad tpu.yaml is reported
 * once at startup rather than once per job submission.
 *
 * IN/OUT entry - entry to complete
 * OUT err - on failure, an xmalloc()'d message naming the problem; caller must
 *	xfree()
 * RET SLURM_SUCCESS or SLURM_ERROR
 */
extern int tpu_type_entry_finalize(tpu_type_entry_t *entry, char **err);

/*
 * Parse a bare chip geometry of the form "<x>x<y>[x<z>]", e.g. "2x2x4".
 * IN shape - raw shape string, may be NULL
 * OUT geom - parsed dimensions on success
 * RET true if the value is well formed. Says nothing about whether the shape
 *	is physically meaningful; see tpu_layout_validate().
 */
extern bool tpu_shape_parse(const char *shape, tpu_geometry_t *geom);

/*
 * Split a --layout value of the form "<family>=<x>x<y>[x<z>]".
 * IN layout - raw option value, may be NULL
 * OUT family - xmalloc()'d family name on success, caller must xfree()
 * OUT geom - parsed dimensions on success
 * RET true if the value is well formed. A false return says nothing about
 *	whether the family or the geometry is meaningful, only that the string
 *	has the right shape.
 */
extern bool tpu_layout_parse(const char *layout, char **family,
			     tpu_geometry_t *geom);

/*
 * Render a geometry back to its "2x2x4" form.
 * IN geom - geometry to render
 * RET xmalloc()'d string, caller must xfree()
 */
extern char *tpu_geometry_str(const tpu_geometry_t *geom);

/*
 * Count the chips of a geometry.
 * IN geom - geometry to count
 * RET total chips described by the geometry
 */
extern uint32_t tpu_geometry_chips(const tpu_geometry_t *geom);

/*
 * Check a geometry against the physical constraints of an accelerator family.
 * IN entry - family the geometry is being requested from
 * IN geom - parsed geometry
 * OUT nodes - node count the geometry requires, set on success
 * OUT err - on failure, an xmalloc()'d message suitable for showing the
 *	submitting user; caller must xfree()
 * RET SLURM_SUCCESS or SLURM_ERROR
 */
extern int tpu_layout_validate(const tpu_type_entry_t *entry,
			       const tpu_geometry_t *geom, uint32_t *nodes,
			       char **err);

#endif /* !_TPU_CONF_H */
