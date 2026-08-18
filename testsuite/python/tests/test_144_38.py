############################################################################
# Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
############################################################################
"""A GRES device keeps the controller's device number past an unusable entry.

An explicit gres.conf with no AutoDetect lists three gpu File records in
order: /dev/tty0, a regular file, and /dev/tty1, on a node running
TaskPlugin=task/cgroup with ConstrainDevices=yes. The controller counts
every configured record, so /dev/tty1 owns device number 2 and slurmd
must number it 2 as well (gres.c gres_node_config_load) even though a
regular file fails the character or block device check (gres.c
_set_gres_device_desc) and initializes no device. An older slurmd
numbered only the devices it could initialize, so everything behind the
unusable entry sat one number low and the job holding the unusable
record was handed the device behind it. The step environment, the device
cgroup, the prolog, epilog, and batch environment, and the share an mps
job gets of its sharing devices all resolve a device through that
number, and a job allocated the unusable record alone gets no device at
all. The same load must also refuse a gres.conf listing that File name
twice, as it always refused a duplicated usable one. The devices are
plain tty character devices, so no GPU hardware is required.
"""

import os
import re
import stat

import pytest

import atf

# The two usable records, in gres.conf order. The unusable regular file
# sits between them, so the second tty is the device that proves the
# numbering.
CONFIGURED_TTYS = ["/dev/tty0", "/dev/tty1"]

# The vendor GPU variables a gpu record without Flags sets beside the Slurm
# ones (gres.c, the GRES_CONF_ENV_SET default).
VENDOR_VARIABLES = (
    "CUDA_VISIBLE_DEVICES",
    "ROCR_VISIBLE_DEVICES",
    "GPU_DEVICE_ORDINAL",
    "ZE_AFFINITY_MASK",
)

# The mps count of the node, which the gres/mps plugin spreads over the
# three configured gpu records, the share an mps job on one device asks
# for, and the share that makes an mps job span two devices. A share is
# reported as the percentage of the count on the job's device, and the
# count is chosen so that percentage differs from the one computed from
# the count of 100 the plugin falls back to when it finds no count for a
# device (gres_mps.c _get_dev_count). The all_sharing flag lets an mps job
# use every sharing device of the node, which the select parameter below
# lets it span.
MPS_COUNT = 600
MPS_SHARE = 50
MPS_SPANNING_SHARE = 300
MPS_RECORD = f"Name=mps Count={MPS_COUNT} Flags=all_sharing"

# The regular file configured as the middle gpu File record, the module's
# healthy four record gres.conf content, and the directory where the
# Prolog and Epilog leave the GRES environment of every job. Assigned in
# setup, the file and the directory in the module fixture working
# directory.
_unusable_file = None
_gres_conf = None
_prep_env_dir = None


