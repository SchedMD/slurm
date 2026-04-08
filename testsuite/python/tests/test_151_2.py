############################################################################
# Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
############################################################################
"""
Tests for ticket 20588: oversubscribe policy vs exclusive intent in what
clients see (scontrol show job/partition), in partition updates, and in batch
job environment variables.

scontrol uses OverSubscribe= for both jobs and partitions. Job show uses compact
tokens (NO|YES|OK). Partition show uses the detailed partition form (NO, FORCE:n,
YES:n) as in slurm.conf.

Behavior under test:
- Jobs: OverSubscribe=NO|YES|OK; Exclusive=NO|NODE|USER|MCS|TOPO
- Partitions: OverSubscribe=NO|FORCE:n|YES:n; Exclusive=NO|NODE|USER|TOPO
  (no MCS on partitions)
- Partition OverSubscribe=EXCLUSIVE is accepted (compat; same effect as
  Exclusive=NODE); exercised via update + running job display
- Partition Exclusive= accepts a single token: NO, NODE, USER, or TOPO
- Batch jobs: SLURM_JOB_OVERSUBSCRIBE and SLURM_JOB_EXCLUSIVE use the same token
  families as job display for the batch step (this file only checks the job
  script environment, not slurmd prep or other hooks)

Covered by this module:
- Token checks on scontrol show job and on every partition from show partition
- sbatch --oversubscribe and --exclusive[=user|mcs|topo|...]: show job matches
  the request; env tests for several exclusive modes and for oversubscribe
- Partition built by fixture: default job inherits partition lines; job flags
  override
- Regression: job --exclusive=topo on OverSubscribe=EXCLUSIVE partition keeps
  Exclusive=TOPO when RUNNING (not collapsed to misleading OverSubscribe-only
  text)
- scontrol update part: Exclusive= values above; OverSubscribe=FORCE -> show
  part OverSubscribe=FORCE:n; invalid OverSubscribe rejected with a useful message
- Default sbatch: output includes SLURM_JOB_OVERSUBSCRIBE= and
  SLURM_JOB_EXCLUSIVE= lines whose values are in the allowed sets

Not covered:
- Options read only from slurm.conf at restart (tests use scontrol create/update)
- Prolog, epilog, and ResumeProgram / other power-script env (not asserted here)
- squeue and sview column output (covered elsewhere; not this file)

Requires: running cluster; partition tests need auto config and at least one
node (see part_for_update fixture).

Jobs that only need RUNNING for scontrol show use a short sleep in --wrap to
limit wall time; env checks wait for COMPLETED on tiny scripts.
"""

import os
import re

import atf
import pytest

# Allowed token sets for display
JOB_OVERSUBSCRIBE_TOKENS = {"NO", "YES", "OK"}
JOB_EXCLUSIVE_TOKENS = {
    "NO",
    "NODE",
    "USER",
    "MCS",
    "TOPO",
}
PART_EXCLUSIVE_TOKENS = {
    "NO",
    "NODE",
    "USER",
    "TOPO",
}


def _part_oversubscribe_display_valid(value):
    """True if value matches scontrol show partition / sinfo OverSubscribe display."""
    if value == "NO":
        return True
    if isinstance(value, str):
        return bool(re.fullmatch(r"(FORCE|YES):[0-9]+", value))
    return False


test_name = os.path.splitext(os.path.basename(__file__))[0]


@pytest.fixture(scope="module", autouse=True)
def setup():
    atf.require_slurm_running()


@pytest.fixture(scope="function", autouse=True)
def cancel_jobs(setup):
    yield
    atf.cancel_all_jobs()


