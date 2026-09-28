############################################################################
# Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
############################################################################
import logging
import re

import pytest

import atf

OUTPUT_FILE = "./file.out"
ERROR_FILE = "./file.err"

# The spank_plugin plugin returns -ESPANK_ERROR on the targeted callback.
# See ESPANK_ERROR in slurm/slurm_errno.h.
SPANK_ERROR_RC = -3000

# Hooks (without the slurm_spank_ prefix) that spank.8 documents to be called
# before each hook in the same context. Note that task_init_privileged and
# task_post_fork "can run in parallel", and that the order of task_post_fork
# and task_init is not documented.
HOOKS_BEFORE = {
    "local": {
        "init": [],
        "init_post_opt": ["init"],
        "local_user_init": ["init", "init_post_opt"],
        "exit": ["init", "init_post_opt", "local_user_init"],
    },
    "allocator": {
        "init": [],
        "init_post_opt": ["init"],
        "exit": ["init", "init_post_opt"],
    },
    "remote": {
        "init": [],
        "init_post_opt": ["init"],
        "user_init": ["init", "init_post_opt"],
        "task_init_privileged": ["init", "init_post_opt", "user_init"],
        "task_post_fork": ["init", "init_post_opt", "user_init"],
        "task_init": ["init", "init_post_opt", "user_init", "task_init_privileged"],
        "task_exit": [
            "init",
            "init_post_opt",
            "user_init",
            "task_init_privileged",
            "task_post_fork",
            "task_init",
        ],
        "exit": [
            "init",
            "init_post_opt",
            "user_init",
            "task_init_privileged",
            "task_post_fork",
            "task_init",
            "task_exit",
        ],
    },
}

# The only hooks that spank.8 documents to be called in each context. Remote
# context is not restricted.
HOOKS_IN_CONTEXT = {
    "local": {"init", "init_post_opt", "local_user_init", "exit"},
    "allocator": {"init", "init_post_opt", "exit"},
    "slurmd": {"init", "slurmd_exit"},
    "job_script": {"job_prolog", "job_epilog"},
}


@pytest.fixture(scope="module", autouse=True)
def setup(spank_plugin):
    """Setup test environment once for all tests"""
    global test_node

    logging.info("Setting up SPANK error code test environment")

    # Ensure the SPANK plugin is included in plugstack.conf. The plugin is
    # driven by SPANK_FAIL_TEST_FUNC / SPANK_FAIL_TEST_CTXT env vars set on
    # each test invocation, so it's loaded with no args.
    atf.require_config_parameter(
        "required", spank_plugin, delimiter=" ", source="plugstack"
    )

    # Just to speed up the test
    atf.require_config_parameter_includes("SchedulerParameters", "bf_interval=1")

    # TODO: Add support/parametric test cases for proctrack/pgid
    atf.require_config_parameter("ProctrackType", "proctrack/cgroup")

    # Start Slurm
    logging.info("Starting Slurm daemons")
    atf.require_nodes(1)
    atf.require_slurm_running()

    test_node = next(iter(atf.nodes))


@pytest.fixture(autouse=True)
def cleanup_between_tests():
    """Reset per-test state: resume any drained node and unlink batch I/O files."""
    yield

    if "DRAIN" in atf.get_node_parameter(test_node, "state"):
        atf.run_command(
            f"scontrol update NodeName={test_node} State=RESUME Reason='test_147_3'",
            fatal=True,
            user="slurm",
        )
    atf.wait_for_node_state(test_node, "IDLE", operator=atf.SetMatch.EQUAL, fatal=True)

    atf.run_command(f"rm -f {OUTPUT_FILE} {ERROR_FILE}")


def get_hook_marker(spank_tmp, function, context):
    """Return (job_id, rc) recorded by the plugin when the hook ran.

    The plugin writes it to a file instead of stderr, where output of
    different processes (srun, slurmstepd and its forked tasks) can interleave.
    """
    marker = f"{spank_tmp}/{function}_{context}_log"
    atf.wait_for_file(marker, fatal=True)
    for t in atf.timer():
        content = atf.run_command_output(f'cat "{marker}"', fatal=True)
        match = re.search(r"job_id=(\d+) rc=(-?\d+)", content)
        if match:
            return int(match.group(1)), int(match.group(2))
    else:
        assert False, f"SPANK plugin should record job_id and rc in {marker}"


