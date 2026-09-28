/*****************************************************************************\
 *  gres_shard_dmem-test.c - Test the dmem region state machine of gres/shard.
 *****************************************************************************
 *  The device memory states of a sharding device are decided entirely from a
 *  list of dmem regions and the PCI address of each device. Feeding both by
 *  hand covers the states that real hardware cannot be made to produce on
 *  demand, and needs no GPU, no dmem cgroup controller and no kernel support.
 *
 *  The plugin sources are included so that the state machine, which is static,
 *  can be driven directly instead of through a whole node configuration load.
 *  All of them are needed: a plugin of a shared gres is built of the three,
 *  and leaving one out would only fail once a loader bound the test eagerly.
\*****************************************************************************/

#include <check.h>
#include <stdlib.h>

#include "src/plugins/gres/common/gres_common.c"
#include "src/plugins/gres/common/gres_c_s.c"
#include "src/plugins/gres/shard/gres_shard.c"

#define GIB (1024 * 1024 * 1024)

static list_t *regions = NULL;
static list_t *devices = NULL;
static list_t *gres_conf = NULL;

static void _add_region(char *name, uint64_t capacity)
{
	cgroup_limits_t *region = xmalloc(sizeof(*region));

	region->dmem_region = xstrdup(name);
	region->limit_in_bytes = capacity;
	list_append(regions, region);
}

static gres_device_t *_add_device(char *path, char *pci_addr, int index)
{
	gres_device_t *gres_device = xmalloc(sizeof(*gres_device));
	gres_slurmd_conf_t *conf = xmalloc(sizeof(*conf));

	gres_device->path = xstrdup(path);
	gres_device->index = index;
	gres_device->dev_num = _compute_local_id(path);
	list_append(devices, gres_device);

	/*
	 * Discovery resolves the address from the gpu record of the device,
	 * not from the device itself, so the record carries what AutoDetect
	 * would have stored.
	 */
	conf->name = xstrdup("gpu");
	conf->file = xstrdup(path);
	conf->pci_addr = xstrdup(pci_addr);
	list_append(gres_conf, conf);

	return gres_device;
}

/* Exclude a device from enforcement, as DmemRegion=off does. */
static void _exclude_device(char *path)
{
	find_conf_gpu_t find = { .file = path, .sharing_name = "gpu" };
	gres_slurmd_conf_t *conf =
		list_find_first(gres_conf, _find_conf_gpu, &find);

	conf->dmem_region = xstrdup(DMEM_REGION_OFF);
}

/* Give a device its shard count, the way a gres.conf shard record would. */
static gres_slurmd_conf_t *_conf_rec(char *name, char *file, uint64_t count)
{
	gres_slurmd_conf_t *conf = xmalloc(sizeof(*conf));

	conf->name = xstrdup(name);
	conf->file = xstrdup(file);
	conf->count = count;
	conf->config_flags = GRES_CONF_HAS_FILE | GRES_CONF_SHARED;
	conf->plugin_id = gres_build_id(name);

	return conf;
}

/* Give a device its shard count, as its gres.conf shard record would. */
static void _add_shards(gres_device_t *dev, uint64_t count)
{
	shared_dev_info_t *shared = xmalloc(sizeof(*shared));

	shared->id = dev->dev_num;
	shared->count = count;
	list_append(shared_info, shared);
}

/* Run discovery over the devices, exactly as _dmem_discover() does. */
static void _discover(void)
{
	dmem_discover_t discover = { .gres_conf_list = gres_conf,
				     .region_list = regions };

	(void) list_for_each(devices, _foreach_dmem_discover, &discover);
	gres_devices = devices;
	_dmem_check_dup_regions();
	(void) list_for_each(devices, _dmem_set_dev_slices, NULL);
}

static void _setup(void)
{
	regions = list_create(NULL);
	devices = list_create(NULL);
	gres_conf = list_create(NULL);
	shared_info = list_create(xfree_ptr);
	slurm_cgroup_conf.constrain_device_memory = true;
}

static void _teardown(void)
{
	FREE_NULL_LIST(shared_info);
	gres_devices = NULL;
}

START_TEST(test_region_matched_by_pci_is_usable)
{
	gres_device_t *dev;

	_add_region("drm/0000:6c:00.0/vram", 2 * (uint64_t) GIB);
	dev = _add_device("/dev/dri/renderD128", "0000:6c:00.0", 0);
	_add_shards(dev, 8);

	_discover();

	ck_assert_int_eq(dev->dmem->state, GRES_DMEM_USABLE);
	ck_assert_str_eq(dev->dmem->region, "drm/0000:6c:00.0/vram");
	ck_assert_msg(dev->dmem->capacity == (2 * (uint64_t) GIB),
		      "capacity is %"PRIu64, dev->dmem->capacity);
	ck_assert_msg(dev->dmem->shards == 8, "shards is %"PRIu64,
		      dev->dmem->shards);
	ck_assert_msg(dev->dmem->slice == (256 * 1024 * 1024),
		      "slice is %"PRIu64, dev->dmem->slice);
}
END_TEST

START_TEST(test_device_without_region_is_none)
{
	gres_device_t *dev;

	_add_region("drm/0000:6c:00.0/vram", 2 * (uint64_t) GIB);
	dev = _add_device("/dev/dri/renderD129", "0000:01:00.0", 0);
	_add_shards(dev, 8);

	_discover();

	ck_assert_int_eq(dev->dmem->state, GRES_DMEM_NONE);
	ck_assert_msg(!dev->dmem->region, "region is %s", dev->dmem->region);
	ck_assert_msg(!dev->dmem->slice, "slice is %"PRIu64, dev->dmem->slice);
}
END_TEST

