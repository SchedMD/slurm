############################################################################
# Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
############################################################################
"""Test the resctrl groups task/resctrl creates and the steps it places there.

These tests need a kernel with resctrl support and hardware with memory
bandwidth (MB) or L3 cache allocation, such as Intel RDT, AMD PQoS, or Arm
MPAM, and are skipped anywhere else. They read /sys/fs/resctrl on the host
running the test, so the nodes must run on that host.

In auto-config the test writes its own resctrl.yaml from what the hardware
offers. In local-config it checks the groups against the site's resctrl.yaml
and never changes it, so the reconfigure case is skipped.
"""

import math
import re

import pytest
import yaml

import atf

RESCTRL = "/sys/fs/resctrl"
GROUPS = ("cpu_only", "gpu_one_per_socket", "gpu_two_per_socket")

# Share of the full limit given to each group in auto-config. Each group gets
# a different value, so the test can tell which schemata went to which group.
SHARES = {
    "cpu_only": 0.3,
    "gpu_one_per_socket": 0.5,
    "gpu_two_per_socket": 0.7,
}

# What setup() found on this host and configured, used by the tests.
plan = {}


@pytest.fixture(scope="module", autouse=True)
def setup(request):
    atf.require_version(
        (26, 11),
        component="sbin/slurmd",
        reason="Ticket 50728: task/resctrl added in 26.11",
    )
    atf.require_sudo_rights()
    filesystems = read_file("/proc/filesystems").splitlines()
    if not any(line.split()[-1:] == ["resctrl"] for line in filesystems):
        pytest.skip("Requires a kernel with resctrl support", allow_module_level=True)

    mount_dir = resctrl_mount_dir()
    if mount_dir and (mount_dir != RESCTRL):
        pytest.skip(
            f"resctrl is mounted on {mount_dir}, task/resctrl only uses {RESCTRL}",
            allow_module_level=True,
        )

    if atf.properties["auto-config"]:
        request.addfinalizer(cleanup)
        auto_config_setup(mount_dir)
    else:
        local_config_setup(mount_dir)

    # A job without --exclusive must share its node to be confined, which
    # select/linear never lets it do.
    atf.require_config_parameter("SelectType", "select/cons_tres")
    atf.require_config_parameter_includes("TaskPlugin", "task/resctrl")
    atf.require_slurm_running()


def auto_config_setup(mount_dir):
    """Write a resctrl.yaml that suits the resources of this host.

    Reading the resources and domains needs the filesystem mounted, which
    slurmd would do anyway.
    """

    if not mount_dir:
        result = atf.run_command(f"mount -t resctrl resctrl {RESCTRL}", user="root")
        if result["exit_code"]:
            pytest.skip(
                f"Cannot mount resctrl: {result['stderr'].strip()}",
                allow_module_level=True,
            )
        plan["mounted"] = True
    plan["preexisting"] = {group for group in GROUPS if group_exists(group)}

    plan_schemata()
    plan["expected"] = {
        group: parse_schemata(schemata_line(value))
        for group, value in plan["values"].items()
    }
    plan["saved"] = {}
    for group in plan["preexisting"]:
        line = schemata_line_of(group)
        if line:
            plan["saved"][group] = line

    atf.require_config_file("resctrl.yaml", resctrl_yaml(plan["values"]))


def local_config_setup(mount_dir):
    """Expect the groups the site's own resctrl.yaml defines.

    Without the file slurmd does not manage resctrl at all, so check for it
    before the mount, which is only its consequence.
    """

    path = f"{atf.properties['slurm-config-dir']}/resctrl.yaml"
    conf = yaml.safe_load(read_file(path, default="")) or {}
    if not conf.get("groups"):
        pytest.skip(f"Requires the groups in {path}", allow_module_level=True)
    plan["expected"] = {
        group["name"]: parse_schemata(group.get("schemata") or "")
        for group in conf["groups"]
        if isinstance(group, dict) and (group.get("name") in GROUPS)
    }

    if not mount_dir:
        pytest.skip(
            f"resctrl is not mounted on {RESCTRL}, see task/resctrl in the slurmd log",
            allow_module_level=True,
        )


