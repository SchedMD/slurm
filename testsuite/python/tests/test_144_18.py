############################################################################
# Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
############################################################################
"""Test shard device memory enforcement (ConstrainDeviceMemory)."""

import glob
import os
import re

import pytest

import atf

SHARDS_PER_GPU = 8
MIB = 1024 * 1024

node = "node1"


@pytest.fixture(scope="module", autouse=True)
def setup():
    global node

    atf.require_version(
        (26, 11),
        component="sbin/slurmd",
        reason="Issue 51032: ConstrainDeviceMemory was added in 26.11",
    )

    gpus = local_gpu_device_files()
    if not gpus:
        pytest.skip("no GPU device files on this node")

    atf.require_config_parameter("SelectType", "select/cons_tres")
    atf.require_config_parameter_includes("SelectTypeParameters", "CR_Core_Memory")
    atf.require_config_parameter_includes("TaskPlugin", "cgroup")
    atf.require_config_parameter_includes("GresTypes", "gpu")
    atf.require_config_parameter_includes("GresTypes", "shard")
    atf.require_config_parameter("CgroupPlugin", "cgroup/v2", source="cgroup")
    atf.require_config_parameter("ConstrainDevices", "yes", source="cgroup")
    atf.require_config_parameter("ConstrainDeviceMemory", "yes", source="cgroup")
    atf.require_config_parameter("EnableControllers", "yes", source="cgroup")
    atf.require_config_parameter("JobAcctGatherType", "jobacct_gather/cgroup")
    atf.require_config_parameter_includes("AccountingStorageTRES", "gres/gpumem")
    atf.require_accounting()

    if atf.properties["auto-config"]:
        atf.require_config_parameter(
            "Name",
            {
                "gpu": {"File": ",".join(gpus)},
                "shard": {"Count": len(gpus) * SHARDS_PER_GPU},
            },
            source="gres",
        )
        atf.require_nodes(
            1, [("Gres", f"gpu:{len(gpus)},shard:{len(gpus) * SHARDS_PER_GPU}")]
        )

    atf.require_slurm_running()

    if not atf.properties["auto-config"]:
        node = local_shard_node(gpus)


def pci_bdf_of_device(dev_file):
    """PCI address of a device file, or None if it has none.

    Resolved through the sysfs character device link, the same way the shard
    plugin does it when AutoDetect stored no address for the device.
    """
    try:
        rdev = os.stat(dev_file).st_rdev
    except OSError:
        return None

    link = f"/sys/dev/char/{os.major(rdev)}:{os.minor(rdev)}/device"
    if not os.path.exists(link):
        return None

    return os.path.basename(os.path.realpath(link))


def local_gpu_device_files():
    """Physical GPU device files of this node, dmem-capable ones preferred.

    A GPU with both a native and a DRM render node must only be configured
    once, or its devices would share one dmem region and be unenforceable, so
    at most one device file is returned per PCI address.

    Devices whose PCI address owns a dmem region are returned when there are
    any, so the test exercises enforcement on whichever driver registers
    regions instead of assuming a vendor. Only when no device has a region
    does this fall back to reporting every GPU, which keeps the unenforced
    paths covered on a node without dmem support.
    """
    regions = " ".join(local_dmem_regions())
    enforced = []
    seen = set()

    for dev_file in sorted(glob.glob("/dev/nvidia[0-9]*")) + sorted(
        glob.glob("/dev/dri/renderD*")
    ):
        bdf = pci_bdf_of_device(dev_file)
        if not bdf or (bdf in seen):
            continue
        seen.add(bdf)
        if bdf in regions:
            enforced.append(dev_file)

    if enforced:
        return enforced

    nvidia = sorted(glob.glob("/dev/nvidia[0-9]*"))
    if nvidia:
        return nvidia
    return sorted(glob.glob("/dev/dri/renderD*"))


def slurmd_log_file(node_name):
    """Path of the slurmd log of one node.

    SlurmdLogFile holds the real location, and may carry the %n placeholder
    that gives every node of a multiple slurmd host its own log. Auto-config
    leaves SlurmdLogFile unset, so the log directory of the controller is the
    fallback, the same way atf derives it.
    """
    log_file = atf.get_config_parameter(
        "SlurmdLogFile", default=None, live=False, quiet=True
    )
    if log_file:
        return log_file.replace("%n", node_name)

    if "slurm-logs-dir" not in atf.properties:
        atf.properties["slurm-logs-dir"] = os.path.dirname(
            atf.get_config_parameter("SlurmctldLogFile", live=False, quiet=True)
        )

    return f"{atf.properties['slurm-logs-dir']}/slurmd.{node_name}.log"