@pytest.fixture(scope="module", autouse=True)
def setup():
    global _unusable_file, _gres_conf, _prep_env_dir

    atf.require_version(
        (26, 11),
        "sbin/slurmd",
        reason=(
            "Issue 50982: slurmd numbers GRES devices as slurmctld counts them "
            "since 26.11"
        ),
    )
    atf.require_config_parameter("SelectType", "select/cons_tres")
    # Strict on purpose: the CR_* allocation units are mutually exclusive
    # (read_config.c _parse_select_type_param rejects more than one), so an
    # additive include on a variant value would leave slurm.conf unloadable.
    # MULTIPLE_SHARING_GRES_PJ lets one mps job take shares on more than
    # one sharing device, which the spanning test needs.
    atf.require_config_parameter(
        "SelectTypeParameters", "CR_CPU,MULTIPLE_SHARING_GRES_PJ"
    )
    atf.require_config_parameter_includes("GresTypes", "gpu")
    atf.require_config_parameter_includes("GresTypes", "mps")
    atf.require_config_parameter_includes("TaskPlugin", "task/cgroup")
    atf.require_config_parameter("ConstrainDevices", "yes", source="cgroup")
    atf.require_tty(0)
    atf.require_tty(1)

    # A regular file stats fine yet is neither a character nor a block
    # device, so slurmd counts its record and initializes no device for
    # it. Written in the module fixture working directory, which
    # conftest.py update_tmp_path_exec_permissions already opened up, and
    # named in gres.conf by its absolute path so the root-run slurmd can
    # stat it there. Its name ends in a digit no tty uses: the gres/mps
    # plugin keys a device's count by that trailing number, and -1, the
    # key a name without one gets, is also its "no device" value, which
    # would hand a job that resolved no device the unusable record's
    # count instead of the fallback.
    _unusable_file = os.path.abspath("unusable_gpu_file9")
    atf.run_command(
        f"cat > {_unusable_file}", input="not a device\n", fatal=True, quiet=True
    )
    _gres_conf = (
        f"Name=gpu File={CONFIGURED_TTYS[0]}\n"
        f"Name=gpu File={_unusable_file}\n"
        f"Name=gpu File={CONFIGURED_TTYS[1]}\n"
        f"{MPS_RECORD}\n"
    )
    atf.require_config_file("gres.conf", _gres_conf)

    # The Prolog and the Epilog record the GRES variables slurmd hands them,
    # one file per job and hook, in the same opened up directory. They exit
    # 0 whatever they find, so a missing variable fails a test and never
    # drains the node.
    _prep_env_dir = os.getcwd()
    recorded = "|".join(
        ("SLURM_JOB_GPUS", "CUDA_MPS_ACTIVE_THREAD_PERCENTAGE") + VENDOR_VARIABLES
    )
    for hook in ("Prolog", "Epilog"):
        script = os.path.join(_prep_env_dir, f"{hook.lower()}.sh")
        out = f"{_prep_env_dir}/{hook.lower()}.$SLURM_JOB_ID"
        # Written aside and renamed, so the file exists only once complete
        atf.make_bash_script(
            script,
            f"env | grep -E '^({recorded})=' > {out}.tmp\n"
            f"mv {out}.tmp {out}\n"
            "exit 0\n",
        )
        atf.require_config_parameter(hook, script)

    atf.require_nodes(1, [("Gres", f"gpu:3,mps:{MPS_COUNT}"), ("CPUs", 2)])
    atf.require_slurm_running()


def _install_gres_conf(content):
    """Write the module's gres.conf as the slurm user that owns it.

    slurmd reads the file the config dir holds, so a test driving a load
    against a different content installs it here and puts the healthy one
    back the same way.
    """
    atf.run_command(
        f"cat > {atf.properties['slurm-config-dir']}/gres.conf",
        input=content,
        user=atf.properties["slurm-user"],
        fatal=True,
        quiet=True,
    )


def _read_prep_env(hook, job_id):
    """The variables the Prolog or Epilog recorded for a job, as a dict.

    The hook writes its file as the job starts or ends on the node, so the
    file is waited for rather than expected at once.
    """
    path = os.path.join(_prep_env_dir, f"{hook}.{job_id}")
    assert atf.wait_for_file(
        path
    ), f"the {hook} must leave its GRES environment for job {job_id} in {path}"
    recorded = atf.run_command_output(f"cat {path}", fatal=True, quiet=True)
    return dict(line.split("=", 1) for line in recorded.splitlines() if "=" in line)


def _hold_lowest_devices(gres_node, count):
    """Run a job on the lowest device numbers for the rest of the test.

    The controller picks free devices in ascending order
    (gres_select_filter.c _pick_gres_topo), so a job asking count GPUs
    takes numbers 0 to count-1 and leaves the next number to the test's own
    jobs: number 2, the record behind the unusable entry, behind a two-GPU
    job, and number 1, the unusable record itself, behind a one-GPU job.
    The allocation is checked rather than assumed, as a precondition, and
    the job goes away with the test's other jobs when the test ends.
    """
    job_id = atf.submit_job_sbatch(
        f"-w {gres_node} -N1 -n1 -t5 --gres=gpu:{count} -J hold_low_devices "
        '--wrap "sleep infinity"',
        fatal=True,
    )
    atf.wait_for_job_state(job_id, "RUNNING", fatal=True)
    expected = "gpu:1(IDX:0)" if count == 1 else f"gpu:{count}(IDX:0-{count - 1})"
    job_gres = atf.get_job_parameter(job_id, "GRES", fatal=True, quiet=True)
    if not job_gres or expected not in job_gres:
        pytest.fail(
            f"the holding job must be allocated device numbers 0 to {count - 1} "
            f"for the test to mean anything, got GRES={job_gres!r}"
        )
    return job_id


