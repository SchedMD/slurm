############################################################################
# Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
############################################################################
"""Test scontrol update of job TRES fields."""

import re

import pytest

import atf

LICENSE = "test_license"
LICENSE_TOTAL = 4
INVALID_TRES = "bogus/x:1"

CLEAR_XFAIL = pytest.mark.xfail(
    atf.get_version("sbin/slurmctld") < (26, 11),
    reason="Issue 51363: Clearing a TRES field on a job that still requests GRES stored an empty string before 26.11",
)
PROPAGATION_XFAIL = pytest.mark.xfail(
    atf.get_version("sbin/slurmctld") < (26, 11),
    reason="Issue 51388: A GRES update of a hetjob or job array only reached the first component or task before 26.11",
)


@pytest.fixture(scope="module", autouse=True)
def setup():
    atf.require_accounting()
    atf.require_config_parameter("SelectType", "select/cons_tres")
    atf.require_config_parameter("SelectTypeParameters", "CR_CPU")
    # slurmctld rejects a het job submission without sched/backfill
    atf.require_config_parameter("SchedulerType", "sched/backfill")
    atf.require_config_parameter_includes("Licenses", f"{LICENSE}:{LICENSE_TOTAL}")
    atf.require_config_parameter_includes("AccountingStorageTRES", f"license/{LICENSE}")
    atf.require_tty(0)
    atf.require_config_parameter("Name", {"gpu": {"File": "/dev/tty0"}}, source="gres")
    atf.require_config_parameter_includes("GresTypes", "gpu")
    atf.require_nodes(1, [("Gres", "gpu:1")])
    atf.require_slurm_running()


def get_field(job_id, field):
    return atf.get_job_parameter(job_id, field, fatal=True, quiet=True)


def get_field_per_record(job_id, field):
    jobs = atf.get_jobs(job_id, fatal=True, quiet=True)
    return [job.get(field) for job in jobs.values()]


def submit_held(args="", ntasks=1):
    """Submit a held batch job and return its job id."""
    return atf.submit_job_sbatch(f"--hold -n{ntasks} {args} --wrap='true'", fatal=True)


def update_job(job_id, update):
    atf.run_command(f"scontrol update JobId={job_id} {update}", fatal=True, quiet=True)


# (field, sbatch args, kept field)
# Each case's sbatch args also request a GPU through kept_field, which must
# survive the update.
GRES_FIELDS = [
    ("CpusPerTres", "--gpus=1 --cpus-per-gpu=1", "TresPerJob"),
    ("MemPerTres", "--gpus=1 --mem-per-gpu=100", "TresPerJob"),
    ("TresPerJob", "--gpus=1 --gpus-per-node=1", "TresPerNode"),
    ("TresPerNode", "--gpus=1 --gpus-per-node=1", "TresPerJob"),
    (
        "TresPerSocket",
        "--gpus=1 --gpus-per-socket=1 --sockets-per-node=1",
        "TresPerJob",
    ),
    ("TresPerTask", "--gpus=1 --gpus-per-task=1", "TresPerJob"),
]
GRES_FIELD_CASES = [pytest.param(f, a, k, id=f) for f, a, k in GRES_FIELDS]


@CLEAR_XFAIL
@pytest.mark.parametrize("field, sbatch_args, kept_field", GRES_FIELD_CASES)
def test_clear_with_other_gres(field, sbatch_args, kept_field):
    """Clearing a TRES field resets it to NULL and leaves the other GRES alone."""
    job_id = submit_held(sbatch_args)

    kept_before = get_field(job_id, kept_field)
    assert kept_before, f"{kept_field} should be set before the clear"
    assert get_field(job_id, field), f"{field} should be set before the clear"

    update_job(job_id, f"{field}=")

    assert get_field(job_id, field) is None, f"{field} should be NULL once cleared"
    assert (
        get_field(job_id, kept_field) == kept_before
    ), f"Clearing {field} should not change {kept_field}"


def test_gres_sets_tres_per_node():
    """Gres= sets TresPerNode, as scontrol.1 documents for job updates."""
    job_id = submit_held()

    update_job(job_id, "Gres=gpu:1")

    assert (
        get_field(job_id, "TresPerNode") == "gres/gpu:1"
    ), "Gres=gpu:1 should set TresPerNode=gres/gpu:1"