def test_scontrol_show_job_oversubscribe_exclusive(setup):
    """scontrol show job must show OverSubscribe= and Exclusive= with valid tokens."""
    job_id = atf.submit_job_sbatch('--wrap "sleep 10"', fatal=True)
    atf.wait_for_job_state(job_id, "RUNNING", fatal=True)

    oversubscribe = atf.get_job_parameter(
        job_id, "OverSubscribe", default=None, quiet=True
    )
    exclusive = atf.get_job_parameter(job_id, "Exclusive", default=None, quiet=True)

    assert oversubscribe is not None, "scontrol show job should include OverSubscribe="
    assert exclusive is not None, "scontrol show job should include Exclusive="
    assert (
        oversubscribe in JOB_OVERSUBSCRIBE_TOKENS
    ), f"OverSubscribe must be one of {JOB_OVERSUBSCRIBE_TOKENS}, got {oversubscribe!r}"
    assert (
        exclusive in JOB_EXCLUSIVE_TOKENS
    ), f"Exclusive must be one of {JOB_EXCLUSIVE_TOKENS}, got {exclusive!r}"

    atf.cancel_jobs([job_id], fatal=True)


def _submit_wait_show(sbatch_extra_args):
    """Submit job with extra sbatch args, wait RUNNING, return (job_id, oversubscribe, exclusive)."""
    job_id = atf.submit_job_sbatch(f'{sbatch_extra_args} --wrap "sleep 10"', fatal=True)
    atf.wait_for_job_state(job_id, "RUNNING", fatal=True)
    oversubscribe = atf.get_job_parameter(
        job_id, "OverSubscribe", default=None, quiet=True
    )
    exclusive = atf.get_job_parameter(job_id, "Exclusive", default=None, quiet=True)
    return job_id, oversubscribe, exclusive


def _submit_wait_show_part(part_name, sbatch_extra_args):
    """Submit job to partition with extra sbatch args, wait RUNNING, return (job_id, oversubscribe, exclusive)."""
    job_id = atf.submit_job_sbatch(
        f'-p {part_name} {sbatch_extra_args} --wrap "sleep 10"', fatal=True
    )
    atf.wait_for_job_state(job_id, "RUNNING", fatal=True)
    oversubscribe = atf.get_job_parameter(
        job_id, "OverSubscribe", default=None, quiet=True
    )
    exclusive = atf.get_job_parameter(job_id, "Exclusive", default=None, quiet=True)
    return job_id, oversubscribe, exclusive


def test_sbatch_oversubscribe_shows_oversubscribe(part_for_update):
    """Submit --oversubscribe to OverSubscribe=YES:4 partition: YES, Exclusive= NO.

    --oversubscribe sets details->share_res = 1, which always maps to
    JOB_OVERSUBSCRIBE_YES (never OK). Use a partition that permits
    oversubscription so _resolve_shared_status() does not reset share_res to 0.
    """
    part_name = part_for_update
    atf.run_command(
        f"scontrol update PartitionName={part_name} OverSubscribe=YES:4",
        fatal=True,
        user=atf.properties["slurm-user"],
    )
    job_id, oversubscribe, exclusive = _submit_wait_show_part(
        part_name, "--oversubscribe"
    )
    assert (
        oversubscribe == "YES"
    ), f"--oversubscribe should show OverSubscribe= YES, got {oversubscribe!r}"
    assert exclusive == "NO", f"Expected Exclusive= NO, got {exclusive!r}"
    atf.cancel_jobs([job_id], fatal=True)


def test_sbatch_exclusive_shows_exclusive_node(setup):
    """Submit with --exclusive; OverSubscribe= NO, Exclusive= NODE."""
    job_id, oversubscribe, exclusive = _submit_wait_show("--exclusive")
    assert (
        oversubscribe == "NO"
    ), f"--exclusive should show OverSubscribe= NO, got {oversubscribe!r}"
    assert (
        exclusive == "NODE"
    ), f"--exclusive should show Exclusive= NODE, got {exclusive!r}"
    atf.cancel_jobs([job_id], fatal=True)