def _probe_script(name):
    """Write the step script that reports its GRES numbers and tty access.

    The step reports the device numbers the controller allocated its job
    while the job still exists, its Slurm and vendor variables, then opens
    each configured tty in a subshell, so a refusal is a nonzero status
    rather than a failed redirection in the probe itself, and exits 0
    whatever the outcome so the step's own status stays clean.
    """
    scontrol = f"{atf.properties['slurm-bin-dir']}/scontrol"
    atf.make_bash_script(
        name,
        f'echo "JOB_IDX=$({scontrol} show job -dd $SLURM_JOB_ID | '
        "grep -oE 'gpu:1\\(IDX:[0-9,-]+\\)' | head -1)\"\n"
        'echo "STEP_GPUS=$SLURM_STEP_GPUS"\n'
        'echo "GPUS_ON_NODE=$SLURM_GPUS_ON_NODE"\n'
        + "".join(f'echo "{var}=${var}"\n' for var in VENDOR_VARIABLES)
        + f"for f in {' '.join(CONFIGURED_TTYS)}\n"
        "do\n"
        '    if (exec 3< "$f") 2>/dev/null\n'
        "    then\n"
        '        echo "OPEN $f"\n'
        "    else\n"
        '        echo "DENIED $f"\n'
        "    fi\n"
        "done\n"
        "exit 0\n",
    )
    return name


def _parse_probe(result):
    """The probe's variables as a dict and its tty verdicts as a dict."""
    report = dict(re.findall(r"^([A-Z_]+)=(\S*)$", result["stdout"], re.MULTILINE))
    probed = {
        path: state
        for state, path in re.findall(
            r"^(OPEN|DENIED) (\S+)$", result["stdout"], re.MULTILINE
        )
    }
    assert set(probed) == set(CONFIGURED_TTYS), (
        "the step must probe both configured ttys, its report reads "
        f"{result['stdout']!r}"
    )
    return report, probed


@pytest.fixture()
def healthy_gres_conf():
    """Leave the module's healthy gres.conf installed however a test ends."""
    yield
    _install_gres_conf(_gres_conf)


@pytest.fixture()
def readable_ttys():
    """Grant both ttys world read for a test and put their modes back.

    A file mode refuses an open just as flatly as the device cgroup, so the
    cgroup is left as the only refuser and openability rather than the
    listing is the answer the probes read.
    """
    starting_modes = {
        path: stat.S_IMODE(os.stat(path).st_mode) for path in CONFIGURED_TTYS
    }
    for path in starting_modes:
        atf.run_command(f"chmod o+r {path}", user="root", quiet=True, fatal=True)
    yield
    for path, mode in starting_modes.items():
        atf.run_command(f"chmod {mode:04o} {path}", user="root", quiet=True)
    restored = {path: stat.S_IMODE(os.stat(path).st_mode) for path in starting_modes}
    assert restored == starting_modes, (
        "device node modes were not restored, expected "
        f"{starting_modes!r} and read {restored!r}"
    )


@pytest.fixture(scope="module")
def gres_node():
    """The node hosting the module's gpu gres, read from a step on it."""
    output = atf.run_job_output(
        "-N1 -n1 -t1 --gres=gpu:1 --mpi=none -J gpu_node_probe "
        "printenv SLURMD_NODENAME",
        fatal=True,
    )
    node = output.strip()
    assert node, "a step holding the gpu gres must report the node it ran on"
    return node