@CLEAR_XFAIL
@pytest.mark.xfail(
    atf.get_version("bin/scontrol") < (26, 11),
    reason="Issue 51459: scontrol update Gres= stored gres/ in TresPerNode before 26.11",
)
def test_gres_clear_with_other_gres():
    """An empty Gres= clears TresPerNode and leaves the other GRES alone."""
    job_id = submit_held("--gpus=1 --gpus-per-node=1")
    kept_before = get_field(job_id, "TresPerJob")
    assert kept_before, "TresPerJob should be set before the clear"

    update_job(job_id, "Gres=")

    assert get_field(job_id, "TresPerNode") is None, "Gres= should clear TresPerNode"
    assert (
        get_field(job_id, "TresPerJob") == kept_before
    ), "Clearing TresPerNode should not change TresPerJob"


@pytest.mark.xfail(
    atf.get_version("sbin/slurmctld") < (26, 11),
    reason="Issue 51363: An invalid TRES update of a job that still requests GRES was stored before 26.11",
)
@pytest.mark.parametrize("field, sbatch_args, kept_field", GRES_FIELD_CASES)
def test_reject_invalid_with_other_gres(field, sbatch_args, kept_field):
    """An invalid TRES is rejected and not stored on a job that requests GRES."""
    job_id = submit_held(sbatch_args)

    field_before = get_field(job_id, field)
    kept_before = get_field(job_id, kept_field)

    assert (
        atf.run_command_exit(
            f"scontrol update JobId={job_id} {field}={INVALID_TRES}",
            xfail=True,
        )
        != 0
    ), f"scontrol update should reject {field}={INVALID_TRES}"

    assert (
        get_field(job_id, field) == field_before
    ), f"A rejected update should not change {field}"
    assert (
        get_field(job_id, kept_field) == kept_before
    ), f"A rejected update should not change {kept_field}"


@pytest.mark.parametrize(
    "update",
    [
        pytest.param(f"TresPerTask={INVALID_TRES}", id="TresPerTask"),
        pytest.param("TresPerTask=cpu=1 TresBind=bogus_value", id="TresBind"),
        pytest.param("TresPerTask=cpu=1 TresFreq=bogus", id="TresFreq"),
    ],
)
def test_reject_invalid_without_gres(update):
    """A rejected update of a job without GRES stores none of its TRES fields."""
    job_id = submit_held()
    fields = ["TresPerTask", "TresBind", "TresFreq"]
    before = [get_field(job_id, f) for f in fields]

    assert (
        atf.run_command_exit(f"scontrol update JobId={job_id} {update}", xfail=True)
        != 0
    ), f"scontrol update should reject {update}"

    assert [
        get_field(job_id, f) for f in fields
    ] == before, f"A rejected {update} should not change {fields}"


@pytest.mark.xfail(
    atf.get_version("sbin/slurmctld") < (26, 11),
    reason="Issue 51363: A rejected TRES update of a job that requests GRES still applied its GRES fields before 26.11",
)
@pytest.mark.parametrize(
    "update",
    [
        pytest.param("TresPerTask=gres/gpu:1 TresBind=bogus_value", id="TresBind"),
        pytest.param("TresPerTask=gres/gpu:1 TresFreq=bogus", id="TresFreq"),
    ],
)
def test_reject_invalid_with_gres_update(update):
    """A rejected update of a job with GRES stores none of its TRES fields."""
    job_id = submit_held("--gpus=1")
    fields = ["TresPerTask", "TresBind", "TresFreq", "TresPerJob"]
    before = [get_field(job_id, f) for f in fields]

    assert (
        atf.run_command_exit(f"scontrol update JobId={job_id} {update}", xfail=True)
        != 0
    ), f"scontrol update should reject {update}"

    assert [
        get_field(job_id, f) for f in fields
    ] == before, f"A rejected {update} should not change {fields}"


def test_reject_invalid_at_submit():
    """A job submission with an invalid TRES is rejected."""
    assert (
        atf.submit_job_sbatch(
            f"--hold --tres-per-task={INVALID_TRES} --wrap='true'", xfail=True
        )
        == 0
    ), f"sbatch should reject --tres-per-task={INVALID_TRES}"


