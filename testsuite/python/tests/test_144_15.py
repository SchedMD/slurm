############################################################################
# Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
############################################################################
"""gres.conf UUID gives devices a UUID without AutoDetect.

A device UUID is otherwise only obtainable through AutoDetect. The UUID option
sets it straight from gres.conf, so Flags=env_uuid can put the UUID in the
vendor environment variables, instead of the numeric device index, with
AutoDetect=off.

There is deliberately no fake_gpus.conf here: nothing autodetects, so every UUID
seen by the tests can only have come from gres.conf.
"""

import re

import pytest

import atf

GPU_0 = "GPU-11111111-1111-1111-1111-111111111111"
GPU_1 = "GPU-22222222-2222-2222-2222-222222222222"

GRES_CONF = (
    "AutoDetect=off\n"
    f"Name=gpu File=/dev/tty0 Flags=env_uuid UUID={GPU_0}\n"
    f"Name=gpu File=/dev/tty1 Flags=env_uuid UUID={GPU_1}\n"
)


@pytest.fixture(scope="module", autouse=True)
def setup():
    atf.require_version(
        (26, 11), "sbin/slurmd", reason="MR 4271: gres.conf UUID= is new in 26.11"
    )
    atf.require_config_parameter("SelectType", "select/cons_tres")
    atf.require_config_parameter("SelectTypeParameters", "CR_CPU")
    atf.require_config_parameter_includes("GresTypes", "gpu")
    atf.require_config_parameter_includes("SlurmdParameters", "config_overrides")
    atf.require_config_parameter_includes("GresTypes", "nic")
    atf.require_config_parameter_includes("GresTypes", "shard")
    atf.require_config_parameter("MaxNodeCount", 4)
    atf.require_config_parameter("TreeWidth", 65533)
    atf.require_config_parameter_includes("SlurmctldParameters", "cloud_reg_addrs")
    atf.require_config_parameter_includes("DebugFlags", "Gres")
    # UUID fatals run a throwaway slurmd; systemd scopes are not required.
    atf.require_config_parameter("IgnoreSystemd", "yes", source="cgroup")
    atf.require_tty(0)
    atf.require_tty(1)
    atf.require_config_file("gres.conf", GRES_CONF)
    atf.require_nodes(1, [("Gres", "gpu:2"), ("CPUs", 2)])
    atf.require_slurm_running()


@pytest.mark.parametrize("var", ["CUDA_VISIBLE_DEVICES", "ROCR_VISIBLE_DEVICES"])
def test_uuid_only_used_for_runtimes_that_accept_it(var):
    """Verify the runtimes that understand "GPU-<uuid>" get a UUID that only
    gres.conf could supply.

    No vendor env flag is set in gres.conf, so all four visible-devices
    variables are set by default and this pins down which of them env_uuid is
    allowed to rewrite.
    """

    output = atf.run_job_output(f"-n1 --gpus=1 printenv {var}", fatal=True)
    value = output.strip()
    assert value in (
        GPU_0,
        GPU_1,
    ), f"{var} should be a configured UUID, got: {value!r}"


@pytest.mark.parametrize("var", ["ZE_AFFINITY_MASK", "GPU_DEVICE_ORDINAL"])
def test_uuid_not_used_for_runtimes_that_reject_it(var):
    """Verify index-only runtimes still get an index, not a UUID.

    Level Zero takes a device (or device.subdevice) index and OpenCL takes an
    ordinal. Neither can parse a UUID, so env_uuid must leave these two alone.
    """

    output = atf.run_job_output(f"-n1 --gpus=1 printenv {var}", fatal=True)
    value = output.strip()
    assert re.fullmatch(
        r"\d+", value
    ), f"{var} must stay a numeric index, got: {value!r}"


def test_env_uuid_lists_both_configured_uuids():
    """Verify a two-GPU job sees both configured UUIDs"""

    output = atf.run_job_output(
        "-n1 --gpus=2 printenv CUDA_VISIBLE_DEVICES",
        fatal=True,
    )
    value = output.strip()
    assert value in (
        f"{GPU_0},{GPU_1}",
        f"{GPU_1},{GPU_0}",
    ), f"CUDA_VISIBLE_DEVICES should list both UUIDs, got: {value!r}"