def get_hooks_run(spank_tmp):
    """Return {(function, context): [rc, ...]} for every hook that ran.

    Each hook appends one line to {function}_{context}_log per run.
    """
    hooks = {}
    output = atf.run_command_output(
        f"grep -H . {spank_tmp}/slurm_spank_*_log 2>/dev/null || true"
    )
    for line in output.splitlines():
        match = re.search(
            rf"{spank_tmp}/(slurm_spank_\w+)_(local|remote|allocator|slurmd|job_script)_log:job_id=\d+ rc=(-?\d+)",
            line,
        )
        if match:
            hook = (match.group(1), match.group(2))
            hooks.setdefault(hook, []).append(int(match.group(3)))
    logging.debug(f"SPANK hooks run: {hooks}")
    return hooks


def assert_hooks_run(spank_tmp, function, context, job_id):
    """Assert the hooks that ran match what spank.8 documents.

    - The hooks documented before the failing one, in its context, ran and
      succeeded.
    - No hook ran in a context where it's documented not to be called.

    What runs after the failing hook is not documented, so it's not checked.
    """
    # Let every hook of the job run before checking them all
    if job_id > 0:
        atf.wait_for_job_state(job_id, "DONE", fatal=True)
    hooks = get_hooks_run(spank_tmp)

    short_function = function.replace("slurm_spank_", "")
    for hook in HOOKS_BEFORE[context][short_function]:
        rcs = hooks.get((f"slurm_spank_{hook}", context))
        assert (
            rcs
        ), f"slurm_spank_{hook} should run before {function} in {context} context, but it didn't"
        assert all(
            rc == 0 for rc in rcs
        ), f"slurm_spank_{hook} should return 0 in {context} context, but returned {rcs}"

    for hook, ctx in hooks:
        if ctx in HOOKS_IN_CONTEXT:
            assert (
                hook.replace("slurm_spank_", "") in HOOKS_IN_CONTEXT[ctx]
            ), f"{hook} should not be called in {ctx} context, but it was"


def assert_job_end_state(job_id, allowed_states):
    """Wait for the job to finish, then assert its final JobState.

    We wait for the aggregate DONE state and then read the actual terminal
    state, rather than polling for FAILED/CANCELLED directly. This way an
    unexpected outcome is reported explicitly: in particular a job that ends in
    TIMEOUT (e.g. an allocation left running until its time limit because the
    client tore down uncleanly) is surfaced as such, instead of hiding behind a
    generic "did not reach FAILED/CANCELLED" wait timeout.
    """
    assert atf.wait_for_job_state(
        job_id, "DONE", fatal=True
    ), f"Job {job_id} should reach a terminal state, but did not"
    state = atf.get_job_parameter(job_id, "JobState", default="NOT_FOUND")
    assert (
        state in allowed_states
    ), f"Job {job_id} should end in one of {allowed_states}, but ended in {state}"


