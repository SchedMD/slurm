############################################################################
# Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
############################################################################
"""Test --max-pids constrains the pids.max of each step cgroup."""

import pytest

import atf

MAX_PIDS = 17

# Job-wide value used by the sbatch test; every step of the job inherits it.
JOB_MAX_PIDS = 50

# The node every step of this module runs on, and its kernel PID limit, both
# read in setup_module() from a probe step.
NODE = None
PID_MAX = None


def setup_module():
    global NODE, PID_MAX

    atf.require_version(
        (26, 11),
        "bin/srun",
        reason="Issue 50878: srun --max-pids added in 26.11",
    )
    atf.require_auto_config("needs to modify TaskPlugin/cgroup parameters")
    atf.require_config_parameter("CgroupPlugin", "cgroup/v2", source="cgroup")
    atf.require_config_parameter_includes("TaskPlugin", "cgroup")
    atf.require_nodes(1, [("CPUs", 1)])
    atf.require_slurm_running()

    # One probe step picks the node, reads its kernel PID limit and checks
    # that the pids controller reaches the step cgroups. Without it the
    # option is documented as silently ignored, so there is nothing to test.
    probe_script = atf.module_tmp_path / "probe_pids.sh"
    atf.make_bash_script(
        probe_script,
        """
cg=$(awk -F: '/^0::/ {print $3}' /proc/self/cgroup)
echo "$SLURMD_NODENAME"
cat /proc/sys/kernel/pid_max
test -f "/sys/fs/cgroup${cg%/*}/pids.max" && echo yes || echo no
""",
    )
    NODE, pid_max, has_pids = atf.run_job_output(
        f"-N1 -t1 -J test_120_3 {probe_script}", fatal=True
    ).split()
    PID_MAX = int(pid_max)
    if has_pids != "yes":
        pytest.skip(
            f"The pids controller is not available in the step cgroups of {NODE}",
            allow_module_level=True,
        )


def get_pids_max(srun_args):
    """
    Run a step and return the pids.max value found in its user cgroup, plus
    the task cgroup it was derived from (for diagnostics).
    """
    task_script = atf.module_tmp_path / "print_pids_max.sh"
    atf.make_bash_script(
        task_script,
        """
cg=$(awk -F: '/^0::/ {print $3}' /proc/self/cgroup)
echo "$cg"
cat "/sys/fs/cgroup${cg%/*}/pids.max"
""",
    )

    output = atf.run_job_output(
        f"{srun_args} -w {NODE} -n1 -t1 -J test_120_3 {task_script}", fatal=True
    )

    cgroup, pids_max = output.split()

    return pids_max, cgroup


def test_max_pids_sets_cgroup_limit():
    """Verify srun --max-pids sets pids.max in the step's user cgroup"""

    pids_max, cgroup = get_pids_max(f"--max-pids={MAX_PIDS}")
    assert pids_max == str(
        MAX_PIDS
    ), f"Expected pids.max '{MAX_PIDS}' above task cgroup {cgroup}, got: {pids_max}"


def test_max_pids_is_enforced():
    """Verify a step cannot hold more PIDs than --max-pids at the same time"""

    # Fork from python so that a failed fork surfaces as EAGAIN right away.
    # bash would retry it quietly for up to half a minute instead.
    forker = atf.module_tmp_path / "forker.sh"
    atf.make_bash_script(
        forker,
        """
exec python3 - "$1" <<'EOF'
import errno
import os
import sys

forked = 0
for i in range(int(sys.argv[1])):
    try:
        pid = os.fork()
    except OSError as e:
        if e.errno != errno.EAGAIN:
            raise
        break
    if pid == 0:
        os.execvp("sleep", ["sleep", "30"])
    forked += 1
print(forked)
EOF
""",
    )

    # The python process itself is one of the PIDs, so only two children fit.
    forked = atf.run_job_output(
        f"--max-pids=3 -w {NODE} -n1 -t1 -J test_120_3 {forker} 6", fatal=True
    ).strip()
    assert (
        forked == "2"
    ), f"Expected 2 forks to succeed under --max-pids=3, got: {forked}"

    forked = atf.run_job_output(
        f"--max-pids=max -w {NODE} -n1 -t1 -J test_120_3 {forker} 6", fatal=True
    ).strip()
    assert (
        forked == "6"
    ), f"Expected 6 forks to succeed under --max-pids=max, got: {forked}"