def test_step_env_numbers_devices_as_the_controller_counts(gres_node):
    """SLURM_STEP_GPUS reads 0,2 for a step holding all three records.

    slurmstepd builds SLURM_STEP_GPUS from each allocated device's
    shared number (gres_common.c common_gres_set_env), so the record
    behind the unusable middle entry keeps the controller's number 2
    while SLURM_GPUS_ON_NODE still counts all three allocated records. A
    slurmd numbering only the usable devices read 0,1 here. The vendor
    GPU variables renumber the two devices the task can see from zero
    under ConstrainDevices=yes (gres_common.c common_gres_set_env with
    gres_use_local_device_index()), so each of them reads 0,1 and the
    Slurm variable is the one that carries the shared number.
    """
    result = atf.run_job(
        f"-w {gres_node} -N1 -n1 -t1 --gres=gpu:3 --mpi=none "
        "-J step_env_numbers bash -c "
        "'echo STEP_GPUS=$SLURM_STEP_GPUS && "
        "echo GPUS_ON_NODE=$SLURM_GPUS_ON_NODE && "
        + " && ".join(f"echo {var}=${var}" for var in VENDOR_VARIABLES)
        + "'",
        fatal=True,
    )
    report = dict(re.findall(r"^([A-Z_]+)=(\S*)$", result["stdout"], re.MULTILINE))
    assert "STEP_GPUS" in report, (
        "the step must report its SLURM_STEP_GPUS, its report reads "
        f"{result['stdout']!r}"
    )
    assert report["STEP_GPUS"] == "0,2", (
        "a step holding all three configured records must read the "
        "controller numbers 0,2 in SLURM_STEP_GPUS, with the device "
        "behind the unusable entry keeping number 2, got "
        f"{report['STEP_GPUS']!r}"
    )
    assert report.get("GPUS_ON_NODE") == "3", (
        "the step must count all three allocated records in "
        f"SLURM_GPUS_ON_NODE, its report reads {result['stdout']!r}"
    )
    for name in VENDOR_VARIABLES:
        assert report.get(name) == "0,1", (
            f"{name} must renumber the step's two devices to 0,1 under "
            f"ConstrainDevices=yes, its report reads {result['stdout']!r}"
        )


def test_device_cgroup_admits_the_controller_numbered_device(gres_node, readable_ttys):
    """A step holding only device number 2 opens /dev/tty1 and no other.

    With the two lowest device numbers held by another job the following
    one gpu step gets number 2, the record behind the unusable entry.
    task/cgroup admits it by the major and minor slurmd resolved from the
    File path it numbered and denies the other configured ones
    (task_cgroup_devices.c _handle_device_access). Under the one-low
    numbering that number matched no device, the step environment came up
    empty, and the granted device stayed denied. The vendor GPU variables
    renumber the devices the task can see from zero under
    ConstrainDevices=yes (gres_common.c common_gres_set_env with
    gres_use_local_device_index()), so each of them reads 0 for the single
    device the step holds.
    """
    _hold_lowest_devices(gres_node, 2)
    result = atf.run_job(
        f"-w {gres_node} -N1 -n1 -t1 --gres=gpu:1 --mpi=none "
        f"-J probe_device_cgroup {_probe_script('./probe_number_2.sh')}",
        fatal=True,
    )
    report, probed = _parse_probe(result)
    assert report.get("JOB_IDX") == "gpu:1(IDX:2)", (
        "with the two lowest device numbers held by another job, the one "
        "gpu job must be allocated the controller number 2, its report "
        f"reads {result['stdout']!r}"
    )
    assert report.get("STEP_GPUS") == "2", (
        "the step must read its allocated controller number 2 in "
        f"SLURM_STEP_GPUS, its report reads {result['stdout']!r}"
    )
    for name in VENDOR_VARIABLES:
        assert report.get(name) == "0", (
            f"{name} must renumber the step's single device to 0 under "
            f"ConstrainDevices=yes, its report reads {result['stdout']!r}"
        )
    assert probed[CONFIGURED_TTYS[1]] == "OPEN", (
        "the step holding device number 2 must be able to open its own "
        f"{CONFIGURED_TTYS[1]}, the probe reads {probed!r}"
    )
    assert probed[CONFIGURED_TTYS[0]] == "DENIED", (
        "the step holding only device number 2 must be refused the "
        f"unallocated {CONFIGURED_TTYS[0]}, the probe reads {probed!r}"
    )