def _slurmd_pids(node_name):
    result = atf.run_command(f"pgrep -f 'slurmd -N {node_name}'", quiet=True)
    if result["exit_code"] != 0:
        return []
    return [int(pid) for pid in result["stdout"].splitlines()]


def _stop_slurmd(node_name):
    """Stop slurmd for node_name, escalating to SIGKILL if it won't exit."""

    pids = _slurmd_pids(node_name)
    if not pids:
        return
    atf.run_command(
        f"kill -TERM {' '.join(str(pid) for pid in pids)}", user="root", quiet=True
    )
    for _ in atf.timer():
        pids = _slurmd_pids(node_name)
        if not pids:
            return
    else:
        atf.run_command(
            f"kill -KILL {' '.join(str(pid) for pid in pids)}",
            user="root",
            quiet=True,
        )


def _run_throwaway_slurmd_gres(gres_conf, extra_args="-G -v", conf=None, xfail=True):
    """Overwrite gres.conf and run a throwaway slurmd against it.

    The running test slurmd already has the module gres.conf in memory, so
    swapping the file only affects this extra process. Restore afterwards.
    `-G` loads GRES and exits, which is enough to hit UUID validation.

    `conf` supplies the dynamic node's own resources. Without it slurmd has no
    expected GRES count for this node and discards every record the gres.conf
    declares, which is invisible to a test that only inspects the exit code.
    """

    gres_path = f"{atf.properties['slurm-config-dir']}/gres.conf"
    slurm_user = atf.properties["slurm-user"]
    orig = atf.run_command_output(
        f"cat {gres_path}", user=slurm_user, fatal=True, quiet=True
    )
    node_name = "UUIDfatal"
    slurmd = f"{atf.properties['slurm-sbin-dir']}/slurmd"
    try:
        atf.run_command(
            f"cat > {gres_path}",
            input=gres_conf,
            user=slurm_user,
            fatal=True,
            quiet=True,
        )
        conf_arg = f' --conf "{conf}"' if conf else ""
        result = atf.run_command(
            f"{slurmd} -N {node_name} -Z{conf_arg} {extra_args}",
            user="root",
            xfail=xfail,
            quiet=True,
        )
        # -G logs at LOG_LEVEL_QUIET, so everything lands on stdout/stderr.
        return result, f"{result['stdout']}\n{result['stderr']}"
    finally:
        atf.run_command(
            f"cat > {gres_path}",
            input=orig,
            user=slurm_user,
            fatal=True,
            quiet=True,
        )
        _stop_slurmd(node_name)


def _uuid_records(haystack, uuid):
    """Return the set of GRES names whose slurmd -G record carries `uuid`.

    slurmd -G prints one "Gres Name=<name> ... UUID=<value>" line per device,
    and omits the UUID= field entirely when the device has none. Flags= carries
    a separate flag literally spelled UUID, so match on "UUID=" plus the value
    rather than on the bare word.
    """

    return {
        match.group(1)
        for match in re.finditer(
            rf"Gres Name=(\S+)[^\n]*\bUUID={re.escape(uuid)}(?:\s|$)", haystack
        )
    }


def _assert_slurmd_fatal_on_gres(gres_conf, needle, conf=None):
    """Overwrite gres.conf, start a throwaway slurmd, expect a UUID fatal."""

    result, haystack = _run_throwaway_slurmd_gres(gres_conf, conf=conf)
    assert result["exit_code"] not in (
        0,
        110,
    ), (
        f"slurmd should have fataled on invalid UUID, "
        f"rc={result['exit_code']}, output={haystack!r}"
    )
    assert (
        needle in haystack
    ), f"expected {needle!r} in slurmd fatal output, got: {haystack!r}"