def test_sbatch_exclusive_oversubscribe_same_as_oversubscribe(part_for_update):
    """Submit --exclusive=oversubscribe to OverSubscribe=YES:4 partition; same as --oversubscribe."""
    part_name = part_for_update
    atf.run_command(
        f"scontrol update PartitionName={part_name} OverSubscribe=YES:4",
        fatal=True,
        user=atf.properties["slurm-user"],
    )
    job_id, oversubscribe, exclusive = _submit_wait_show_part(
        part_name, "--exclusive=oversubscribe"
    )
    assert (
        oversubscribe == "YES"
    ), f"--exclusive=oversubscribe should show OverSubscribe= YES, got {oversubscribe!r}"
    assert exclusive == "NO", f"Expected Exclusive= NO, got {exclusive!r}"
    atf.cancel_jobs([job_id], fatal=True)


def test_sbatch_exclusive_user_shows_user(setup):
    """Submit with --exclusive=user; scontrol show job reports Exclusive=USER."""
    if atf.get_config_parameter("SelectType") == "select/linear":
        pytest.skip(
            "select/linear's _resolve_shared_status() adds WHOLE_NODE_REQUIRED "
            "to user-exclusive jobs since linear can only allocate whole nodes; "
            "the helper truthfully returns Exclusive=NODE."
        )
    job_id, oversubscribe, exclusive = _submit_wait_show("--exclusive=user")
    assert (
        exclusive == "USER"
    ), f"--exclusive=user should show Exclusive= USER, got {exclusive!r}"
    atf.cancel_jobs([job_id], fatal=True)


def test_sbatch_exclusive_mcs_shows_mcs(setup):
    """Submit with --exclusive=mcs; scontrol show job reports Exclusive=MCS."""
    if atf.get_config_parameter("SelectType") == "select/linear":
        pytest.skip(
            "select/linear's _resolve_shared_status() adds WHOLE_NODE_REQUIRED "
            "to mcs-exclusive jobs since linear can only allocate whole nodes; "
            "the helper truthfully returns Exclusive=NODE."
        )
    job_id, oversubscribe, exclusive = _submit_wait_show("--exclusive=mcs")
    assert (
        exclusive == "MCS"
    ), f"--exclusive=mcs should show Exclusive= MCS, got {exclusive!r}"
    atf.cancel_jobs([job_id], fatal=True)


def test_sbatch_exclusive_topo_shows_topo(setup):
    """Submit with --exclusive=topo; scontrol show job reports Exclusive=TOPO."""
    job_id, oversubscribe, exclusive = _submit_wait_show("--exclusive=topo")
    assert (
        exclusive == "TOPO"
    ), f"--exclusive=topo should show Exclusive= TOPO, got {exclusive!r}"
    atf.cancel_jobs([job_id], fatal=True)


def test_sbatch_exclusive_user_env_in_job_script(setup):
    """Submit with --exclusive=user; job script sees SLURM_JOB_EXCLUSIVE= USER."""
    if atf.get_config_parameter("SelectType") == "select/linear":
        pytest.skip(
            "select/linear's _resolve_shared_status() adds WHOLE_NODE_REQUIRED "
            "to user-exclusive jobs since linear can only allocate whole nodes; "
            "the helper truthfully sets SLURM_JOB_EXCLUSIVE=NODE."
        )
    out_path = atf.module_tmp_path / "exclusive_user_env.out"
    script = atf.module_tmp_path / "exclusive_user_env.sh"
    atf.make_bash_script(
        script,
        'echo "SLURM_JOB_EXCLUSIVE=${SLURM_JOB_EXCLUSIVE:-<unset>}"',
    )
    job_id = atf.submit_job_sbatch(
        f"--exclusive=user --output={out_path} {script}",
        fatal=True,
    )
    atf.wait_for_job_state(job_id, "COMPLETED", fatal=True, timeout=30)
    assert out_path.is_file(), "Job stdout file not found"
    content = out_path.read_text()
    assert "SLURM_JOB_EXCLUSIVE=" in content
    for line in content.splitlines():
        if line.startswith("SLURM_JOB_EXCLUSIVE="):
            val = line.split("=", 1)[1].strip()
            assert (
                val == "USER"
            ), f"--exclusive=user job should see SLURM_JOB_EXCLUSIVE= USER, got {val!r}"
            break


