############################################################################
# Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
############################################################################
"""Test that the peak PID count of a step is accounted as the pids TRES.

A step that forks a known number of children must report that count in the
pids TRES, both live through sstat and from the database through sacct.
"""

import pytest

import atf

CHILDREN = 4
# The forking process itself counts too.
EXPECTED_PIDS = CHILDREN + 1


@pytest.fixture(scope="module", autouse=True)
def setup():
    atf.require_version(
        (26, 11),
        component="sbin/slurmd",
        reason="Issue 50878: pids TRES accounting added in 26.11",
    )
    atf.require_version(
        (26, 11),
        component="bin/sstat",
        reason="Issue 50878: sstat pids fields added in 26.11",
    )
    atf.require_version(
        (26, 11),
        component="bin/sacct",
        reason="Issue 50878: sacct pids fields added in 26.11",
    )
    atf.require_accounting()
    atf.require_config_parameter("CgroupPlugin", "cgroup/v2", source="cgroup")
    atf.require_config_parameter("JobAcctGatherType", "jobacct_gather/cgroup")
    atf.require_config_parameter("JobAcctGatherFrequency", ["task=2", "2"])
    atf.require_nodes(1, [("CPUs", 1)])
    atf.require_slurm_running()

    # The pids TRES is only collected where the pids controller reaches the
    # step cgroups, so check that before counting anything.
    probe_script = atf.module_tmp_path / "probe_pids.sh"
    atf.make_bash_script(
        probe_script,
        """
cg=$(awk -F: '/^0::/ {print $3}' /proc/self/cgroup)
grep -qw pids "/sys/fs/cgroup${cg%/*}/cgroup.controllers" && echo yes || echo no
""",
    )
    if (
        atf.run_job_output(f"-N1 -t1 -J test_120_4 {probe_script}", fatal=True).strip()
        != "yes"
    ):
        pytest.skip("The pids controller is not available in the step cgroups")


def test_pids_tres_is_accounted():
    """Verify the step's peak PID count reaches sstat and sacct as pids"""

    forked = atf.module_tmp_path / "forked"
    forker = atf.module_tmp_path / "forker.sh"
    atf.make_bash_script(
        forker,
        f"""
exec python3 - <<'PY'
import os
import pathlib

pids = []
for i in range({CHILDREN}):
    pid = os.fork()
    if pid == 0:
        os.execvp("sleep", ["sleep", "infinity"])
    pids.append(pid)
pathlib.Path("{forked}").touch()
for pid in pids:
    os.waitpid(pid, 0)
PY
""",
    )
    job_id = atf.submit_job_sbatch(
        f"-n1 -t2 -J test_120_4 --wrap 'srun -n1 {forker}'", fatal=True
    )
    atf.wait_for_file(forked, fatal=True)

    sstat_fields = "MaxPids,MaxPidsTask,AvePids"
    expected = f"{EXPECTED_PIDS}|0|{EXPECTED_PIDS}"
    sstat = None
    for _ in atf.timer(fatal=True):
        sstat = atf.run_command_output(
            f"sstat -n -P -o {sstat_fields} -j {job_id}.0", fatal=True
        ).strip()
        if sstat:
            break
    assert (
        sstat == expected
    ), f"Expected sstat {sstat_fields} of {expected}, got: {sstat}"

    atf.cancel_jobs([job_id], fatal=True)

    # The step usage reaches the database when the step completes.
    sacct = None
    for _ in atf.timer(fatal=True):
        sacct = atf.run_command_output(
            f"sacct -n -P -j {job_id}.0 -o {sstat_fields},TRESUsageInMax",
            fatal=True,
        ).strip()
        if sacct and sacct.split("|")[0]:
            break
    max_pids, max_task, ave_pids, tres_max = sacct.split("|")
    assert max_pids == str(EXPECTED_PIDS) and ave_pids == str(
        EXPECTED_PIDS
    ), f"Expected sacct MaxPids and AvePids of {EXPECTED_PIDS}, got: {sacct}"
    assert max_task == "0", f"Expected the peak on task 0, got: {sacct}"
    assert f"pids={EXPECTED_PIDS}" in tres_max.split(
        ","
    ), f"Expected pids={EXPECTED_PIDS} in TRESUsageInMax, got: {tres_max}"