@pytest.fixture
def duplicate_cloud_gres():
    """Install duplicate cloud-node UUIDs and restore gres.conf."""

    node_name = "UUIDcloud"
    gres_path = f"{atf.properties['slurm-config-dir']}/gres.conf"
    slurm_user = atf.properties["slurm-user"]
    orig = atf.run_command_output(
        f"cat {gres_path}", user=slurm_user, fatal=True, quiet=True
    )
    duplicate_gres = (
        "AutoDetect=off\n"
        f"NodeName={node_name} Name=gpu File=/dev/tty0 UUID={GPU_0}\n"
        f"NodeName={node_name} Name=gpu File=/dev/tty1 UUID={GPU_0}\n"
    )
    atf.run_command(
        f"cat > {gres_path}",
        input=duplicate_gres,
        user=slurm_user,
        fatal=True,
        quiet=True,
    )

    yield node_name

    atf.run_command(
        f"cat > {gres_path}",
        input=orig,
        user=slurm_user,
        fatal=True,
        quiet=True,
    )
    atf.run_command(
        f"scontrol delete NodeName={node_name}",
        user=slurm_user,
        xfail=True,
        quiet=True,
    )


def test_uuid_rejects_file_range():
    """Verify UUID cannot sit on a File= that names more than one device"""

    _assert_slurmd_fatal_on_gres(
        "AutoDetect=off\n" f"Name=gpu File=/dev/tty[0-1] UUID={GPU_0}\n",
        "UUID requires a single device file",
    )


def test_uuid_rejects_missing_file():
    """Verify UUID without File or MultipleFiles is rejected"""

    _assert_slurmd_fatal_on_gres(
        f"AutoDetect=off\nName=gpu UUID={GPU_0}\n",
        "UUID requires File or MultipleFiles",
    )


def test_uuid_rejects_comma():
    """Verify UUID may not contain a comma (it is consumed as a list)"""

    _assert_slurmd_fatal_on_gres(
        "AutoDetect=off\n" f"Name=gpu File=/dev/tty0 UUID={GPU_0},{GPU_1}\n",
        "may not contain a comma",
    )


def test_uuid_rejects_empty():
    """Verify UUID= with no value is rejected.

    An empty UUID= is a gres.conf parse error (the value never reaches
    the UUID-is-empty fatal), but it must still keep slurmd from starting.
    """

    result, haystack = _run_throwaway_slurmd_gres(
        "AutoDetect=off\nName=gpu File=/dev/tty0 UUID=\n",
    )
    assert result["exit_code"] not in (
        0,
        110,
    ), (
        f"slurmd should have fataled on empty UUID, "
        f"rc={result['exit_code']}, output={haystack!r}"
    )
    assert (
        "Parse error" in haystack and "UUID=" in haystack
    ), f"expected an empty-UUID parse error, got: {haystack!r}"


def test_uuid_rejects_duplicates():
    """Verify two devices of the same GRES name may not share a UUID"""

    _assert_slurmd_fatal_on_gres(
        "AutoDetect=off\n"
        f"Name=gpu File=/dev/tty0 UUID={GPU_0}\n"
        f"Name=gpu File=/dev/tty1 UUID={GPU_0}\n",
        "Duplicate UUID",
    )


def test_slurmctld_rejects_duplicate_uuids_without_exiting(
    duplicate_cloud_gres,
):
    """Verify a bad cloud node is rejected without killing slurmctld."""

    node_name = duplicate_cloud_gres
    slurm_user = atf.properties["slurm-user"]
    result = atf.run_command(
        f"scontrol create NodeName={node_name} State=cloud",
        user=slurm_user,
        xfail=True,
        quiet=True,
    )
    assert result["exit_code"] != 0, (
        "slurmctld should reject a cloud node with duplicate UUIDs; " f"result={result}"
    )
    assert (
        atf.run_command_exit("scontrol ping", user=slurm_user, quiet=True) == 0
    ), "slurmctld exited while rejecting a duplicate UUID"
    assert (
        node_name not in atf.get_nodes()
    ), f"cloud node {node_name} remained after its gres.conf was rejected"