def test_sbatch_exclusive_node_env_in_job_script(setup):
    """Submit with --exclusive; job script sees SLURM_JOB_EXCLUSIVE= NODE."""
    out_path = atf.module_tmp_path / "exclusive_node_env.out"
    script = atf.module_tmp_path / "exclusive_node_env.sh"
    atf.make_bash_script(
        script,
        'echo "SLURM_JOB_EXCLUSIVE=${SLURM_JOB_EXCLUSIVE:-<unset>}"',
    )
    job_id = atf.submit_job_sbatch(
        f"--exclusive --output={out_path} {script}",
        fatal=True,
    )
    atf.wait_for_job_state(job_id, "COMPLETED", fatal=True, timeout=30)
    assert out_path.is_file(), "Job stdout file not found"
    content = out_path.read_text()
    assert "SLURM_JOB_EXCLUSIVE=" in content
    for line in content.splitlines():
        if line.startswith("SLURM_JOB_EXCLUSIVE="):
            val = line.split("=", 1)[1].strip()
            assert (
                val == "NODE"
            ), f"--exclusive job should see SLURM_JOB_EXCLUSIVE= NODE, got {val!r}"
            break


def test_sbatch_exclusive_mcs_env_in_job_script(setup):
    """Submit with --exclusive=mcs; job script sees SLURM_JOB_EXCLUSIVE= MCS."""
    if atf.get_config_parameter("SelectType") == "select/linear":
        pytest.skip(
            "select/linear's _resolve_shared_status() adds WHOLE_NODE_REQUIRED "
            "to mcs-exclusive jobs since linear can only allocate whole nodes; "
            "the helper truthfully sets SLURM_JOB_EXCLUSIVE=NODE."
        )
    out_path = atf.module_tmp_path / "exclusive_mcs_env.out"
    script = atf.module_tmp_path / "exclusive_mcs_env.sh"
    atf.make_bash_script(
        script,
        'echo "SLURM_JOB_EXCLUSIVE=${SLURM_JOB_EXCLUSIVE:-<unset>}"',
    )
    job_id = atf.submit_job_sbatch(
        f"--exclusive=mcs --output={out_path} {script}",
        fatal=True,
    )
    atf.wait_for_job_state(job_id, "COMPLETED", fatal=True, timeout=30)
    assert out_path.is_file(), "Job stdout file not found"
    content = out_path.read_text()
    assert "SLURM_JOB_EXCLUSIVE=" in content
    for line in content.splitlines():
        if line.startswith("SLURM_JOB_EXCLUSIVE="):
            val = line.split("=", 1)[1].strip()
            assert (
                val == "MCS"
            ), f"--exclusive=mcs job should see SLURM_JOB_EXCLUSIVE= MCS, got {val!r}"
            break


def test_sbatch_exclusive_topo_env_in_job_script(setup):
    """Submit with --exclusive=topo; job script sees SLURM_JOB_EXCLUSIVE= TOPO."""
    out_path = atf.module_tmp_path / "exclusive_topo_env.out"
    script = atf.module_tmp_path / "exclusive_topo_env.sh"
    atf.make_bash_script(
        script,
        'echo "SLURM_JOB_EXCLUSIVE=${SLURM_JOB_EXCLUSIVE:-<unset>}"',
    )
    job_id = atf.submit_job_sbatch(
        f"--exclusive=topo --output={out_path} {script}",
        fatal=True,
    )
    atf.wait_for_job_state(job_id, "COMPLETED", fatal=True, timeout=30)
    assert out_path.is_file(), "Job stdout file not found"
    content = out_path.read_text()
    assert "SLURM_JOB_EXCLUSIVE=" in content
    for line in content.splitlines():
        if line.startswith("SLURM_JOB_EXCLUSIVE="):
            val = line.split("=", 1)[1].strip()
            assert (
                val == "TOPO"
            ), f"--exclusive=topo job should see SLURM_JOB_EXCLUSIVE= TOPO, got {val!r}"
            break