def local_shard_node(gpus):
    """Name of the node that shards the given GPUs.

    In local-config mode the gres configuration belongs to the administrator,
    so the node is discovered rather than created. A host may serve several
    nodes, under multiple slurmd or with other GPUs configured, so the only
    reliable test is whether a node reports these very devices in its own
    slurmd log. Node fields come straight from "scontrol show nodes --json",
    so they are matched without regard to case.
    """
    considered = {}

    for name, info in atf.get_nodes(quiet=True).items():
        fields = {key.casefold(): value for key, value in info.items()}
        if "shard" not in str(fields.get("gres") or ""):
            continue

        log_file = slurmd_log_file(name)
        if atf.run_command_exit(f"test -f {log_file}", user="root", quiet=True) != 0:
            continue

        devices = parse_dmem_log_devs(name)
        if all(gpu in devices for gpu in gpus):
            return name
        considered[name] = sorted(devices)

    pytest.skip(
        f"no node shards {gpus}. Sharding nodes of this host report "
        f"{considered or 'no devices'}. A slurmd that has not restarted since "
        f"gres.conf changed still logs its old devices."
    )


def local_dmem_regions():
    """Return {region_name: capacity_bytes} of this node, empty if none.

    The read is a probe: the file legitimately does not exist on kernels
    without the dmem controller, so it must not be fatal.
    """
    regions = {}
    output = atf.run_command_output("cat /sys/fs/cgroup/dmem.capacity", quiet=True)
    for line in output.splitlines():
        parts = line.rsplit(" ", 1)
        if (len(parts) == 2) and parts[1].isdigit():
            regions[parts[0]] = int(parts[1])
    return regions


def slurmd_log(node_name=None):
    """Read the slurmd log of one node, the test node by default."""
    return atf.run_command_output(
        f"cat {slurmd_log_file(node_name or node)}",
        user="root",
        fatal=True,
        quiet=True,
    )


def parse_dmem_log_devs(node_name=None):
    """Parse the per-device dmem state lines from the slurmd log.

    Returns {device_path: fields}, where fields is the key=value map of the
    last "shard: <path> ..." state line (region, capacity, shards, slice,
    dmem) of each device.
    """
    devs = {}
    for line in slurmd_log(node_name).splitlines():
        match = re.search(r"shard: (/\S+) (\S+=.*)$", line)
        if not match:
            continue
        fields = dict(f.split("=", 1) for f in match.group(2).split() if "=" in f)
        if "shards" in fields:
            devs[match.group(1)] = fields
    return devs


@pytest.fixture(scope="module")
def dmem_log_devs():
    """Per-device dmem state line fields as of the module setup."""
    return parse_dmem_log_devs()


def test_startup_log_contract(dmem_log_devs):
    """Verify the per-device dmem state line field contract at startup.

    Every sharding device logs one state line. "region=" appears only if a dmem
    region exists for the device, and "dmem=" appears only if a region was
    matched and the device participates (here always "enforced" since
    ConstrainDeviceMemory=yes). Devices without a usable region get a
    warning instead, never a drain.
    """
    for path in local_gpu_device_files():
        assert path in dmem_log_devs, f"No shard dmem state line for {path}"
        fields = dmem_log_devs[path]
        assert (
            int(fields["shards"]) == SHARDS_PER_GPU
        ), f"State line of {path} must report shards={SHARDS_PER_GPU}"
        if "dmem" in fields:
            assert "region" in fields, "dmem= must only appear with region="
            assert (
                int(fields["shards"]) > 0
            ), f"dmem= of {path} must only appear with shards to enforce"
            assert (
                fields["dmem"] == "enforced"
            ), "dmem= must be enforced with ConstrainDeviceMemory=yes"
        if "region" not in fields:
            assert re.search(
                rf"shard: {path} has no dmem region", slurmd_log()
            ), f"Missing no-region warning for {path}"

    assert atf.run_command_output(
        f"sinfo -h -n {node} -o %T", fatal=True
    ).strip() not in (
        "drained",
        "draining",
    ), "Node must never drain for missing dmem capability"


