/*****************************************************************************\
 *  tpu-layout-test.c - Unit tests for --layout parsing and geometry checks.
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
 *  Slurm is distributed in the hope that it will be useful, but WITHOUT ANY
 *  WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 *  FOR A PARTICULAR PURPOSE.  See the GNU General Public License for more
 *  details.
 *
 *  You should have received a copy of the GNU General Public License along
 *  with Slurm; if not, write to the Free Software Foundation, Inc.,
 *  51 Franklin Street, Fifth Floor, Boston, MA 02110-1301  USA.
\*****************************************************************************/

#include <stdlib.h>

#include "src/common/log.h"
#include "src/common/macros.h"
#include "src/common/tpu_conf.h"
#include "src/common/xmalloc.h"
#include "src/common/xstring.h"

#include <check.h>

/*
 * Representative 3D torus and 2D mesh families from etc/tpu.yaml.example.
 */
static tpu_type_entry_t tpu7x = {
	.nodes_per_pod = 2304,
	.shape_per_node = "2x2x1",
	.shape_per_segment = "4x4x4",
	.type = "tpu7x",
	.vendor_id = TPU_DEFAULT_VENDOR_ID,
};

static tpu_type_entry_t v6e = {
	.nodes_per_pod = 64,
	.shape_per_node = "2x2",
	.shape_per_segment = "16x16",
	.type = "v6e",
	.vendor_id = TPU_DEFAULT_VENDOR_ID,
};

static void _finalize(tpu_type_entry_t *entry)
{
	char *err = NULL;

	ck_assert_msg(!tpu_conf_type_entry_finalize(entry, &err),
		      "%s should be a valid family, rejected with: %s",
		      entry->type, err);
	xfree(err);
}

static void _check_accept(tpu_type_entry_t *entry, const char *shape,
			  uint32_t want_nodes)
{
	tpu_geometry_t geom;
	char *err = NULL;
	uint32_t nodes = 0;

	ck_assert_msg(tpu_conf_shape_parse(shape, &geom),
		      "%s did not parse",
		      shape);

	ck_assert_msg(!tpu_conf_layout_validate(entry, &geom, &nodes, &err),
		      "%s=%s should be valid, rejected with: %s", entry->type,
		      shape, err);
	ck_assert_msg(nodes == want_nodes, "%s=%s sized %u nodes, expected %u",
		      entry->type, shape, nodes, want_nodes);
	xfree(err);
}

static void _check_reject(tpu_type_entry_t *entry, const char *shape)
{
	tpu_geometry_t geom;
	char *err = NULL;
	uint32_t nodes = 0;

	ck_assert_msg(tpu_conf_shape_parse(shape, &geom),
		      "%s did not parse",
		      shape);

	ck_assert_msg(tpu_conf_layout_validate(entry, &geom, &nodes, &err),
		      "%s=%s should have been rejected", entry->type, shape);
	ck_assert_msg(err && err[0],
		      "%s=%s was rejected without an explanation", entry->type,
		      shape);
	ck_assert_msg(!nodes, "%s=%s was rejected but still sized %u nodes",
		      entry->type, shape, nodes);
	xfree(err);
}

START_TEST(test_layout_parse_valid)
{
	struct {
		const char *layout;
		const char *family;
		int dim_cnt;
		uint32_t dim[3];
	} vectors[] = {
		{ "tpu7x=2x2x4", "tpu7x", 3, { 2, 2, 4 } },
		{ "v6e=4x8", "v6e", 2, { 4, 8 } },
		{ "tpu7x=16x24x24", "tpu7x", 3, { 16, 24, 24 } },
		/*
		 * Shape well formed but meaningless. Parsing only checks the
		 * syntax; tpu_conf_layout_validate() is what knows the family.
		 */
		{ "whatever=1", "whatever", 1, { 1 } },
	};

	for (int i = 0; i < ARRAY_SIZE(vectors); i++) {
		tpu_geometry_t geom;
		char *family = NULL;

		ck_assert_msg(tpu_conf_layout_parse(vectors[i].layout, &family,
						    &geom),
			      "%s should parse", vectors[i].layout);
		ck_assert_msg(!xstrcmp(family, vectors[i].family),
			      "%s gave family '%s', expected '%s'",
			      vectors[i].layout, family, vectors[i].family);
		ck_assert_msg(geom.dim_cnt == vectors[i].dim_cnt,
			      "%s gave %d dimensions, expected %d",
			      vectors[i].layout, geom.dim_cnt,
			      vectors[i].dim_cnt);
		for (int j = 0; j < geom.dim_cnt; j++)
			ck_assert_msg(geom.dim[j] == vectors[i].dim[j],
				      "%s dimension %d is %u, expected %u",
				      vectors[i].layout, j, geom.dim[j],
				      vectors[i].dim[j]);
		xfree(family);
	}
}