@pytest.mark.parametrize(
    "function, context, xfail, drains, fails, jobid_assigned",
    [
        ("slurm_spank_init", "local", True, False, True, False),
        ("slurm_spank_init_post_opt", "local", True, False, True, False),
        ("slurm_spank_local_user_init", "local", True, False, True, True),
        ("slurm_spank_user_init", "remote", True, False, True, True),
        ("slurm_spank_task_init_privileged", "remote", True, False, True, True),
        ("slurm_spank_task_post_fork", "remote", True, False, True, True),
        ("slurm_spank_task_init", "remote", True, False, True, True),
        ("slurm_spank_task_exit", "remote", False, False, False, True),
        ("slurm_spank_exit", "local", False, False, True, False),
    ],
)
def test_srun(spank_tmp, function, context, xfail, drains, fails, jobid_assigned):
    """Validate srun behavior when SPANK callbacks fail."""
    logging.info("Testing srun command")
    logging.debug(f"Function {function}, xfail: {xfail}")

    result = atf.run_command(
        command=f"srun -K -I10 -W10 -w {test_node} -t1 true",
        env_vars=f"SPANK_HOOK_CREATE_FILE=1 SPANK_FAIL_TEST_FUNC={function} SPANK_FAIL_TEST_CTXT={context}",
    )
    if xfail:
        assert (
            result["exit_code"] != 0
        ), f"srun should fail, but exited with {result['exit_code']}"
    else:
        assert (
            result["exit_code"] == 0
        ), f"srun should succeed, but exited with {result['exit_code']}"

    # Extract JobID and verify the SPANK plugin failed the targeted callback
    job_id, plugin_rc = get_hook_marker(spank_tmp, function, context)
    logging.debug(f"SPANK plugin failed {function} ({context}) of job {job_id}")
    assert (
        plugin_rc == SPANK_ERROR_RC
    ), f"SPANK plugin should return {SPANK_ERROR_RC}, but returned {plugin_rc}"

    if jobid_assigned:
        assert int(job_id) > 0, "JobID should be assigned"

        if fails:
            assert_job_end_state(int(job_id), ("FAILED", "CANCELLED"))
    else:
        assert int(job_id) == 0, "JobID should NOT be assigned"

    if drains:
        node_state = atf.get_node_parameter(test_node, "state")
        assert [
            "DRAIN"
        ] == node_state, f"Test node should be drained, but is in state {node_state}"

    assert_hooks_run(spank_tmp, function, context, int(job_id))


@pytest.mark.parametrize(
    "function, context, xfail, drains, fails, jobid_assigned",
    [
        ("slurm_spank_init", "allocator", True, False, True, False),
        ("slurm_spank_init_post_opt", "allocator", True, False, True, False),
        ("slurm_spank_init", "local", True, False, True, False),
        ("slurm_spank_init_post_opt", "local", True, False, True, False),
        ("slurm_spank_local_user_init", "local", True, False, True, True),
        ("slurm_spank_user_init", "remote", True, False, True, True),
        ("slurm_spank_task_init_privileged", "remote", True, False, True, True),
        ("slurm_spank_task_post_fork", "remote", True, False, True, True),
        ("slurm_spank_task_init", "remote", True, False, True, True),
        ("slurm_spank_task_exit", "remote", False, False, False, True),
        ("slurm_spank_exit", "local", False, False, True, False),
        ("slurm_spank_exit", "allocator", False, False, True, False),
    ],
)
def test_salloc(spank_tmp, function, context, xfail, drains, fails, jobid_assigned):
    """Validate salloc+srun behavior when SPANK callbacks fail."""
    logging.info("Testing salloc command")
    logging.debug(f"Function {function}, xfail: {xfail}")

    result = atf.run_command(
        command=f"salloc -K -I10 -w {test_node} -t1 srun -K -I10 -W10 -t1 true",
        env_vars=f"SPANK_HOOK_CREATE_FILE=1 SPANK_FAIL_TEST_FUNC={function} SPANK_FAIL_TEST_CTXT={context}",
    )
    if xfail:
        assert (
            result["exit_code"] != 0
        ), f"salloc should fail, but exited with {result['exit_code']}"
    else:
        assert (
            result["exit_code"] == 0
        ), f"salloc should succeed, but exited with {result['exit_code']}"

    # Extract JobID and verify the SPANK plugin failed the targeted callback
    job_id, plugin_rc = get_hook_marker(spank_tmp, function, context)
    logging.debug(f"SPANK plugin failed {function} ({context}) of job {job_id}")
    assert (
        plugin_rc == SPANK_ERROR_RC
    ), f"SPANK plugin should return {SPANK_ERROR_RC}, but returned {plugin_rc}"

    if jobid_assigned:
        assert int(job_id) > 0, "JobID should be assigned"

        if fails:
            assert_job_end_state(int(job_id), ("FAILED", "CANCELLED"))

    assert_hooks_run(spank_tmp, function, context, int(job_id))