def test_oversubscribe_job_sees_slurm_job_oversubscribe_yes(part_for_update):
    """Submit --oversubscribe to OverSubscribe=YES:4 partition; env var is YES.

    SLURM_JOB_OVERSUBSCRIBE comes from job_oversubscribe_string() applied to the
    job's share_res. --oversubscribe pins share_res = 1 -> JOB_OVERSUBSCRIBE_YES;
    the OK token only appears for default jobs whose share_res stays NO_VAL8 on a
    partition that allows oversubscription (see
    test_partition_oversubscribe_yes_default_job_inherits).
    """
    part_name = part_for_update
    atf.run_command(
        f"scontrol update PartitionName={part_name} OverSubscribe=YES:4",
        fatal=True,
        user=atf.properties["slurm-user"],
    )
    out_path = atf.module_tmp_path / "oversubscribe_env.out"
    script = atf.module_tmp_path / "oversubscribe_env.sh"
    atf.make_bash_script(
        script,
        'echo "SLURM_JOB_OVERSUBSCRIBE=${SLURM_JOB_OVERSUBSCRIBE:-<unset>}"',
    )
    job_id = atf.submit_job_sbatch(
        f"-p {part_name} --oversubscribe --output={out_path} {script}",
        fatal=True,
    )
    atf.wait_for_job_state(job_id, "COMPLETED", fatal=True, timeout=30)
    assert out_path.is_file(), "Job stdout file not found"
    content = out_path.read_text()
    assert "SLURM_JOB_OVERSUBSCRIBE=" in content
    for line in content.splitlines():
        if line.startswith("SLURM_JOB_OVERSUBSCRIBE="):
            val = line.split("=", 1)[1].strip()
            assert (
                val == "YES"
            ), f"--oversubscribe job should see SLURM_JOB_OVERSUBSCRIBE= YES, got {val!r}"
            break


# --- Partition × job interaction (partition settings vs job flags) ---


def test_partition_exclusive_node_default_job_inherits(part_for_update):
    """Partition Exclusive=NODE, default job (no flags): job shows Exclusive=NODE, OverSubscribe=NO."""
    part_name = part_for_update
    atf.run_command(
        f"scontrol update PartitionName={part_name} Exclusive=NODE",
        fatal=True,
        user=atf.properties["slurm-user"],
    )
    job_id, oversubscribe, exclusive = _submit_wait_show_part(part_name, "")
    assert (
        oversubscribe == "NO"
    ), f"Partition NO oversubscribe: expected NO, got {oversubscribe!r}"
    assert (
        exclusive == "NODE"
    ), f"Partition Exclusive=NODE: job should show NODE, got {exclusive!r}"
    atf.cancel_jobs([job_id], fatal=True)


def test_partition_oversubscribe_yes_default_job_inherits(part_for_update):
    """Partition OverSubscribe=YES, default job: job shows OverSubscribe=OK, Exclusive=NO."""
    if atf.get_config_parameter("SelectType") == "select/linear":
        pytest.skip(
            "select/linear's _resolve_shared_status() resets share_res to 0 for "
            "default jobs, so the helper truthfully returns OverSubscribe=NO "
            "regardless of partition policy."
        )
    part_name = part_for_update
    atf.run_command(
        f"scontrol update PartitionName={part_name} OverSubscribe=YES",
        fatal=True,
        user=atf.properties["slurm-user"],
    )
    job_id, oversubscribe, exclusive = _submit_wait_show_part(part_name, "")
    assert (
        oversubscribe == "OK"
    ), f"Partition OverSubscribe=YES: default job should show OverSubscribe=OK, got {oversubscribe!r}"
    assert exclusive == "NO", f"Expected Exclusive=NO, got {exclusive!r}"
    atf.cancel_jobs([job_id], fatal=True)