END_TEST

START_TEST(test_layout_parse_invalid)
{
	const char *vectors[] = {
		NULL,
		"",
		"tpu7x", /* no shape */
		"=2x2x4", /* no family */
		"tpu7x=", /* empty shape */
		"tpu7x=2x2x4x8", /* more dimensions than any fabric has */
		"tpu7x=0x2x2", /* zero sized dimension */
		"tpu7x=02x2x2", /* leading zero */
		"tpu7x=2x2x1073741825", /* chip product overflows uint32_t */
		"tpu7x=-2x2x2", /* strtoul() would wrap this around */
		"tpu7x= 2x2x2", /* strtoul() would skip the space */
		"tpu7x=+2x2x2", /* strtoul() would accept the sign */
		"tpu7x=2y2", /* wrong separator */
		"tpu7x=2x", /* trailing separator */
		"tpu7x=2xx2", /* empty dimension */
		"tpu7x=2x2x2 ", /* trailing garbage */
		"tpu7x=2x2x2=4",
	};

	for (int i = 0; i < ARRAY_SIZE(vectors); i++) {
		tpu_geometry_t geom;
		char *family = NULL;

		ck_assert_msg(!tpu_conf_layout_parse(vectors[i],
						     &family,
						     &geom),
			      "'%s' should not parse",
			      vectors[i] ? vectors[i] : "(null)");
		ck_assert_msg(!family,
			      "'%s' failed to parse but still set a family",
			      vectors[i] ? vectors[i] : "(null)");
	}
}

END_TEST

START_TEST(test_finalize_derives_counts)
{
	_finalize(&tpu7x);
	ck_assert_int_eq(tpu7x.dimensions, 3);
	ck_assert_int_eq(tpu7x.chips_per_node, 4);
	ck_assert_int_eq(tpu7x.nodes_per_segment, 16);

	_finalize(&v6e);
	ck_assert_int_eq(v6e.dimensions, 2);
	ck_assert_int_eq(v6e.chips_per_node, 4);
	ck_assert_int_eq(v6e.nodes_per_segment, 64);
}

END_TEST

START_TEST(test_finalize_rejects_bad_config)
{
	struct {
		const char *shape_per_node;
		const char *shape_per_segment;
		uint32_t nodes_per_pod;
		const char *why;
	} vectors[] = {
		{ NULL, "4x4x4", 2304, "missing node shape" },
		{ "2x2x1", NULL, 2304, "missing segment shape" },
		{ "not a shape", "4x4x4", 2304, "unparsable node shape" },
		{ "2x2x1", "4x4", 2304, "dimension counts disagree" },
		{ "2x2x1", "3x4x4", 2304, "segment is not whole nodes" },
		{ "2x2x1", "4x4x4", 2300, "pod is not whole segments" },
		{ "4", "16", 64, "one-dimensional fabric" },
	};

	for (int i = 0; i < ARRAY_SIZE(vectors); i++) {
		tpu_type_entry_t entry = {
			.nodes_per_pod = vectors[i].nodes_per_pod,
			.shape_per_node = (char *) vectors[i].shape_per_node,
			.shape_per_segment =
				(char *) vectors[i].shape_per_segment,
			.type = "test",
		};
		char *err = NULL;

		ck_assert_msg(tpu_conf_type_entry_finalize(&entry, &err),
			      "%s should have been rejected", vectors[i].why);
		ck_assert_msg(err && err[0],
			      "%s was rejected without an explanation",
			      vectors[i].why);
		xfree(err);
		/*
		 * Set by tpu_conf_type_entry_finalize() when vendor_id is
		 * unset
		 */
		xfree(entry.vendor_id);
	}
}

END_TEST