def cleanup():
    """Undo what auto-config did to /sys/fs/resctrl.

    Slurm never removes its groups. Unmount resctrl if this module mounted
    it, which removes every group. Otherwise remove only the groups that did
    not exist before, and put back the limits of the ones that did.
    """

    if plan.get("mounted"):
        atf.run_command(f"umount {RESCTRL}", user="root")
        return
    for group in GROUPS:
        if group not in plan.get("preexisting", GROUPS):
            if group_exists(group):
                atf.run_command(f"rmdir {RESCTRL}/{group}", user="root")
        elif group in plan.get("saved", {}):
            line = plan["saved"][group]
            atf.run_command(f"echo '{line}' > {RESCTRL}/{group}/schemata", user="root")


def read_file(path, default=None):
    """Return the contents of path, or default if it cannot be read."""

    result = atf.run_command(f"cat {path}", quiet=True)
    if result["exit_code"]:
        if default is None:
            pytest.fail(f"Cannot read {path}: {result['stderr'].strip()}")
        return default
    return result["stdout"]


def parse_schemata(text):
    """Return {resource: {domain: value}} from the contents of a schemata."""

    schemata = {}
    for line in text.splitlines():
        resource, sep, domains = line.strip().partition(":")
        if not sep:
            continue
        schemata[resource] = dict(
            domain.strip().split("=", 1) for domain in domains.split(";") if domain
        )
    return schemata


def group_exists(group):
    return atf.run_command_exit(f"test -f {RESCTRL}/{group}/tasks", quiet=True) == 0


def groups_of(pid):
    """Return the Slurm groups whose tasks file lists pid."""

    return [
        group
        for group in GROUPS
        if str(pid) in read_file(f"{RESCTRL}/{group}/tasks").split()
    ]


def resctrl_mount_dir():
    """Return where resctrl is mounted, preferring RESCTRL, or None."""

    dirs = []
    for line in read_file("/proc/self/mounts").splitlines():
        fields = line.split()
        if len(fields) > 2 and fields[2] == "resctrl":
            dirs.append(fields[1])
    if RESCTRL in dirs:
        return RESCTRL
    return dirs[0] if dirs else None


def mb_plan():
    """Plan memory bandwidth limits from the root group and info/MB.

    The root group holds the full limit (100 percent on Intel and Arm, a
    bandwidth on AMD). Values are rounded up to the granularity the kernel
    uses and kept at or above its minimum.
    """

    gran = int(read_file(f"{RESCTRL}/info/MB/bandwidth_gran", default="1"))
    low = int(read_file(f"{RESCTRL}/info/MB/min_bandwidth", default="0"))
    full = max(int(value) for value in plan["root"]["MB"].values())

    def value(share):
        rounded = math.ceil(full * share / gran) * gran
        return min(max(rounded, low), full)

    plan["fmt"] = str
    return {group: value(SHARES[group]) for group in GROUPS}


def l3_plan():
    """Plan contiguous L3 cache masks from the low bits of info/L3/cbm_mask."""

    nbits = int(read_file(f"{RESCTRL}/info/L3/cbm_mask"), 16).bit_length()
    min_bits = int(read_file(f"{RESCTRL}/info/L3/min_cbm_bits", default="1"))

    def value(share):
        bits = min(max(math.ceil(nbits * share), min_bits), nbits)
        return (1 << bits) - 1

    plan["fmt"] = lambda mask: f"{mask:x}"
    return {group: value(SHARES[group]) for group in GROUPS}