def test_partition_exclusive_user_default_job_inherits(part_for_update):
    """Partition Exclusive=USER, default job: job shows Exclusive=USER."""
    if atf.get_config_parameter("SelectType") == "select/linear":
        pytest.skip(
            "select/linear's _resolve_shared_status() sets WHOLE_NODE_REQUIRED "
            "for default jobs, so the helper truthfully returns Exclusive=NODE "
            "regardless of partition policy."
        )
    part_name = part_for_update
    atf.run_command(
        f"scontrol update PartitionName={part_name} Exclusive=USER",
        fatal=True,
        user=atf.properties["slurm-user"],
    )
    job_id, oversubscribe, exclusive = _submit_wait_show_part(part_name, "")
    assert (
        exclusive == "USER"
    ), f"Partition Exclusive=USER: job should show USER, got {exclusive!r}"
    atf.cancel_jobs([job_id], fatal=True)


def test_partition_exclusive_node_job_oversubscribe(part_for_update):
    """Partition Exclusive=NODE with job --oversubscribe: fields stay valid tokens.

    Partition may keep OverSubscribe=NO (job request loses) or allow YES/OK with
    Exclusive=NO when the scheduler honors oversubscribe on that partition.
    """
    part_name = part_for_update
    atf.run_command(
        f"scontrol update PartitionName={part_name} Exclusive=NODE",
        fatal=True,
        user=atf.properties["slurm-user"],
    )
    job_id, oversubscribe, exclusive = _submit_wait_show_part(
        part_name, "--oversubscribe"
    )
    assert (
        oversubscribe in JOB_OVERSUBSCRIBE_TOKENS
    ), f"OverSubscribe must be one of {JOB_OVERSUBSCRIBE_TOKENS}, got {oversubscribe!r}"
    assert (
        exclusive in JOB_EXCLUSIVE_TOKENS
    ), f"Exclusive must be one of {JOB_EXCLUSIVE_TOKENS}, got {exclusive!r}"
    if oversubscribe in ("YES", "OK"):
        assert (
            exclusive == "NO"
        ), f"When OverSubscribe=YES/OK, Exclusive should be NO, got {exclusive!r}"
    atf.cancel_jobs([job_id], fatal=True)


def test_partition_oversubscribe_yes_job_exclusive_wins(part_for_update):
    """Partition OverSubscribe=YES, job --exclusive: job wins → OverSubscribe=NO, Exclusive=NODE."""
    part_name = part_for_update
    atf.run_command(
        f"scontrol update PartitionName={part_name} OverSubscribe=YES",
        fatal=True,
        user=atf.properties["slurm-user"],
    )
    job_id, oversubscribe, exclusive = _submit_wait_show_part(part_name, "--exclusive")
    assert (
        oversubscribe == "NO"
    ), f"Job --exclusive should override partition oversubscribe: NO, got {oversubscribe!r}"
    assert (
        exclusive == "NODE"
    ), f"Job --exclusive should show Exclusive= NODE, got {exclusive!r}"
    atf.cancel_jobs([job_id], fatal=True)


def test_exclusive_topo_on_oversubscribe_exclusive_partition(part_for_update):
    """Job --exclusive=topo on OverSubscribe=EXCLUSIVE partition: show Exclusive=TOPO while RUNNING.

    Regression: display must not drop topo exclusive when the partition is
    effectively exclusive-only.
    """
    part_name = part_for_update
    atf.run_command(
        f"scontrol update PartitionName={part_name} OverSubscribe=EXCLUSIVE",
        fatal=True,
        user=atf.properties["slurm-user"],
    )
    job_id, oversubscribe, exclusive = _submit_wait_show_part(
        part_name, "--exclusive=topo"
    )
    assert (
        exclusive == "TOPO"
    ), f"Job --exclusive=topo on OverSubscribe=EXCLUSIVE partition must show Exclusive=TOPO when running, got {exclusive!r}"
    assert (
        oversubscribe == "NO"
    ), f"Exclusive=topo job should show OverSubscribe=NO, got {oversubscribe!r}"
    atf.cancel_jobs([job_id], fatal=True)