START_TEST(test_validate_3d_accept)
{
	_finalize(&tpu7x);

	/* Exactly one node: the chips of a single host. */
	_check_accept(&tpu7x, "2x2x1", 1);

	/* Within one segment every axis must divide the 4x4x4 cube. */
	_check_accept(&tpu7x, "2x2x2", 2);
	_check_accept(&tpu7x, "2x2x4", 4);
	_check_accept(&tpu7x, "2x4x4", 8);
	_check_accept(&tpu7x, "4x4x4", 16);

	/* Beyond it, whole cubes only. */
	_check_accept(&tpu7x, "4x4x8", 32);
	_check_accept(&tpu7x, "4x8x8", 64);
	_check_accept(&tpu7x, "8x8x8", 128);
	_check_accept(&tpu7x, "4x4x12", 48);

	/* The whole pod. */
	_check_accept(&tpu7x, "16x24x24", 2304);
}

END_TEST

START_TEST(test_validate_3d_reject)
{
	_finalize(&tpu7x);

	/* Wrong number of dimensions for the fabric. */
	_check_reject(&tpu7x, "4x4");
	_check_reject(&tpu7x, "8");

	/*
	 * Not a whole number of 2x2x1 nodes along every axis, even where the
	 * chip count alone would divide evenly.
	 */
	_check_reject(&tpu7x, "1x1x1");
	_check_reject(&tpu7x, "1x1x2");
	_check_reject(&tpu7x, "1x2x3");
	_check_reject(&tpu7x, "1x1x4");
	_check_reject(&tpu7x, "1x2x2");
	_check_reject(&tpu7x, "1x2x4");
	_check_reject(&tpu7x, "4x1x1");

	/*
	 * Dimensions must be in non-decreasing order.
	 */
	_check_reject(&tpu7x, "4x2x2");
	_check_reject(&tpu7x, "2x4x1");
	_check_reject(&tpu7x, "12x4x4");
	_check_reject(&tpu7x, "8x8x4");

	/* Does not divide the 4x4x4 segment. */
	_check_reject(&tpu7x, "2x2x8");
	_check_reject(&tpu7x, "2x2x6");
	_check_reject(&tpu7x, "2x2x3");

	/* Spans segments without being a whole number of them. */
	_check_reject(&tpu7x, "4x4x6");
	_check_reject(&tpu7x, "2x8x8");

	/* Larger than the pod. */
	_check_reject(&tpu7x, "16x24x28");
	_check_reject(&tpu7x, "16x16x256");
}

END_TEST

START_TEST(test_validate_2d_accept)
{
	_finalize(&v6e);

	_check_accept(&v6e, "2x2", 1);
	_check_accept(&v6e, "2x4", 2);
	_check_accept(&v6e, "4x4", 4);
	_check_accept(&v6e, "4x8", 8);
	_check_accept(&v6e, "8x8", 16);
	_check_accept(&v6e, "8x16", 32);
	_check_accept(&v6e, "16x16", 64);
}

END_TEST

START_TEST(test_validate_2d_reject)
{
	_finalize(&v6e);

	/* A 2D mesh takes exactly two dimensions. */
	_check_reject(&v6e, "2x2x2");
	_check_reject(&v6e, "4");

	/* Not a whole number of 2x2 nodes along every axis. */
	_check_reject(&v6e, "1x1");
	_check_reject(&v6e, "1x2");
	_check_reject(&v6e, "3x4");

	/* Does not divide the 16x16 mesh. */
	_check_reject(&v6e, "4x12");
	_check_reject(&v6e, "6x8");

	/* Out of order. */
	_check_reject(&v6e, "8x4");
	_check_reject(&v6e, "16x8");

	/*
	 * Divides 16x16 and is a multiple of 2x2, but exceeds the 2:1
	 * intra-segment aspect-ratio bound (4, 8, and 16 v6e nodes are
	 * wired as 4x4, 4x8, and 8x8, not 2x8, 2x16, or 4x16).
	 */
	_check_reject(&v6e, "2x8");
	_check_reject(&v6e, "2x16");
	_check_reject(&v6e, "4x16");

	/*
	 * 8x32 is 256 chips on 64 nodes, exactly the pod's capacity, but a
	 * v6e pod is treated as a single 16x16 segment with no OCS tier.
	 */
	_check_reject(&v6e, "8x32");
	_check_reject(&v6e, "16x32");
}

END_TEST