def test_no_max_pids_leaves_cgroup_unconstrained():
    """Verify that without srun --max-pids, pids.max is not constrained"""

    pids_max, cgroup = get_pids_max("")
    assert (
        pids_max == "max"
    ), f"Expected pids.max 'max' above task cgroup {cgroup}, got: {pids_max}"


def test_max_pids_max_literal_is_accepted():
    """Verify srun --max-pids=max is accepted and sets pids.max to 'max'"""

    pids_max, cgroup = get_pids_max("--max-pids=max")
    assert (
        pids_max == "max"
    ), f"Expected pids.max 'max' above task cgroup {cgroup}, got: {pids_max}"


def test_max_pids_zero_is_rejected():
    """Verify srun --max-pids=0 is rejected before any step is launched"""

    # pids.max=0 would forbid the kernel from admitting any process into the
    # step cgroup, so the option refuses it up front.
    results = atf.run_command(
        "srun --max-pids=0 -n1 -t1 -J test_120_3 true", xfail=True, fatal=True
    )
    assert (
        "Invalid --max-pids specification" in results["stderr"]
    ), f"Expected the failure to name --max-pids, got: {results['stderr']}"


def test_job_max_pids_applies_to_every_step():
    """Verify the sbatch value is the default of every step and a ceiling for srun

    srun may lower it. A larger value, and max, are rejected.
    """

    file_out = atf.module_tmp_path / "job_max_pids.out"
    step_script = atf.module_tmp_path / "print_step_pids_max.sh"
    atf.make_bash_script(
        step_script,
        """
cg=$(awk -F: '/^0::/ {print $3}' /proc/self/cgroup)
cat "/sys/fs/cgroup${cg%/*}/pids.max"
""",
    )
    job_script = atf.module_tmp_path / "job_max_pids.sh"
    atf.make_bash_script(
        job_script,
        f"""
echo "batch=$({step_script})"
echo "inherited=$(srun -n1 {step_script})"
echo "lowered=$(srun -n1 --max-pids={JOB_MAX_PIDS - 10} {step_script})"
if srun -n1 --max-pids={JOB_MAX_PIDS + 1} true 2> raised.err; then
    echo "raised=accepted"
else
    echo "raised=rejected"
fi
echo "message=$(grep -q 'Invalid --max-pids specification' raised.err && echo found || echo missing)"
if srun -n1 --max-pids=max true 2> /dev/null; then
    echo "max=accepted"
else
    echo "max=rejected"
fi
""",
    )

    job_id = atf.submit_job_sbatch(
        f"--max-pids={JOB_MAX_PIDS} -w {NODE} -N1 -t1 -J test_120_3 --output={file_out} {job_script}",
        fatal=True,
    )
    atf.wait_for_job_state(job_id, "DONE", fatal=True)
    atf.wait_for_file(file_out, fatal=True)

    output = atf.run_command_output(f"cat {file_out}", fatal=True)
    expected = [
        f"batch={JOB_MAX_PIDS}",
        f"inherited={JOB_MAX_PIDS}",
        f"lowered={JOB_MAX_PIDS - 10}",
        "raised=rejected",
        "message=found",
        "max=rejected",
    ]
    for line in expected:
        assert line in output.split(
            "\n"
        ), f"Expected '{line}' in the job output, got: {output}"


def test_max_pids_at_kernel_limit_is_accepted():
    """Verify srun --max-pids accepts the kernel PID limit itself"""

    # The clamp only applies above the kernel limit, so the limit itself must
    # be written as is. It guards against an off-by-one in the clamp.
    pids_max, cgroup = get_pids_max(f"--max-pids={PID_MAX}")
    assert pids_max == str(
        PID_MAX
    ), f"Expected pids.max '{PID_MAX}' above task cgroup {cgroup}, got: {pids_max}"


def test_max_pids_above_kernel_limit_is_unlimited():
    """Verify srun --max-pids above the kernel PID limit is written as 'max'"""

    # Such a limit can never be reached, and the kernel rejects values above
    # its own ceiling, so slurmstepd writes max instead of failing the step.
    pids_max, cgroup = get_pids_max(f"--max-pids={PID_MAX + 1}")
    assert (
        pids_max == "max"
    ), f"Expected pids.max 'max' above task cgroup {cgroup}, got: {pids_max}"