def test_uuid_accepted_with_multiple_files():
    """Verify UUID is valid on MultipleFiles (one physical device).

    Accepting the record is only half the promise: slurmd exits 0 whenever GRES
    parsing completes, so rc alone cannot tell "UUID kept" from "UUID parsed and
    dropped". gres.conf(5) says slurmd -G displays the UUID, so assert it is
    there.
    """

    result, haystack = _run_throwaway_slurmd_gres(
        "AutoDetect=off\n" f"Name=gpu MultipleFiles=/dev/tty0,/dev/tty1 UUID={GPU_0}\n",
        conf="Gres=gpu:1 CPUs=2",
        xfail=False,
    )
    assert result["exit_code"] == 0, (
        f"UUID with MultipleFiles should be accepted, "
        f"rc={result['exit_code']}, output={haystack!r}"
    )
    assert _uuid_records(haystack, GPU_0), (
        f"UUID {GPU_0} should be retained on the MultipleFiles record, "
        f"output={haystack!r}"
    )


def test_uuid_duplicate_ok_across_gres_names():
    """Verify UUID uniqueness is per GRES name, not cluster-wide.

    Both records must keep the UUID; accepting the config while silently
    dropping one of them would also exit 0.
    """

    result, haystack = _run_throwaway_slurmd_gres(
        "AutoDetect=off\n"
        f"Name=gpu File=/dev/tty0 UUID={GPU_0}\n"
        f"Name=nic File=/dev/tty1 UUID={GPU_0}\n",
        conf="Gres=gpu:1,nic:1 CPUs=2",
        xfail=False,
    )
    assert result["exit_code"] == 0, (
        f"the same UUID on gpu and nic should be accepted, "
        f"rc={result['exit_code']}, output={haystack!r}"
    )
    names = _uuid_records(haystack, GPU_0)
    assert "gpu" in names and "nic" in names, (
        f"both the gpu and nic records should keep UUID {GPU_0}, "
        f"got records {names}, output={haystack!r}"
    )


def test_uuid_rejects_duplicates_on_non_gpu_gres():
    """Verify UUID duplicate validation is not gpu-specific.

    gres.conf(5) documents no exemption for other GRES types, and the
    gres.conf parser validates UUID= for every Name=/NodeName= record
    regardless of GRES name.
    """

    _assert_slurmd_fatal_on_gres(
        "AutoDetect=off\n"
        f"Name=nic File=/dev/tty0 UUID={GPU_0}\n"
        f"Name=nic File=/dev/tty1 UUID={GPU_0}\n",
        "Duplicate UUID",
        conf="Gres=nic:2 CPUs=2",
    )


def test_uuid_rejects_comma_on_non_gpu_gres():
    """Verify UUID comma validation is not gpu-specific.

    Same contract as above: the comma check in the gres.conf parser runs
    before any GRES-type-specific handling, so it applies uniformly.
    """

    _assert_slurmd_fatal_on_gres(
        "AutoDetect=off\n" f"Name=nic File=/dev/tty0 UUID={GPU_0},{GPU_1}\n",
        "may not contain a comma",
        conf="Gres=nic:1 CPUs=2",
    )


def test_uuid_on_shared_gres_with_count():
    """Verify a shared GRES may carry a UUID with Count greater than one.

    gres.conf(5) makes the shared GRES an explicit exception to the
    single-device rule: it "may still use a Count greater than one, since that
    count describes shared TRES units carved out of the single device".
    """

    result, haystack = _run_throwaway_slurmd_gres(
        "AutoDetect=off\n"
        f"Name=gpu File=/dev/tty0 UUID={GPU_0}\n"
        f"Name=shard Count=4 File=/dev/tty0 UUID={GPU_0}\n",
        conf="Gres=gpu:1,shard:4 CPUs=2",
        xfail=False,
    )
    assert result["exit_code"] == 0, (
        f"a shared GRES with Count>1 and a UUID should be accepted, "
        f"rc={result['exit_code']}, output={haystack!r}"
    )
    names = _uuid_records(haystack, GPU_0)
    assert "shard" in names, (
        f"the shard record should carry UUID {GPU_0}, "
        f"got records {names}, output={haystack!r}"
    )


