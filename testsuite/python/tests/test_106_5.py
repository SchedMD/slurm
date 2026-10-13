############################################################################
# Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
############################################################################
"""Test sbcast to a heterogeneous job with namespace/tmpfs."""

import pytest

import atf

ns_dir = "/tmp/test_106_5_private"
file_content = "sbcast_hetjob_tmpfs"


@pytest.fixture(scope="module", autouse=True)
def setup():
    atf.require_version(
        (25, 11),
        component="bin/sbcast",
        reason="Issue 50542: NamespaceType was added in 25.11",
    )
    atf.require_nodes(2)

    # Only backfill starts hetjobs
    atf.require_config_parameter("SchedulerType", "sched/backfill")
    atf.require_config_parameter_includes("SchedulerParameters", ("bf_interval", 1))

    atf.require_config_parameter("NamespaceType", "namespace/tmpfs")
    atf.require_config_parameter_includes("PrologFlags", "Contain")

    # BasePath must be unique per node, also with multiple slurmd
    atf.require_config_file(
        "job_container.conf",
        f"AutoBasePath=true\nBasePath=/tmp/%h_%n_base_path Dirs={ns_dir}\n",
    )

    # Assume the slurmd sees the same file system as us
    atf.run_command(f"mkdir -p {ns_dir}", fatal=True)

    atf.require_slurm_running()

    yield

    atf.run_command(f"rm -rf {ns_dir}", fatal=True)


@pytest.fixture(autouse=True)
def clean_ns_dir():
    atf.run_command(f"rm -rf {ns_dir}/*", fatal=True)


@pytest.fixture
def het_job_id():
    """Start a running hetjob with two components on different nodes."""

    job_id = atf.submit_job_sbatch(
        "-N1 --exclusive : -N1 --exclusive --wrap 'sleep infinity'", fatal=True
    )
    atf.wait_for_job_state(job_id, "RUNNING", fatal=True)

    jobs = atf.get_jobs(job_id, fatal=True)
    components = sorted(
        atf.range_to_list(jobs[job_id]["HetJobIdSet"]),
        key=lambda component_id: jobs[component_id]["HetJobOffset"],
    )
    nodes = [jobs[component_id]["NodeList"] for component_id in components]
    if nodes[0] == nodes[1]:
        pytest.fail(f"The components should be on different nodes, got {nodes}")

    # Steps wait for the prolog, so after them each namespace exists
    for het_group in (0, 1):
        atf.run_command(
            f"srun --jobid={job_id} --het-group={het_group} true", fatal=True
        )

    atf.run_command(f"echo {file_content} > file", fatal=True)

    return job_id


def test_sbcast_hetjob_leader(het_job_id):
    """Verify sbcast to the hetjob leader puts the file in all components."""

    result = atf.run_command(f"sbcast --jobid={het_job_id} file {ns_dir}/file")
    assert result["exit_code"] == 0, f"sbcast should succeed: {result['stderr']}"

    for het_group in (0, 1):
        output = atf.run_command_output(
            f"srun --jobid={het_job_id} --het-group={het_group} cat {ns_dir}/file"
        )
        assert (
            output.strip() == file_content
        ), f"Component {het_group} should have the file in its namespace"

    assert not atf.run_command_output(
        f"ls -A {ns_dir}", fatal=True
    ), "The file should be only inside the job namespaces, not on the host"


def test_sbcast_hetjob_offset(het_job_id):
    """Verify sbcast to <leader>+<offset> puts the file only in that component."""

    for het_group in (0, 1):
        result = atf.run_command(
            f"sbcast --jobid={het_job_id}+{het_group} file {ns_dir}/file{het_group}"
        )
        assert (
            result["exit_code"] == 0
        ), f"sbcast to component {het_group} should succeed: {result['stderr']}"

    for het_group in (0, 1):
        output = atf.run_command_output(
            f"srun --jobid={het_job_id} --het-group={het_group} ls -A {ns_dir}"
        )
        assert output.split() == [
            f"file{het_group}"
        ], f"Component {het_group} should have only its own file, got {output}"

        output = atf.run_command_output(
            f"srun --jobid={het_job_id} --het-group={het_group}"
            f" cat {ns_dir}/file{het_group}"
        )
        assert (
            output.strip() == file_content
        ), f"Component {het_group} should have the file content, got {output}"

    assert not atf.run_command_output(
        f"ls -A {ns_dir}", fatal=True
    ), "The files should be only inside the job namespaces, not on the host"


def test_sbcast_hetjob_in_allocation():
    """Verify sbcast without --jobid in a hetjob allocation reaches all components."""

    atf.run_command(f"echo {file_content} > file", fatal=True)

    result = atf.run_command(
        f'salloc -N1 : -N1 bash -c "sbcast file {ns_dir}/file_alloc'
        f" && srun --het-group=0 cat {ns_dir}/file_alloc"
        f' && srun --het-group=1 cat {ns_dir}/file_alloc"'
    )
    assert (
        result["exit_code"] == 0
    ), f"sbcast and both reads should succeed: {result['stderr']}"
    assert result["stdout"].split() == [
        file_content,
        file_content,
    ], f"Both components should have the file, got {result['stdout']}"

    assert not atf.run_command_output(
        f"ls -A {ns_dir}", fatal=True
    ), "The file should be only inside the job namespaces, not on the host"