def submit_hetjob(args=""):
    """Submit a held 2-component hetjob with args on both components."""
    return submit_held(f"{args} : --hold -n1 {args}")


def submit_split_array(args=""):
    """Submit a held 2-task array and split task 1 into its own record."""
    job_id = submit_held(f"--array=0-1 {args}")
    update_job(f"{job_id}_1", "Comment=split")
    return job_id


def update_record(submit, job_id, index, update):
    """Update the record at index in get_field_per_record() of a hetjob or array."""
    sep = "+" if submit is submit_hetjob else "_"
    update_job(f"{job_id}{sep}{index}", update)


@pytest.mark.parametrize(
    "field, value",
    [
        pytest.param("TresBind", "gres/gpu:closest", id="TresBind"),
        pytest.param("TresFreq", "gpu:1000", id="TresFreq"),
        pytest.param("TresPerTask", "cpu=1", id="TresPerTask"),
        pytest.param(
            "TresPerNode", "gres/gpu:1", id="TresPerNode", marks=PROPAGATION_XFAIL
        ),
    ],
)
@pytest.mark.parametrize(
    "submit", [submit_hetjob, submit_split_array], ids=["hetjob", "array"]
)
def test_update_every_record(submit, field, value):
    """An update of the whole job sets the field on every component or task."""
    job_id = submit()

    update_job(job_id, f"{field}={value}")

    assert (
        get_field_per_record(job_id, field) == [value] * 2
    ), f"Every component or task should have {field}={value}"


@PROPAGATION_XFAIL
@pytest.mark.parametrize(
    "submit", [submit_hetjob, submit_split_array], ids=["hetjob", "array"]
)
def test_update_partially_set(submit):
    """The second record gets a value the first record already has."""
    job_id = submit()
    update_record(submit, job_id, 0, "TresPerNode=gres/gpu:1")
    assert get_field_per_record(job_id, "TresPerNode") == [
        "gres/gpu:1",
        None,
    ], "Only the first record should have TresPerNode before the update"

    update_job(job_id, "TresPerNode=gres/gpu:1")

    assert (
        get_field_per_record(job_id, "TresPerNode") == ["gres/gpu:1"] * 2
    ), "Every component or task should have TresPerNode=gres/gpu:1"


@PROPAGATION_XFAIL
@pytest.mark.parametrize(
    "submit", [submit_hetjob, submit_split_array], ids=["hetjob", "array"]
)
def test_update_second_differs(submit):
    """The second record's different value is replaced by the requested one."""
    job_id = submit("--gpus=1")
    update_record(submit, job_id, 1, "TresPerTask=gres/gpu:1")
    kept_before = get_field_per_record(job_id, "TresPerJob")
    assert get_field_per_record(job_id, "TresPerTask") == [
        None,
        "gres/gpu:1",
    ], "Only the second record should have TresPerTask before the update"

    update_job(job_id, "TresPerTask=cpu=1")

    assert (
        get_field_per_record(job_id, "TresPerTask") == ["cpu=1"] * 2
    ), "Every component or task should have TresPerTask=cpu=1"
    assert (
        get_field_per_record(job_id, "TresPerJob") == kept_before
    ), "Updating TresPerTask should not change TresPerJob"


@CLEAR_XFAIL
@PROPAGATION_XFAIL
@pytest.mark.parametrize(
    "submit", [submit_hetjob, submit_split_array], ids=["hetjob", "array"]
)
def test_clear_every_record(submit):
    """Clearing a field of the whole job clears it on every component or task."""
    job_id = submit("--gpus=1 --gpus-per-task=1")
    kept_before = get_field_per_record(job_id, "TresPerJob")
    assert all(
        get_field_per_record(job_id, "TresPerTask")
    ), "TresPerTask should be set before the clear"
    assert all(kept_before), "TresPerJob should be set before the clear"

    update_job(job_id, "TresPerTask=")

    assert (
        get_field_per_record(job_id, "TresPerTask") == [None] * 2
    ), "Every component or task should have TresPerTask cleared"
    assert (
        get_field_per_record(job_id, "TresPerJob") == kept_before
    ), "Clearing TresPerTask should not change TresPerJob"