def plan_schemata():
    """Pick the resource to limit and the value of each group, or skip."""

    plan["root"] = parse_schemata(read_file(f"{RESCTRL}/schemata"))
    if "MB" in plan["root"]:
        plan["resource"] = "MB"
        plan["values"] = mb_plan()
    elif "L3" in plan["root"]:
        plan["resource"] = "L3"
        plan["values"] = l3_plan()
    else:
        pytest.skip(
            f"Requires MB or L3 allocation, {RESCTRL}/schemata lists {sorted(plan['root'])}",
            allow_module_level=True,
        )
    plan["domains"] = sorted(plan["root"][plan["resource"]], key=int)

    # The three groups, the root group, and every group made outside Slurm
    # must fit in the CLOSIDs of the hardware, or slurmd refuses to start.
    closids = read_file(f"{RESCTRL}/info/*/num_closids", default="").split()
    groups = atf.run_command_output(f"ls -d {RESCTRL}/*/tasks", quiet=True).split()
    foreign = [path for path in groups if path.split("/")[-2] not in GROUPS]
    needed = len(GROUPS) + 1 + len(foreign)
    available = min((int(n) for n in closids), default=needed)
    if available < needed:
        pytest.skip(
            f"Requires {needed} CLOSIDs, the hardware has {available}",
            allow_module_level=True,
        )


def schemata_line(value):
    domains = ";".join(f"{d}={plan['fmt'](value)}" for d in plan["domains"])
    return f"{plan['resource']}:{domains}"


def schemata_line_of(group):
    """Return the line of the planned resource in a group's schemata."""

    for line in read_file(f"{RESCTRL}/{group}/schemata", default="").splitlines():
        if line.strip().startswith(f"{plan['resource']}:"):
            return line.strip()
    return None


def value_matches(resource, have, want):
    """Compare a value read back from a schemata with a configured one.

    Cache masks must match exactly. A bandwidth is rounded to the granularity
    of its resource, and Arm MPAM converts it to a hardware fraction and back,
    so it may come back one granularity step away.
    """

    info = f"{RESCTRL}/info/{resource}"
    if read_file(f"{info}/cbm_mask", default=""):
        return int(have, 16) == int(want, 16)
    gran = int(read_file(f"{info}/bandwidth_gran", default="1"))
    return abs(int(have) - int(want)) <= gran


def schemata_matches(group, expected):
    """Return True if the schemata of group holds every value in expected."""

    current = parse_schemata(read_file(f"{RESCTRL}/{group}/schemata", default=""))
    return all(
        (domain in current.get(resource, {}))
        and value_matches(resource, current[resource][domain], want)
        for resource, domains in expected.items()
        for domain, want in domains.items()
    )


def resctrl_yaml(values):
    lines = ["groups:"]
    for group in GROUPS:
        lines += [
            f"  - name: {group}",
            "    schemata: |",
            f"      {schemata_line(values[group])}",
        ]
    return "\n".join(lines) + "\n"


def write_resctrl_yaml(values):
    atf.run_command(
        f"cat > {atf.properties['slurm-config-dir']}/resctrl.yaml",
        input=resctrl_yaml(values),
        user=atf.properties["slurm-user"],
        fatal=True,
    )
    atf.run_command(
        "scontrol reconfigure", user=atf.properties["slurm-user"], fatal=True
    )


def wait_for_pid(file_name):
    """Wait for a job to write its pid to file_name and return it."""

    for _ in atf.timer(fatal=True):
        pid = read_file(file_name, default="").strip()
        if pid.isdigit():
            return int(pid)


def gives_whole_nodes(part, exclusive):
    """Return True if a job in part gets whole nodes.

    Exclusive=NODE and Exclusive=TOPO give every job whole nodes.
    OverSubscribe=FORCE with a count above one shares every node, so even an
    --exclusive job shares its node there and is confined like any other.
    """

    mode, _, count = str(part.get("OverSubscribe") or "").partition(":")
    if part.get("Exclusive") in ("NODE", "TOPO"):
        return True
    if (mode == "FORCE") and (not count.isdigit() or int(count) > 1):
        return False
    return exclusive


def job_partition(exclusive, among=None):
    """Return a partition where a job gets whole nodes only if exclusive.

    Prefer the default partition, and pick from among when given. Skip if no
    partition fits.
    """

    parts = sorted(
        atf.get_partitions(fatal=True).values(),
        key=lambda part: part.get("Default") != "YES",
    )
    for part in parts:
        if among is not None and part["PartitionName"] not in among:
            continue
        if (part.get("State") == "UP") and part.get("TotalNodes"):
            if gives_whole_nodes(part, exclusive) == exclusive:
                return part["PartitionName"]
    whole = "gets" if exclusive else "does not get"
    pytest.skip(f"Requires a partition where a job {whole} whole nodes")