def test_uuid_on_shared_gres_without_sharing_uuid():
    """Verify a UUID set only on the shared GRES record is kept.

    gres.conf(5) names the shared GRES as a consumer of the value, so a UUID
    configured on the shard line is a value the admin expects to take effect.
    """

    result, haystack = _run_throwaway_slurmd_gres(
        "AutoDetect=off\n"
        "Name=gpu File=/dev/tty0\n"
        f"Name=shard Count=4 File=/dev/tty0 UUID={GPU_0}\n",
        conf="Gres=gpu:1,shard:4 CPUs=2",
        xfail=False,
    )
    assert result["exit_code"] == 0, (
        f"a UUID on the shared GRES alone should be accepted, "
        f"rc={result['exit_code']}, output={haystack!r}"
    )
    names = _uuid_records(haystack, GPU_0)
    assert "shard" in names, (
        f"the shard record should keep the UUID it configured, "
        f"got records {names}, output={haystack!r}"
    )


def test_uuid_conflict_between_shared_and_sharing_gres():
    """Verify a shared GRES UUID that disagrees with its GPU is rejected.

    gres.conf(5) says a shared GRES inherits the UUID of the device it shares
    and that a UUID configured on its own line must match, so a disagreeing
    value stops slurmd rather than being silently discarded.
    """

    _assert_slurmd_fatal_on_gres(
        "AutoDetect=off\n"
        f"Name=gpu File=/dev/tty0 UUID={GPU_0}\n"
        f"Name=shard Count=4 File=/dev/tty0 UUID={GPU_1}\n",
        "does not match UUID=",
        conf="Gres=gpu:1,shard:4 CPUs=2",
    )


def test_uuid_rejects_count_on_unshared_gres():
    """Verify Count greater than one is rejected on a non-shared GRES.

    gres.conf(5) states the shared-GRES Count exception without stating the
    rule it excepts, so pin the rule the exception implies.
    """

    _assert_slurmd_fatal_on_gres(
        "AutoDetect=off\n" f"Name=gpu File=/dev/tty0 Count=2 UUID={GPU_0}\n",
        "count does not match File value",
    )


def test_uuid_duplicate_ok_across_nodes():
    """Verify the same UUID may be used on a device of two different nodes.

    gres.conf(5) scopes uniqueness to "all devices of the same GRES name on a
    node", so a vendor serial that repeats across nodes is legal. Records for
    other nodes are filtered out before the duplicate check runs.
    """

    result, haystack = _run_throwaway_slurmd_gres(
        "AutoDetect=off\n"
        f"NodeName=UUIDfatal Name=gpu File=/dev/tty0 UUID={GPU_0}\n"
        f"NodeName=UUIDother Name=gpu File=/dev/tty0 UUID={GPU_0}\n",
        conf="Gres=gpu:1 CPUs=2",
        xfail=False,
    )
    assert result["exit_code"] == 0, (
        f"the same UUID on two different nodes should be accepted, "
        f"rc={result['exit_code']}, output={haystack!r}"
    )
    assert _uuid_records(haystack, GPU_0) == {
        "gpu"
    }, f"this node's gpu should keep UUID {GPU_0}, output={haystack!r}"


@pytest.mark.parametrize(
    "uuid",
    [
        "GPU-0123456789abcdef",
        "8680a156-0800-0000-0300-000000000000",
        "0123456789abcdef",
    ],
    ids=["rsmi", "oneapi", "nrt"],
)
def test_uuid_accepts_any_autodetect_format(uuid):
    """Verify UUID imposes no format of its own.

    gres.conf(5) says "the exact format varies by device and AutoDetect
    mechanism", and the mechanisms disagree: rsmi zero-pads behind GPU-,
    oneapi emits 8-4-4-4-12 with no prefix, nrt emits a raw serial. A
    configured value has to match whatever AutoDetect reports, so the option
    must accept all of them verbatim.
    """

    result, haystack = _run_throwaway_slurmd_gres(
        "AutoDetect=off\n" f"Name=gpu File=/dev/tty0 UUID={uuid}\n",
        conf="Gres=gpu:1 CPUs=2",
        xfail=False,
    )
    assert result["exit_code"] == 0, (
        f"UUID {uuid!r} should be accepted, "
        f"rc={result['exit_code']}, output={haystack!r}"
    )
    assert "gpu" in _uuid_records(
        haystack, uuid
    ), f"UUID {uuid!r} should be retained verbatim, output={haystack!r}"