@CLEAR_XFAIL
@PROPAGATION_XFAIL
@pytest.mark.parametrize(
    "submit", [submit_hetjob, submit_split_array], ids=["hetjob", "array"]
)
def test_clear_partially_set(submit):
    """Clearing reaches the second record when the first has nothing to clear."""
    job_id = submit("--gpus=1")
    update_record(submit, job_id, 1, "TresPerTask=gres/gpu:1")
    kept_before = get_field_per_record(job_id, "TresPerJob")
    assert get_field_per_record(job_id, "TresPerTask") == [
        None,
        "gres/gpu:1",
    ], "Only the second record should have TresPerTask before the update"

    update_job(job_id, "TresPerTask=")

    assert (
        get_field_per_record(job_id, "TresPerTask") == [None] * 2
    ), "Every component or task should have TresPerTask cleared"
    assert (
        get_field_per_record(job_id, "TresPerJob") == kept_before
    ), "Clearing TresPerTask should not change TresPerJob"


@pytest.mark.parametrize(
    "field, value",
    [
        pytest.param("TresPerTask", "cpu=1", id="TresPerTask"),
        pytest.param(
            "TresPerNode", "gres/gpu:1", id="TresPerNode", marks=PROPAGATION_XFAIL
        ),
    ],
)
def test_array_update_running_task(field, value):
    """An array update reaches its pending task and is rejected for its running one."""
    job_id = atf.submit_job_sbatch(
        "--hold -n1 --array=0-1 --wrap='sleep infinity'", fatal=True
    )
    atf.run_command(f"scontrol release {job_id}_0", fatal=True)
    running_id = atf.get_job_id_from_array_task(job_id, 0, fatal=True)
    atf.wait_for_job_state(running_id, "RUNNING", fatal=True)
    running_before = get_field(running_id, field)

    result = atf.run_command(
        f"scontrol update JobId={job_id} {field}={value}", xfail=True
    )
    assert (
        result["exit_code"] != 0
    ), f"scontrol update should fail for the running task of {job_id}"
    assert (
        "no longer pending" in result["stderr"]
    ), "scontrol update should fail because the running task is not pending"

    assert (
        get_field(running_id, field) == running_before
    ), f"The running task should keep its {field}"
    assert (
        get_field(job_id, field) == value
    ), f"The pending task should have {field}={value}"


def get_license_count(job_id):
    licenses = get_field(job_id, "Licenses")
    match = re.search(rf"{LICENSE}[:=](\d+)", licenses or "")
    assert match, f"{LICENSE} should be in Licenses={licenses}"
    return int(match.group(1))


@pytest.mark.parametrize(
    "value",
    [
        pytest.param(1, id="unchanged"),
        pytest.param(
            2,
            id="changed",
            marks=pytest.mark.xfail(
                atf.get_version("sbin/slurmctld") < (26, 11),
                reason="Issue 51404: A TresPerTask license update used the per-task count instead of per-task * ntasks before 26.11",
            ),
        ),
    ],
)
def test_license_tres_per_task(value):
    """A TresPerTask license update scales the license count by ntasks."""
    ntasks = 2
    job_id = submit_held(
        f"--overcommit --tres-per-task=license/{LICENSE}:1", ntasks=ntasks
    )
    assert (
        get_license_count(job_id) == ntasks
    ), f"{LICENSE} should be {ntasks} before the update"

    field_update = f"TresPerTask=license/{LICENSE}:{value}"
    update_job(job_id, field_update)

    expected = value * ntasks
    assert (
        get_license_count(job_id) == expected
    ), f"{field_update} on a -n{ntasks} job should set Licenses={LICENSE}:{expected}"


def test_license_tres_per_task_without_ntasks():
    """A TresPerTask license update of a job without a task count is not scaled."""
    job_id = atf.submit_job_sbatch(
        f"--hold --tres-per-task=license/{LICENSE}:1 --wrap='true'", fatal=True
    )
    assert get_license_count(job_id) == 1, f"{LICENSE} should be 1 before the update"

    update_job(job_id, f"TresPerTask=license/{LICENSE}:2")

    assert (
        get_license_count(job_id) == 2
    ), f"A job without a task count should have Licenses={LICENSE}:2"