def test_scontrol_show_part_oversubscribe_exclusive():
    """scontrol show part must show OverSubscribe= and Exclusive= with valid tokens."""
    partitions = atf.get_partitions()
    assert partitions, "No partitions found"

    for part_name, part_dict in partitions.items():
        over_subscribe = part_dict.get("OverSubscribe")
        exclusive = part_dict.get("Exclusive")

        assert (
            over_subscribe is not None
        ), f"Partition {part_name}: scontrol show part should include OverSubscribe="
        assert (
            exclusive is not None
        ), f"Partition {part_name}: scontrol show part should include Exclusive="
        assert _part_oversubscribe_display_valid(over_subscribe), (
            f"Partition {part_name}: OverSubscribe must be NO, FORCE:n, or "
            f"YES:n, got {over_subscribe!r}"
        )
        assert exclusive in PART_EXCLUSIVE_TOKENS, (
            f"Partition {part_name}: Exclusive must be one of "
            f"{PART_EXCLUSIVE_TOKENS}, got {exclusive!r}"
        )


@pytest.fixture(scope="module")
def part_for_update(setup):
    """Create a partition for update_part tests; remove on teardown."""
    atf.require_auto_config("Needs to create/delete partition")
    atf.require_nodes(1, [("CPUs", 1)])

    part_name = f"{test_name}_part"
    nodes = list(atf.get_nodes().keys())
    atf.run_command(
        f"scontrol create PartitionName={part_name} Nodes={nodes[0]} "
        "OverSubscribe=NO",
        fatal=True,
        user=atf.properties["slurm-user"],
    )

    yield part_name

    atf.run_command(
        f"scontrol delete PartitionName={part_name}",
        user=atf.properties["slurm-user"],
        quiet=True,
    )


def test_scontrol_update_part_exclusive_no(part_for_update):
    """scontrol update part Exclusive=NO sets Exclusive to NO (partition show)."""
    part_name = part_for_update
    atf.run_command(
        f"scontrol update PartitionName={part_name} Exclusive=NO",
        fatal=True,
        user=atf.properties["slurm-user"],
    )
    exclusive = atf.get_partition_parameter(part_name, "Exclusive")
    assert (
        exclusive == "NO"
    ), f"After Exclusive=NO, Exclusive should be NO, got {exclusive!r}"


def test_scontrol_update_part_exclusive_node(part_for_update):
    """scontrol update part Exclusive=NODE sets Exclusive to NODE."""
    part_name = part_for_update
    atf.run_command(
        f"scontrol update PartitionName={part_name} Exclusive=NODE",
        fatal=True,
        user=atf.properties["slurm-user"],
    )
    exclusive = atf.get_partition_parameter(part_name, "Exclusive")
    assert (
        exclusive == "NODE"
    ), f"After Exclusive=NODE, Exclusive should be NODE, got {exclusive!r}"


def test_scontrol_update_part_exclusive_user(part_for_update):
    """scontrol update part Exclusive=USER sets Exclusive to USER."""
    part_name = part_for_update
    atf.run_command(
        f"scontrol update PartitionName={part_name} Exclusive=USER",
        fatal=True,
        user=atf.properties["slurm-user"],
    )
    exclusive = atf.get_partition_parameter(part_name, "Exclusive")
    assert (
        exclusive == "USER"
    ), f"After Exclusive=USER, Exclusive should be USER, got {exclusive!r}"