def test_dmem_limits_and_env(dmem_log_devs):
    """A shard job gets dmem.max = dmem.min = shards x slice on its device,
    and SLURM_SHARD_MEM_PER_GPU announces the same amount in MiB keyed by
    the PCI address of the device. A step sub-allocating fewer shards gets
    its own smaller limit at the step cgroup level."""
    enforced = {
        fields["region"]: fields
        for fields in dmem_log_devs.values()
        if fields.get("dmem") == "enforced"
    }
    if not enforced:
        pytest.skip("no dmem region matched any GPU on this node")

    regions = local_dmem_regions()
    file_out = "dmem_job.out"
    script = "dmem_job.sh"
    atf.make_bash_script(
        script,
        """
job_cg=$(sed -n 's/^0:://p' /proc/self/cgroup)
job_cg=${job_cg%%/step_*}
echo "DMEM_MAX:$(cat /sys/fs/cgroup${job_cg}/dmem.max | tr '\\n' ';')"
echo "DMEM_MIN:$(cat /sys/fs/cgroup${job_cg}/dmem.min | tr '\\n' ';')"
srun -n1 --gres=shard:1 bash -c 'step_cg=$(sed -n "s/^0:://p" /proc/self/cgroup); step_cg=${step_cg%/task_*}; echo "STEP_DMEM_MAX:$(cat /sys/fs/cgroup${step_cg}/dmem.max | tr "\\n" ";")"; echo "STEPENV:$SLURM_SHARD_MEM_PER_GPU"'
echo "SHARDENV:$SLURM_SHARD_MEM_PER_GPU"
""",
    )
    job_id = atf.submit_job_sbatch(
        f"-w {node} --gres=shard:3 -t1 -o {file_out} {script}", fatal=True
    )
    atf.wait_for_job_state(job_id, "DONE", fatal=True)
    # SHARDENV is the last line the job writes, so once it appears the
    # output is complete and a single read below is race-free
    atf.assert_file_contents(file_out, "SHARDENV:", contains=True)
    output = atf.run_command_output(f"cat {file_out}", fatal=True)

    # The 3 shards land on one device: one enforced region holds 3 slices
    expected = {region: 3 * (regions[region] // SHARDS_PER_GPU) for region in enforced}
    limited = [
        region
        for region, exp_bytes in expected.items()
        if f"{region} {exp_bytes}" in output
    ]
    assert len(limited) == 1, f"Expected one limited region, output: {output}"
    region = limited[0]
    assert re.search(
        rf"DMEM_MAX:.*{re.escape(region)} {expected[region]};", output
    ), "dmem.max must hold shards x slice"
    assert re.search(
        rf"DMEM_MIN:.*{re.escape(region)} {expected[region]};", output
    ), "dmem.min must hold shards x slice"
    assert re.search(
        rf"SHARDENV:.*\b[0-9a-f]{{4}}:[0-9a-f]{{2}}:[0-9a-f]{{2}}\.\d="
        rf"({expected[region] // MIB})M",
        output,
    ), "SLURM_SHARD_MEM_PER_GPU must be keyed by a PCI address and hold the MiB-floored amount"

    # The 1-shard step must get its own 1 x slice limit, not the job's
    slice_bytes = regions[region] // SHARDS_PER_GPU
    assert re.search(
        rf"STEP_DMEM_MAX:.*{re.escape(region)} {slice_bytes};", output
    ), "step dmem.max must hold 1 x slice for a 1-shard step"
    assert re.search(
        rf"STEPENV:.*\b[0-9a-f]{{4}}:[0-9a-f]{{2}}:[0-9a-f]{{2}}\.\d="
        rf"({slice_bytes // MIB})M",
        output,
    ), "step SLURM_SHARD_MEM_PER_GPU must be keyed by a PCI address and hold the step's own amount"

    atf.run_command(f"rm -f {file_out}", fatal=True, quiet=True)


def gpumem_allocator():
    """Compile the CUDA allocator when a toolkit is available, else None.

    The accounting pipeline is asserted regardless; the allocator only adds
    the nonzero-amount assertion on nodes that can build it.
    """
    nvccs = sorted(glob.glob("/usr/local/cuda*/bin/nvcc")) or ["nvcc"]
    atf.run_command(
        "cat > alloc_vram.cu",
        input="""
#include <cuda_runtime.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

int main(int argc, char **argv)
{
    size_t mib = atoi(argv[1]);
    void *p = NULL;

    if (cudaMalloc(&p, mib << 20) != cudaSuccess)
        return 1;
    cudaMemset(p, 1, mib << 20);
    cudaDeviceSynchronize();
    sleep(atoi(argv[2]));
    return 0;
}
""",
        fatal=True,
        quiet=True,
    )
    result = atf.run_command(
        f"{nvccs[-1]} -allow-unsupported-compiler -o alloc_vram alloc_vram.cu",
        quiet=True,
    )
    if result["exit_code"] != 0:
        return None
    return "./alloc_vram"


def test_gpumem_accounting_from_dmem(dmem_log_devs):
    """gres/gpumem is fed from the dmem kernel counter on enforced devices:
    a completed shard job reports the gres/gpumem TRES in sacct. The amount
    is asserted only when a CUDA toolkit is available to actually allocate
    device memory; otherwise the recorded value is legitimately zero and
    only the accounting pipeline is verified."""
    if not [f for f in dmem_log_devs.values() if f.get("dmem") == "enforced"]:
        pytest.skip("no dmem region matched any GPU on this node")

    alloc_mib = 256
    allocator = gpumem_allocator()
    if allocator:
        cmd = f"{allocator} {alloc_mib} 12"
    else:
        cmd = "sleep 12"

    job_id = atf.submit_job_sbatch(
        f"-w {node} --gres=shard:1 -t2 -o /dev/null --wrap '{cmd}'", fatal=True
    )
    atf.wait_for_job_state(job_id, "DONE", fatal=True)

    for t in atf.timer():
        output = atf.run_command_output(
            f"sacct -j {job_id}.batch -P -n --format=TRESUsageInTot",
            fatal=True,
            quiet=True,
        )
        if "gres/gpumem=" in output:
            break
    else:
        assert False, f"gres/gpumem never showed in sacct: {output}"

    if allocator:
        match = re.search(r"gres/gpumem=([0-9.]+)([KMGT]?)", output)
        factors = {"": 1, "K": 1 << 10, "M": 1 << 20, "G": 1 << 30, "T": 1 << 40}
        used = float(match.group(1)) * factors[match.group(2)]
        assert used >= (alloc_mib << 20), (
            f"gres/gpumem must account at least the {alloc_mib} MiB "
            f"allocated, got {output}"
        )

    atf.run_command("rm -f alloc_vram alloc_vram.cu", fatal=True, quiet=True)


def test_dmemregion_off(dmem_log_devs):
    """DmemRegion=off keeps a device unenforced even under
    ConstrainDeviceMemory=yes: its state line drops the dmem= field, and a
    shard job gets no SLURM_SHARD_MEM_PER_GPU and no dmem limit.

    Must run last in this module: it rewrites gres.conf and restarts Slurm.
    """
    regions = [
        fields["region"]
        for fields in dmem_log_devs.values()
        if fields.get("dmem") == "enforced"
    ]
    if not regions:
        pytest.skip("no dmem region matched any GPU on this node")

    if not atf.properties["auto-config"]:
        pytest.skip("Needs auto-config to rewrite gres.conf and restart Slurm")

    gpus = local_gpu_device_files()
    gres_conf = "".join(f"Name=gpu File={gpu} DmemRegion=off\n" for gpu in gpus)
    gres_conf += f"Name=shard Count={len(gpus) * SHARDS_PER_GPU}\n"
    atf.run_command(
        f"cat > {atf.properties['slurm-config-dir']}/gres.conf",
        input=gres_conf,
        user=atf.properties["slurm-user"],
        fatal=True,
        quiet=True,
    )
    atf.restart_slurm(quiet=True)
    atf.wait_for_node_state(node, "IDLE", fatal=True)

    for path, fields in parse_dmem_log_devs().items():
        assert "dmem" not in fields, f"{path} must not be enforced with DmemRegion=off"

    file_out = "dmem_off_job.out"
    script = "dmem_off_job.sh"
    atf.make_bash_script(
        script,
        """
job_cg=$(sed -n 's/^0:://p' /proc/self/cgroup)
job_cg=${job_cg%%/step_*}
echo "DMEM_MAX:$(cat /sys/fs/cgroup${job_cg}/dmem.max | tr '\\n' ';')"
echo "SHARDENV:$SLURM_SHARD_MEM_PER_GPU"
""",
    )
    job_id = atf.submit_job_sbatch(
        f"-w {node} --gres=shard:3 -t1 -o {file_out} {script}", fatal=True
    )
    atf.wait_for_job_state(job_id, "DONE", fatal=True)
    # SHARDENV is the last line the job writes, so once it appears the
    # output is complete and a single read below is race-free
    atf.assert_file_contents(file_out, "SHARDENV:", contains=True)
    output = atf.run_command_output(f"cat {file_out}", fatal=True)

    assert re.search(
        r"^SHARDENV:\s*$", output, re.MULTILINE
    ), "SLURM_SHARD_MEM_PER_GPU must not be set for excluded devices"
    for region in regions:
        assert not re.search(
            rf"{re.escape(region)} \d", output
        ), f"No dmem limit may be written for excluded region {region}"

    atf.run_command(f"rm -f {file_out}", fatal=True, quiet=True)