@pytest.mark.parametrize(
    "function, context, xfail, drains, fails, jobid_assigned, outfile",
    [
        ("slurm_spank_init", "allocator", True, False, True, False, False),
        ("slurm_spank_init_post_opt", "allocator", True, False, True, False, False),
        ("slurm_spank_init", "local", True, False, True, True, False),
        ("slurm_spank_init_post_opt", "local", True, False, True, True, False),
        ("slurm_spank_local_user_init", "local", True, False, True, True, False),
        ("slurm_spank_user_init", "remote", True, True, True, True, False),
        ("slurm_spank_task_init_privileged", "remote", True, False, True, True, False),
        ("slurm_spank_task_post_fork", "remote", True, True, True, True, False),
        ("slurm_spank_task_init", "remote", True, False, True, True, False),
        ("slurm_spank_task_exit", "remote", False, False, False, True, True),
        ("slurm_spank_exit", "local", False, False, False, True, True),
        ("slurm_spank_exit", "allocator", False, False, False, True, True),
    ],
)
def test_sbatch(
    spank_tmp, function, context, xfail, drains, fails, jobid_assigned, outfile
):
    """Validate sbatch behavior when SPANK callbacks fail."""
    logging.info("Testing sbatch command")
    logging.debug(f"Function {function}, in context: {context}, xfail: {xfail}")

    result = atf.run_command(
        command=f"sbatch --no-requeue -W -w {test_node} -t1 -o {OUTPUT_FILE} -e {ERROR_FILE} --wrap=\"srun echo 'IT_RAN'\"",
        env_vars=f"SPANK_HOOK_CREATE_FILE=1 SPANK_FAIL_TEST_FUNC={function} SPANK_FAIL_TEST_CTXT={context}",
    )
    if xfail:
        assert (
            result["exit_code"] != 0
        ), f"sbatch should fail, but exited with {result['exit_code']}"
    else:
        assert (
            result["exit_code"] == 0
        ), f"sbatch should succeed, but exited with {result['exit_code']}"

    # Extract JobID
    job_id = 0
    output = result["stdout"]
    pattern = r"Submitted batch job (\d+)"
    match = re.search(pattern, output)
    if match:
        job_id = match.group(1)
        logging.debug(f"Submitted batch job - Job ID: {job_id}")

    if jobid_assigned:
        assert int(job_id) > 0, "JobID should be returned"
    else:
        assert int(job_id) == 0, "JobID should NOT be returned"

    # Verify the SPANK plugin failed the targeted callback
    _, plugin_rc = get_hook_marker(spank_tmp, function, context)
    assert (
        plugin_rc == SPANK_ERROR_RC
    ), f"SPANK plugin should return {SPANK_ERROR_RC}, but returned {plugin_rc}"

    if jobid_assigned and fails:
        assert_job_end_state(int(job_id), ("FAILED", "CANCELLED"))
    elif jobid_assigned and not fails:
        assert_job_end_state(int(job_id), ("COMPLETED",))

    if outfile:
        atf.wait_for_file(OUTPUT_FILE, fatal=True)
        for t in atf.timer():
            content = atf.run_command_output(f'cat "{OUTPUT_FILE}"', fatal=True)
            if "IT_RAN" in content:
                break
        else:
            assert (
                False
            ), f"Output file {OUTPUT_FILE} should contain 'IT_RAN', but got: {content}"
    else:
        # The targeted callback failed before the wrapped command could run, so
        # the job must not have produced stdout. The file may be absent (job
        # never started) or present but empty, either way IT_RAN must not appear.
        # The job has already reached a terminal state above, so this is stable.
        content = atf.run_command_output(f'cat "{OUTPUT_FILE}" 2>/dev/null || true')
        assert (
            "IT_RAN" not in content
        ), f"Output file {OUTPUT_FILE} should not contain 'IT_RAN', but got: {content}"

    if drains:
        node_state = atf.get_node_parameter(test_node, "state")
        assert (
            "DRAIN" in node_state
        ), f"Test node should be drained, but is in state {node_state}"

    assert_hooks_run(spank_tmp, function, context, int(job_id))