def test_scontrol_update_part_exclusive_topo(part_for_update):
    """scontrol update part Exclusive=TOPO sets Exclusive to TOPO."""
    part_name = part_for_update
    atf.run_command(
        f"scontrol update PartitionName={part_name} Exclusive=TOPO",
        fatal=True,
        user=atf.properties["slurm-user"],
    )
    exclusive = atf.get_partition_parameter(part_name, "Exclusive")
    assert (
        exclusive == "TOPO"
    ), f"After Exclusive=TOPO, Exclusive should be TOPO, got {exclusive!r}"


def test_scontrol_update_part_oversubscribe_force_shows_force_n(part_for_update):
    """scontrol update part OverSubscribe=FORCE; show partition OverSubscribe=FORCE:n."""
    part_name = part_for_update
    atf.run_command(
        f"scontrol update PartitionName={part_name} OverSubscribe=FORCE",
        fatal=True,
        user=atf.properties["slurm-user"],
    )
    over_subscribe = atf.get_partition_parameter(part_name, "OverSubscribe")
    assert isinstance(over_subscribe, str) and re.fullmatch(
        r"FORCE:[0-9]+", over_subscribe
    ), f"OverSubscribe=FORCE should display as FORCE:n, got {over_subscribe!r}"


def test_scontrol_update_part_oversubscribe_invalid_error(part_for_update):
    """scontrol update part with invalid OverSubscribe reports acceptable values."""
    part_name = part_for_update
    result = atf.run_command(
        f"scontrol update PartitionName={part_name} OverSubscribe=INVALID",
        user=atf.properties["slurm-user"],
        fatal=False,
    )
    assert result["exit_code"] != 0, "Invalid OverSubscribe should cause non-zero exit"
    stderr = result.get("stderr", "") or result.get("stdout", "")
    # Error message should mention acceptable values (NO, YES, FORCE, EXCLUSIVE)
    assert re.search(
        r"NO|YES|FORCE|EXCLUSIVE",
        stderr,
        re.IGNORECASE,
    ), f"Error message should list acceptable OverSubscribe values, got: {stderr!r}"


def test_job_env_slurm_job_oversubscribe_exclusive(setup):
    """Job script must see SLURM_JOB_OVERSUBSCRIBE and SLURM_JOB_EXCLUSIVE when set by Slurm."""
    out_path = atf.module_tmp_path / "env_check.out"
    script = atf.module_tmp_path / "env_check.sh"
    atf.make_bash_script(
        script,
        'echo "SLURM_JOB_OVERSUBSCRIBE=${SLURM_JOB_OVERSUBSCRIBE:-<unset>}"\n'
        'echo "SLURM_JOB_EXCLUSIVE=${SLURM_JOB_EXCLUSIVE:-<unset>}"',
    )

    job_id = atf.submit_job_sbatch(
        f"--output={out_path} {script}",
        fatal=True,
    )
    atf.wait_for_job_state(job_id, "COMPLETED", fatal=True, timeout=30)

    assert out_path.is_file(), "Job stdout file not found to check env vars"

    content = out_path.read_text()

    assert (
        "SLURM_JOB_OVERSUBSCRIBE=" in content
    ), "Job output should contain SLURM_JOB_OVERSUBSCRIBE= line"
    assert (
        "SLURM_JOB_EXCLUSIVE=" in content
    ), "Job output should contain SLURM_JOB_EXCLUSIVE= line"

    for line in content.splitlines():
        if line.startswith("SLURM_JOB_OVERSUBSCRIBE="):
            val = line.split("=", 1)[1].strip()
            assert (
                val in JOB_OVERSUBSCRIBE_TOKENS
            ), f"SLURM_JOB_OVERSUBSCRIBE should be one of {JOB_OVERSUBSCRIBE_TOKENS}, got {val!r}"
        elif line.startswith("SLURM_JOB_EXCLUSIVE="):
            val = line.split("=", 1)[1].strip()
            assert (
                val in JOB_EXCLUSIVE_TOKENS
            ), f"SLURM_JOB_EXCLUSIVE should be one of {JOB_EXCLUSIVE_TOKENS}, got {val!r}"