def test_groups_created():
    """slurmd creates the three groups and writes their schemata"""

    for group in GROUPS:
        expected = plan["expected"].get(group, {})
        assert group_exists(group), f"Group {group} was not created in {RESCTRL}"
        assert schemata_matches(
            group, expected
        ), f"Group {group} should hold {expected}"


def test_shared_job_in_cpu_only():
    """The batch script and the steps of a job without GPUs go to cpu_only"""

    atf.make_bash_script(
        "job.sh",
        """\
echo $$ > batch_pid
srun -n1 bash -c 'echo $$ > step_pid; exec sleep infinity' &
sleep infinity
""",
    )
    partition = job_partition(exclusive=False)
    atf.submit_job_sbatch(f"-p {partition} -N1 -n1 -t5 -o /dev/null job.sh", fatal=True)

    for pid_file in ("batch_pid", "step_pid"):
        pid = wait_for_pid(pid_file)
        assert groups_of(pid) == [
            "cpu_only"
        ], f"Process {pid} from {pid_file} should be in cpu_only only"


def test_exclusive_job_not_confined():
    """A job that owns the whole node is left out of the Slurm groups"""

    partition = job_partition(exclusive=True)
    atf.submit_job_sbatch(
        f"-p {partition} --exclusive -N1 -t5 -o /dev/null --wrap 'echo $$ > batch_pid; exec sleep infinity'",
        fatal=True,
    )

    pid = wait_for_pid("batch_pid")
    assert not groups_of(pid), f"Process {pid} of an exclusive job is in a group"


def test_gpu_job_in_gpu_one_per_socket():
    """A job with one GPU on the node goes to gpu_one_per_socket"""

    gpu_nodes = [
        node
        for node in atf.get_nodes(fatal=True).values()
        if re.search(r"(^|,)gpu(:|\(|$)", node.get("gres") or "")
    ]
    if not gpu_nodes:
        pytest.skip("Requires a node with gpu gres")
    node = gpu_nodes[0]
    partition = job_partition(exclusive=False, among=node.get("partitions") or [])

    atf.submit_job_sbatch(
        f"-p {partition} -w {node['name']} --gpus=1 -t5 -o /dev/null --wrap 'echo $$ > batch_pid; exec sleep infinity'",
        fatal=True,
    )

    pid = wait_for_pid("batch_pid")
    assert groups_of(pid) == [
        "gpu_one_per_socket"
    ], f"Process {pid} of a one GPU job should be in gpu_one_per_socket only"


def test_reconfigure_updates_schemata():
    """A changed schemata applies to the jobs already in the group"""

    if not atf.properties["auto-config"]:
        pytest.skip("Rewrites resctrl.yaml, which only auto-config may do")
    new_value = plan["values"]["gpu_two_per_socket"]
    if new_value == plan["values"]["cpu_only"]:
        pytest.skip("The hardware cannot tell the cpu_only and new limits apart")
    new_expected = parse_schemata(schemata_line(new_value))

    partition = job_partition(exclusive=False)
    atf.submit_job_sbatch(
        f"-p {partition} -N1 -n1 -t5 -o /dev/null --wrap 'echo $$ > batch_pid; exec sleep infinity'",
        fatal=True,
    )
    pid = wait_for_pid("batch_pid")
    assert groups_of(pid) == ["cpu_only"], f"Process {pid} should be in cpu_only"

    write_resctrl_yaml(dict(plan["values"], cpu_only=new_value))
    changed = False
    for _ in atf.timer():
        if schemata_matches("cpu_only", new_expected):
            changed = True
            break
    still_in = groups_of(pid) == ["cpu_only"]

    # Put back the original limits for any test that runs after this one.
    write_resctrl_yaml(plan["values"])
    for _ in atf.timer(fatal=True):
        if schemata_matches("cpu_only", plan["expected"]["cpu_only"]):
            break

    assert changed, "cpu_only did not get the new schemata after a reconfigure"
    assert still_in, f"Process {pid} left cpu_only during the reconfigure"