START_TEST(test_validate_unfinalized)
{
	tpu_type_entry_t entry = {
		.nodes_per_pod = 2304,
		.shape_per_node = "2x2x1",
		.shape_per_segment = "4x4x4",
		.type = "tpu7x",
	};
	tpu_geometry_t geom;
	char *err = NULL;
	uint32_t nodes = 0;

	ck_assert(tpu_conf_shape_parse("2x2x4", &geom));

	/*
	 * An entry that never made it through finalize must be reported, not
	 * treated as a zero dimensional fabric.
	 */
	ck_assert(tpu_conf_layout_validate(&entry, &geom, &nodes, &err));
	ck_assert(err && err[0]);
	ck_assert_int_eq(nodes, 0);
	xfree(err);
}

END_TEST

START_TEST(test_geometry_helpers)
{
	tpu_geometry_t geom;
	char *str;

	ck_assert(tpu_conf_shape_parse("2x4x8", &geom));
	ck_assert_int_eq(tpu_conf_geometry_chips(&geom), 64);

	str = tpu_conf_geometry_str(&geom);
	ck_assert_str_eq(str, "2x4x8");
	xfree(str);

	ck_assert(tpu_conf_shape_parse("16x16", &geom));
	ck_assert_int_eq(tpu_conf_geometry_chips(&geom), 256);

	str = tpu_conf_geometry_str(&geom);
	ck_assert_str_eq(str, "16x16");
	xfree(str);
}

END_TEST

START_TEST(test_vendor_id_and_pci_lookup)
{
	tpu_type_entry_t def_vendor = {
		.nodes_per_pod = 64,
		.pci_device_id = "0x006f",
		.shape_per_node = "2x2",
		.shape_per_segment = "16x16",
		.type = "v6e",
	};
	tpu_type_entry_t custom_vendor = {
		.nodes_per_pod = 64,
		.pci_device_id = "0x0099",
		.shape_per_node = "2x2",
		.shape_per_segment = "16x16",
		.type = "custom",
		.vendor_id = "0x1234",
	};
	tpu_conf_t conf = { 0 };

	_finalize(&def_vendor);
	_finalize(&custom_vendor);

	ck_assert_str_eq(def_vendor.vendor_id, TPU_DEFAULT_VENDOR_ID);
	ck_assert_str_eq(custom_vendor.vendor_id, "0x1234");

	conf.tpu_types = list_create(NULL);
	list_append(conf.tpu_types, &def_vendor);
	list_append(conf.tpu_types, &custom_vendor);

	ck_assert(tpu_conf_find_pci_id(&conf, "0x1ae0", "0x006F") ==
		  &def_vendor);
	ck_assert(tpu_conf_find_pci_id(&conf, "0x1234", "0x0099") ==
		  &custom_vendor);
	ck_assert(!tpu_conf_find_pci_id(&conf, "0x1234", "0x006f"));
	ck_assert(!tpu_conf_find_pci_id(&conf, "0x1ae0", "0x0099"));

	FREE_NULL_LIST(conf.tpu_types);
	xfree(def_vendor.vendor_id);
}

END_TEST

int main(void)
{
	int number_failed;

	log_options_t log_opts = LOG_OPTS_INITIALIZER;
	log_opts.stderr_level = LOG_LEVEL_DEBUG5;
	log_init("tpu-layout-test", log_opts, 0, NULL);

	Suite *s = suite_create("tpu-layout");
	TCase *tc_core = tcase_create("tpu-layout");

	tcase_add_test(tc_core, test_layout_parse_valid);
	tcase_add_test(tc_core, test_layout_parse_invalid);
	tcase_add_test(tc_core, test_finalize_derives_counts);
	tcase_add_test(tc_core, test_finalize_rejects_bad_config);
	tcase_add_test(tc_core, test_validate_3d_accept);
	tcase_add_test(tc_core, test_validate_3d_reject);
	tcase_add_test(tc_core, test_validate_2d_accept);
	tcase_add_test(tc_core, test_validate_2d_reject);
	tcase_add_test(tc_core, test_validate_unfinalized);
	tcase_add_test(tc_core, test_geometry_helpers);
	tcase_add_test(tc_core, test_vendor_id_and_pci_lookup);

	suite_add_tcase(s, tc_core);

	SRunner *sr = srunner_create(s);

	srunner_run_all(sr, CK_ENV);
	number_failed = srunner_ntests_failed(sr);
	srunner_free(sr);

	return (number_failed == 0) ? EXIT_SUCCESS : EXIT_FAILURE;
}