def test_step_allocated_the_unusable_record_alone_gets_no_device(
    gres_node, readable_ttys
):
    """A step holding only device number 1, the unusable record, gets nothing.

    With number 0 held by another job the following one gpu step gets
    number 1, the record whose File initialized no device. The controller
    counts it as one GPU, so SLURM_GPUS_ON_NODE reads 1, but no device
    answers to its number: the Slurm and vendor variables come up empty
    and the device cgroup admits neither tty. Under the one-low numbering
    that number named /dev/tty1, the device allocated to number 2, and the
    step was handed it.
    """
    _hold_lowest_devices(gres_node, 1)
    result = atf.run_job(
        f"-w {gres_node} -N1 -n1 -t1 --gres=gpu:1 --mpi=none "
        f"-J probe_unusable_record {_probe_script('./probe_number_1.sh')}",
        fatal=True,
    )
    report, probed = _parse_probe(result)
    assert report.get("JOB_IDX") == "gpu:1(IDX:1)", (
        "with device number 0 held by another job, the one gpu job must be "
        f"allocated the controller number 1, its report reads {result['stdout']!r}"
    )
    assert report.get("GPUS_ON_NODE") == "1", (
        "the step must still count its one allocated record in "
        f"SLURM_GPUS_ON_NODE, its report reads {result['stdout']!r}"
    )
    for name in ("STEP_GPUS",) + VENDOR_VARIABLES:
        assert report.get(name) == "", (
            f"{name} must be empty for the step allocated the unusable record "
            f"alone, its report reads {result['stdout']!r}"
        )
    for path in CONFIGURED_TTYS:
        assert probed[path] == "DENIED", (
            "the step allocated the unusable record alone must be refused "
            f"{path}, the probe reads {probed!r}"
        )