START_TEST(test_two_regions_of_one_device_are_ambiguous)
{
	gres_device_t *dev;

	_add_region("drm/0000:6c:00.0/vram", 2 * (uint64_t) GIB);
	_add_region("drm/0000:6c:00.0/gtt", 4 * (uint64_t) GIB);
	dev = _add_device("/dev/dri/renderD128", "0000:6c:00.0", 0);
	_add_shards(dev, 8);

	_discover();

	ck_assert_int_eq(dev->dmem->state, GRES_DMEM_AMBIGUOUS);
	ck_assert_msg(!dev->dmem->slice, "slice is %"PRIu64, dev->dmem->slice);
}
END_TEST

START_TEST(test_one_region_of_two_devices_is_shared)
{
	gres_device_t *dev1, *dev2;

	_add_region("drm/0000:6c:00.0/vram", 2 * (uint64_t) GIB);
	dev1 = _add_device("/dev/dri/renderD128", "0000:6c:00.0", 0);
	dev2 = _add_device("/dev/dri/renderD129", "0000:6c:00.0", 1);
	_add_shards(dev1, 8);
	_add_shards(dev2, 8);

	_discover();

	/*
	 * Neither device may be enforced: a limit on the region would bound
	 * both of them, so both are reported as sharing it.
	 */
	ck_assert_int_eq(dev1->dmem->state, GRES_DMEM_SHARED);
	ck_assert_int_eq(dev2->dmem->state, GRES_DMEM_SHARED);
	ck_assert_msg(!dev1->dmem->slice, "slice is %"PRIu64, dev1->dmem->slice);
	ck_assert_msg(!dev2->dmem->slice, "slice is %"PRIu64, dev2->dmem->slice);
}
END_TEST

/*
 * A device of a region but no shards has nothing to enforce. It keeps the
 * region, so that the startup log can name it, and holds no slice, so that
 * every limit write skips it. The log of such a device is held to the same
 * rule by the startup log contract of test_144_18.
 */
START_TEST(test_device_without_shards_has_no_slice)
{
	gres_device_t *dev;

	_add_region("drm/0000:6c:00.0/vram", 2 * (uint64_t) GIB);
	dev = _add_device("/dev/dri/renderD128", "0000:6c:00.0", 0);
	/* No shard record for this device. */

	_discover();

	ck_assert_msg(!dev->dmem->shards, "shards is %"PRIu64,
		      dev->dmem->shards);
	ck_assert_msg(!dev->dmem->slice, "slice is %"PRIu64, dev->dmem->slice);
	ck_assert_str_eq(dev->dmem->region, "drm/0000:6c:00.0/vram");
}
END_TEST

START_TEST(test_dmemregion_off_excludes_the_device)
{
	gres_device_t *dev;

	_add_region("drm/0000:6c:00.0/vram", 2 * (uint64_t) GIB);
	dev = _add_device("/dev/dri/renderD128", "0000:6c:00.0", 0);
	_exclude_device("/dev/dri/renderD128");
	_add_shards(dev, 8);

	_discover();

	/* The region is still detected, so the log can name what is excluded. */
	ck_assert_int_eq(dev->dmem->state, GRES_DMEM_EXCLUDED);
	ck_assert_str_eq(dev->dmem->region, "drm/0000:6c:00.0/vram");
	ck_assert_msg(!dev->dmem->slice, "slice is %"PRIu64, dev->dmem->slice);
}
END_TEST

/*
 * Every shared gres of a node reaches shared_info, and each shared plugin
 * holds its own copy of that list, so the count a device holds for mps must
 * never be taken for its shards.
 */
START_TEST(test_mps_count_of_the_device_is_not_taken)
{
	list_t *gres_conf = list_create(NULL);
	gres_device_t *dev;

	_add_region("drm/0000:6c:00.0/vram", 2 * (uint64_t) GIB);
	dev = _add_device("/dev/dri/renderD128", "0000:6c:00.0", 0);

	list_append(gres_conf, _conf_rec("mps", dev->path, 5));
	list_append(gres_conf, _conf_rec("shard", dev->path, 8));
	_build_shared_dev_info(gres_conf, gres_build_id("shard"));

	_discover();

	ck_assert_msg(dev->dmem->shards == 8, "shards is %"PRIu64,
		      dev->dmem->shards);
	ck_assert_msg(dev->dmem->slice == (256 * 1024 * 1024),
		      "slice is %"PRIu64, dev->dmem->slice);
}
END_TEST

static Suite *dmem_suite(void)
{
	Suite *s = suite_create("gres_shard_dmem");
	TCase *tc = tcase_create("states");

	tcase_add_checked_fixture(tc, _setup, _teardown);
	tcase_add_test(tc, test_region_matched_by_pci_is_usable);
	tcase_add_test(tc, test_device_without_region_is_none);
	tcase_add_test(tc, test_two_regions_of_one_device_are_ambiguous);
	tcase_add_test(tc, test_one_region_of_two_devices_is_shared);
	tcase_add_test(tc, test_dmemregion_off_excludes_the_device);
	tcase_add_test(tc, test_mps_count_of_the_device_is_not_taken);
	tcase_add_test(tc, test_device_without_shards_has_no_slice);
	suite_add_tcase(s, tc);

	return s;
}

int main(void)
{
	int failed = 0;
	SRunner *sr = srunner_create(dmem_suite());

	srunner_run_all(sr, CK_VERBOSE);
	failed = srunner_ntests_failed(sr);
	srunner_free(sr);

	return (failed == 0) ? EXIT_SUCCESS : EXIT_FAILURE;
}