def test_prolog_epilog_and_batch_env_number_devices_as_the_controller_counts(
    gres_node,
):
    """The prolog, epilog, and batch environment carry the controller's number.

    slurmd builds the prolog and epilog GRES environment from the job's
    allocation bitmap, looking each allocated bit up by device number
    (gres_common.c gres_common_prep_set_env), and the batch step gets
    SLURM_JOB_GPUS the same way (gres_common.c common_gres_set_env). With
    the two lowest numbers held, a one gpu batch job gets number 2 and all
    three must report it, where the one-low numbering found no device for
    that bit and left every one of them empty. The holding job, allocated
    numbers 0 and 1, owns the single device 0, where the one-low numbering
    handed it /dev/tty1 as its second one. The prolog and epilog carry the
    shared number in the vendor variables too, while the batch step
    renumbers its single device to 0 under ConstrainDevices=yes. The
    gres/mps plugin resolves the sharing device of an mps job through its
    own copy of the lookup before it computes the job's share of that
    device (gres_mps.c gres_p_prep_set_env), so an mps job landing behind
    the held devices must get its share in the prolog and the epilog, where
    the one-low numbering walked the device list by position, found nothing
    for bit 2, and set no share.
    """
    hold_job = _hold_lowest_devices(gres_node, 2)
    recorded = _read_prep_env("prolog", hold_job)
    for name in ("SLURM_JOB_GPUS",) + VENDOR_VARIABLES:
        assert recorded.get(name) == "0", (
            "the prolog of the job holding numbers 0 and 1 must name only "
            f"device 0 in {name}, the unusable record owning none, it "
            f"recorded {recorded!r}"
        )

    gpu_job = atf.submit_job_sbatch(
        f"-w {gres_node} -N1 -n1 -t1 --gres=gpu:1 -J batch_number_2 "
        "--output batch_gpu.out --wrap 'echo JOB_GPUS=$SLURM_JOB_GPUS'",
        fatal=True,
    )
    atf.wait_for_job_state(gpu_job, "DONE", fatal=True)
    atf.assert_file_contents(
        "batch_gpu.out",
        "JOB_GPUS=2\n",
        contains=True,
        strip=False,
        message=(
            "the batch step allocated the controller number 2 must read it in "
            "SLURM_JOB_GPUS"
        ),
    )
    for hook in ("prolog", "epilog"):
        recorded = _read_prep_env(hook, gpu_job)
        for name in ("SLURM_JOB_GPUS",) + VENDOR_VARIABLES:
            assert recorded.get(name) == "2", (
                f"the {hook} of the job allocated the controller number 2 must "
                f"read it in {name}, it recorded {recorded!r}"
            )

    expected_share = str(MPS_SHARE * 100 // (MPS_COUNT // 3))
    mps_job = atf.submit_job_sbatch(
        f"-w {gres_node} -N1 -n1 -t1 --gres=mps:{MPS_SHARE} -J mps_behind_unusable "
        "--output batch_mps.out --wrap 'echo JOB_GPUS=$SLURM_JOB_GPUS "
        "PCT=$CUDA_MPS_ACTIVE_THREAD_PERCENTAGE CVD=$CUDA_VISIBLE_DEVICES'",
        fatal=True,
    )
    atf.wait_for_job_state(mps_job, "DONE", fatal=True)
    atf.assert_file_contents(
        "batch_mps.out",
        f"JOB_GPUS=2 PCT={expected_share} CVD=0\n",
        contains=True,
        strip=False,
        message=(
            "the batch step of the mps job sharing the device behind the "
            f"unusable entry must read number 2, its {expected_share} percent "
            "share, and the device renumbered to 0"
        ),
    )
    for hook in ("prolog", "epilog"):
        recorded = _read_prep_env(hook, mps_job)
        assert recorded.get("SLURM_JOB_GPUS") == "2", (
            f"the {hook} of the mps job sharing the device behind the unusable "
            f"entry must read number 2 in SLURM_JOB_GPUS, it recorded {recorded!r}"
        )
        assert recorded.get("CUDA_VISIBLE_DEVICES") == "2", (
            f"the {hook} of the mps job must name its sharing device 2 in "
            f"CUDA_VISIBLE_DEVICES, it recorded {recorded!r}"
        )
        assert recorded.get("CUDA_MPS_ACTIVE_THREAD_PERCENTAGE") == expected_share, (
            f"the {hook} of the mps job must read its {MPS_SHARE} of the "
            f"{MPS_COUNT // 3} on its device as {expected_share} percent in "
            f"CUDA_MPS_ACTIVE_THREAD_PERCENTAGE, it recorded {recorded!r}"
        )


def test_mps_job_spanning_the_unusable_record_gets_its_share(gres_node):
    """An mps job spanning the unusable record and /dev/tty1 gets its share.

    With device 0 held by a gpu job, an mps job asking more than one
    device's count spans the two remaining records, numbers 1 and 2, the
    lower of which initialized no device. The gres/mps plugin computes the
    job's share from the first device in list order whose number the job
    holds (gres_mps.c gres_p_prep_set_env, as gres_common.c
    common_gres_set_env picks it), /dev/tty1 here, so the prolog and the
    epilog carry the share and name device 2. A lookup through the job's
    lowest number alone found no device for number 1 and set no share. The
    share of a spanning job is the plugin's own arithmetic over the count
    of that one device, so the test pins the computed form.
    """
    _hold_lowest_devices(gres_node, 1)
    expected_share = str(MPS_SPANNING_SHARE * 100 // (MPS_COUNT // 3))
    mps_job = atf.submit_job_sbatch(
        f"-w {gres_node} -N1 -n1 -t1 --gres=mps:{MPS_SPANNING_SHARE} "
        "-J mps_spanning_unusable --output batch_mps_spanning.out "
        "--wrap 'echo JOB_GPUS=$SLURM_JOB_GPUS GPUS_ON_NODE=$SLURM_GPUS_ON_NODE'",
        fatal=True,
    )
    atf.wait_for_job_state(mps_job, "DONE", fatal=True)
    atf.assert_file_contents(
        "batch_mps_spanning.out",
        "JOB_GPUS=2 GPUS_ON_NODE=2\n",
        contains=True,
        strip=False,
        message=(
            "the batch step of the spanning mps job must name device 2 alone "
            "in SLURM_JOB_GPUS while SLURM_GPUS_ON_NODE counts both records"
        ),
    )
    for hook in ("prolog", "epilog"):
        recorded = _read_prep_env(hook, mps_job)
        assert recorded.get("SLURM_JOB_GPUS") == "2", (
            f"the {hook} of the spanning mps job must name device 2 alone in "
            f"SLURM_JOB_GPUS, it recorded {recorded!r}"
        )
        assert recorded.get("CUDA_VISIBLE_DEVICES") == "2", (
            f"the {hook} of the spanning mps job must name device 2 in "
            f"CUDA_VISIBLE_DEVICES, it recorded {recorded!r}"
        )
        assert recorded.get("CUDA_MPS_ACTIVE_THREAD_PERCENTAGE") == expected_share, (
            f"the {hook} of the spanning mps job must read its share over the "
            f"{MPS_COUNT // 3} on device 2 as {expected_share} percent in "
            f"CUDA_MPS_ACTIVE_THREAD_PERCENTAGE, it recorded {recorded!r}"
        )


def test_duplicate_unusable_file_refuses_config_load(gres_node, healthy_gres_conf):
    """slurmd refuses its gres config when the unusable File repeats.

    The gres config load records every configured File name whether or
    not its device initialized and fails on a repeated one (gres.c
    gres_node_config_load), which the gpu plugin turns into a fatal
    error (gres_gpu.c gres_p_node_config_load). slurmd -G runs that same
    load (slurmd.c _load_gres) in a foreground process, so the refusal
    is driven without stopping the node's running slurmd. The repeat
    replaces the third record to keep the count at the configured three,
    since the load trims records beyond the slurm.conf count before the
    duplicate check walks the list. An older slurmd dropped an unusable
    entry before that check saw its name and accepted the repeat.
    """
    slurmd_load = f"{atf.properties['slurm-sbin-dir']}/slurmd -N {gres_node} -G"

    healthy = atf.run_command(slurmd_load, user="root", quiet=True)
    assert healthy["exit_code"] == 0, (
        "the gres config load must accept the healthy gres.conf with one "
        "unusable File entry, it reads "
        f"{healthy['stdout'] + healthy['stderr']!r}"
    )

    broken_conf = (
        f"Name=gpu File={CONFIGURED_TTYS[0]}\n"
        f"Name=gpu File={_unusable_file}\n"
        f"Name=gpu File={_unusable_file}\n"
        f"{MPS_RECORD}\n"
    )
    _install_gres_conf(broken_conf)
    refused = atf.run_command(slurmd_load, user="root", quiet=True)
    assert refused["exit_code"] != 0, (
        "the gres config load must refuse a gres.conf listing the "
        "unusable File name twice, it exited 0 with "
        f"{refused['stdout'] + refused['stderr']!r}"
    )
    assert (
        f"duplicate device file name ({_unusable_file})"
        in refused["stdout"] + refused["stderr"]
    ), (
        "the refusal must name the duplicated device file, it reads "
        f"{refused['stdout'] + refused['stderr']!r}"
    )

    # The healthy file goes back before the clean reload is asserted, and
    # the healthy_gres_conf fixture covers the endings that never get here.
    _install_gres_conf(_gres_conf)
    restored = atf.run_command(slurmd_load, user="root", quiet=True)
    assert restored["exit_code"] == 0, (
        "the gres config load must accept the restored healthy gres.conf, "
        f"it reads {restored['stdout'] + restored['stderr']!r}"
    )
