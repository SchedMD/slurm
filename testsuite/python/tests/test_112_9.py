############################################################################
# Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
############################################################################
"""Verify external and mixed hetjobs submitted through slurmrestd."""

import importlib
import json
import os
import types

import pytest

import atf

# Every REST test runs once per data_parser version. 26.05 has no v0.0.46,
# and slurmrestd fatals on a parser it lacks.
serves_v0046 = atf.get_version("sbin/slurmrestd") >= (26, 11)
rest_versions = [
    "v0.0.45",
    pytest.param(
        "v0.0.46",
        marks=pytest.mark.skipif(
            not serves_v0046,
            reason="Ticket 25423: data_parser v0.0.46 was added in 26.11",
        ),
    ),
]
data_parsers = ["v0.0.45", "v0.0.46"] if serves_v0046 else ["v0.0.45"]

# One external node per component of the widest all-external hetjob here
component_count = 3
node_names = [f"extnode{i}" for i in range(1, component_count + 1)]
partition_name = "external"

# Components of a hetjob never share a node, so a hetjob with two regular
# components needs a compute node apiece or it pends on Resources for good.
compute_nodes = ["node1", "node2"]

# Printed by the mixed hetjob leader's batch script, so the test can tell that
# the script actually ran rather than that the job merely held an allocation
batch_marker = "mixed_leader_ran"


@pytest.fixture(scope="module", autouse=True)
def setup():
    # slurmrestd is upgraded with the client commands, so it is never newer
    # than slurmctld and this also gates the controller side
    atf.require_version(
        (26, 5, 3),
        "sbin/slurmrestd",
        reason="Issue 50997: scriptless REST hetjob submission is available in 26.05.3",
    )

    atf.require_config_parameter("SelectType", "select/cons_tres")
    # Overwrite rather than append: the CR_* allocation types are mutually
    # exclusive, so adding CR_CPU to a config already setting CR_Core_Memory
    # yields "cr_core_memory,CR_CPU", which slurmctld rejects.
    atf.require_config_parameter("SelectTypeParameters", "CR_CPU")

    # Only the backfill scheduler starts hetjobs, so shorten its cycle
    atf.require_config_parameter_includes("SchedulerParameters", ("bf_interval", 1))

    # Static EXTERNAL nodes: a node created with "scontrol create" is also
    # flagged DYNAMIC_NORM, which drags the allocation path into alias and
    # address lookups that have nothing to resolve for an external node.
    # The compute nodes need NodeAddr because the bare names do not resolve
    # here, and a Port apiece because both slurmds run on this host.
    node_config = {
        node_name: {"CPUs": 4, "NodeAddr": "localhost", "Port": 6821 + index}
        for index, node_name in enumerate(compute_nodes)
    }
    for node_name in node_names:
        node_config[node_name] = {"CPUs": 4, "State": "EXTERNAL"}
    atf.require_config_parameter("NodeName", node_config)

    # Keep the external nodes in their own partition so an external job is
    # guaranteed to land on them rather than on a compute node.
    atf.require_config_parameter(
        "PartitionName",
        {
            "primary": {"Nodes": ",".join(compute_nodes), "Default": "YES"},
            partition_name: {"Nodes": ",".join(node_names)},
        },
    )

    # This module starts no slurmdbd
    atf.require_accounting(enabled=False)

    atf.require_config_parameter_includes("AuthAltTypes", "auth/jwt")
    atf.require_slurmrestd("slurmctld", ",".join(data_parsers))

    # require_slurm_running() starts a slurmd for every NodeName line, but an
    # EXTERNAL node has none, so start the controller and the compute nodes'
    # slurmds alone.
    atf.start_slurmctld(clean=True)
    for node_name in compute_nodes:
        atf.start_slurmd(node_name, wait="idle")
    atf.start_slurmrestd()

    yield

    # Not fatal: scancel refuses a pending non-leader hetjob component.
    # stop_slurm() reaps slurmrestd too.
    atf.cancel_all_jobs(quiet=True)
    atf.stop_slurm(quiet=True)


@pytest.fixture(autouse=True)
def cancel_jobs_after_test():
    """Cancel each test's jobs so the next one finds the nodes free.

    Not fatal: scancel refuses a pending non-leader hetjob component.
    """
    yield
    atf.cancel_all_jobs(quiet=True)


@pytest.fixture(scope="module")
def openapi_generated(setup):
    """Generate the OpenAPI client once, after setup() has started the daemons.

    A second generation in one process raises "duplicate validator function".
    """
    atf.require_openapi_generator("7.3.0")


@pytest.fixture(params=rest_versions)
def rest(request, openapi_generated):
    """The job submit method and models of one data_parser version."""
    # "v0.0.45" names its models "V0045..." in openapi_client.models.v0045_...
    tag = request.param.replace(".", "")
    prefix = tag.capitalize()

    def model(module, name):
        return getattr(
            importlib.import_module(f"openapi_client.models.{tag}_{module}"),
            f"{prefix}{name}",
        )

    return types.SimpleNamespace(
        submit=getattr(atf.openapi_slurm(), f"slurm_{tag}_post_job_submit"),
        JobDescMsg=model("job_desc_msg", "JobDescMsg"),
        JobSubmitReq=model("job_submit_req", "JobSubmitReq"),
        Uint32NoValStruct=model("uint32_no_val_struct", "Uint32NoValStruct"),
    )


def regular_component(rest, **fields):
    """Return a one task component for the compute nodes."""
    return rest.JobDescMsg(
        tasks=1,
        time_limit=rest.Uint32NoValStruct(set=True, number=5),
        partition="primary",
        environment=["PATH=/bin/:/sbin/:/usr/bin/:/usr/sbin/"],
        # conftest chdirs each test into an isolated, exec-enabled tmp dir
        current_working_directory=os.getcwd(),
        **fields,
    )


def external_component(rest):
    """Return a one task external component for the external nodes."""
    return rest.JobDescMsg(
        flags=["EXTERNAL_JOB"],
        tasks=1,
        time_limit=rest.Uint32NoValStruct(set=True, number=5),
        partition=partition_name,
    )


def batch_script(marker):
    """Return a batch script that prints marker and keeps running."""
    return f"#!/bin/bash\necho {marker}\nsleep infinity\n"


def submit_hetjob(rest, components):
    """Submit components as one hetjob and return the leader's job id."""
    resp = rest.submit(rest.JobSubmitReq(jobs=components))
    assert not resp.errors, f"Hetjob submission returned errors: {resp.errors}"
    assert resp.job_id, "Hetjob submission returned no job_id"
    return int(resp.job_id)


def submit_mixed_hetjob(rest):
    """Submit a regular leader with a batch script plus an external component.

    Returns the leader's job id.
    """
    return submit_hetjob(
        rest,
        [
            regular_component(
                rest, name="mixed_leader", script=batch_script(batch_marker)
            ),
            external_component(rest),
        ],
    )


def running_components(leader_job_id, count):
    """Wait for the leader's count components to run.

    Returns their job ids in offset order.
    """
    het_job_expression = atf.get_job_parameter(leader_job_id, "HetJobIdSet", fatal=True)
    assert het_job_expression is not None, "Leader is missing HetJobIdSet"

    component_list = sorted(atf.range_to_list(het_job_expression))
    assert (
        len(component_list) == count
    ), f"Expected {count} components, got {component_list}"
    assert (
        component_list[0] == leader_job_id
    ), f"Leader {leader_job_id} is not the lowest component id in {component_list}"

    for component_job_id in component_list:
        atf.wait_for_job_state(component_job_id, "RUNNING", fatal=True)

    return component_list


def assert_on_external_nodes(jobs_dict, job_id):
    """Assert the component was allocated EXTERNAL nodes only."""
    nodes = atf.node_range_to_list(jobs_dict[job_id]["NodeList"])
    assert nodes, f"Component {job_id} was allocated no nodes"
    for node in nodes:
        assert (
            node in node_names
        ), f"Component {job_id} ran on {node}, expected one of {node_names}"
        assert "EXTERNAL" in atf.get_node_parameter(
            node, "state"
        ), f"Node {node} is not in EXTERNAL state"


def assert_on_compute_nodes(jobs_dict, job_id):
    """Assert the component was allocated nodes with a slurmd only."""
    nodes = atf.node_range_to_list(jobs_dict[job_id]["NodeList"])
    assert nodes, f"Component {job_id} was allocated no nodes"
    for node in nodes:
        assert (
            node in compute_nodes
        ), f"Component {job_id} ran on {node}, expected one of {compute_nodes}"


def assert_refused(exc, error_number, error, source):
    """Assert a refused submission reports the error from source, and no job."""
    assert exc.status >= 400, f"Refused submission answered HTTP {exc.status}"

    body = json.loads(exc.body)
    assert not body.get("job_id"), f"Refused hetjob created job {body['job_id']}"

    errors = body.get("errors") or []
    assert any(
        entry.get("error_number") == error_number
        and str(entry.get("error")).lower() == error.lower()
        and entry.get("source") == source
        for entry in errors
    ), f"Expected error {error_number} ({error}) from {source}, got {errors}"


def test_external_hetjob_rest(rest):
    """
    An external hetjob submits and runs on external nodes. No component
    carries a batch script, so this exercises the REST job submission path's
    handling of a scriptless hetjob leader.
    """

    leader_job_id = submit_hetjob(
        rest, [external_component(rest) for _ in range(component_count)]
    )
    component_list = running_components(leader_job_id, component_count)

    jobs_dict = atf.get_jobs(fatal=True)
    for offset, component_job_id in enumerate(component_list):
        assert component_job_id in jobs_dict, f"Component {component_job_id} not found"
        component_dict = jobs_dict[component_job_id]
        assert (
            component_dict["HetJobId"] == leader_job_id
        ), f"Component {component_job_id} HetJobId != leader {leader_job_id}"
        assert (
            component_dict["HetJobOffset"] == offset
        ), f"Component {component_job_id} HetJobOffset != {offset}"
        assert (
            component_dict["JobState"] == "RUNNING"
        ), f"Component {component_job_id} is not RUNNING"
        assert_on_external_nodes(jobs_dict, component_job_id)


def test_hetjob_regular_leader_with_external_component(rest):
    """
    A hetjob may mix a regular leader with external components, as long as the
    leader supplies the batch script.

    The leader owns the script and runs on a node with slurmd; the external
    component only reserves external capacity and never gets a script.
    """

    leader_job_id = submit_mixed_hetjob(rest)
    component_list = running_components(leader_job_id, 2)

    jobs_dict = atf.get_jobs(fatal=True)
    assert_on_compute_nodes(jobs_dict, leader_job_id)
    assert_on_external_nodes(jobs_dict, component_list[1])

    # _het_job_ready() gates the batch launch on every component being ready,
    # so this shows the external component does not block the leader's launch.
    atf.assert_file_contents(
        jobs_dict[leader_job_id]["StdOut"],
        batch_marker,
        contains=True,
        message=f"Leader {leader_job_id} never ran its batch script",
    )


@pytest.mark.parametrize("trailer", ["regular", "external"])
def test_scriptless_regular_leader_rejected(rest, trailer):
    """
    slurmrestd refuses a hetjob whose regular leader has no batch script,
    whatever component follows it.
    """

    from openapi_client.exceptions import ApiException

    components = [
        regular_component(rest, name="scriptless_leader"),
        regular_component(rest) if trailer == "regular" else external_component(rest),
    ]

    jobs_before = atf.get_jobs(fatal=True).keys()
    with pytest.raises(ApiException) as exc_info:
        rest.submit(rest.JobSubmitReq(jobs=components))

    assert (
        exc_info.value.status == 422
    ), f"Expected HTTP 422, got {exc_info.value.status}: {exc_info.value.body}"
    # The source shows slurmrestd refused it rather than slurmctld
    assert_refused(
        exc_info.value, 2009, "Job script not specified", "_job_post_het_submit"
    )

    new_jobs = atf.get_jobs(fatal=True).keys() - jobs_before
    assert not new_jobs, f"Refused hetjob created jobs {sorted(new_jobs)}"


def test_external_leader_with_regular_component_rejected(rest):
    """
    A hetjob whose leader is external must not carry non-external components.

    Only the leader supplies the batch script and an external leader has none,
    so slurmctld refuses a regular component behind it. This covers the REST
    submission path's handling of that controller rule.
    """

    from openapi_client.exceptions import ApiException

    components = [
        external_component(rest),
        # No script, and no EXTERNAL_JOB: a component is non-external simply
        # by lacking the flag. Giving it a script would make it external-with
        # -a-script if the flag ever leaked onto it, and _validate_job_desc()
        # refuses that with the same errno as the rule under test.
        rest.JobDescMsg(
            tasks=1,
            time_limit=rest.Uint32NoValStruct(set=True, number=5),
        ),
    ]

    with pytest.raises(ApiException) as exc_info:
        rest.submit(rest.JobSubmitReq(jobs=components))

    # slurmrestd before 26.11 answers 500 for this errno
    if atf.get_version("sbin/slurmrestd") >= (26, 11):
        assert (
            exc_info.value.status == 422
        ), f"Expected HTTP 422, got {exc_info.value.status}: {exc_info.value.body}"
    # The source shows slurmctld refused it, not slurmrestd
    assert_refused(
        exc_info.value,
        2189,
        "Invalid configuration for external job",
        "slurm_submit_batch_het_job()",
    )

    # The refusal must leave slurmctld running
    atf.run_command("scontrol ping", fatal=True)


def test_concurrent_mixed_hetjobs(rest):
    """
    Two mixed hetjobs run at the same time, and each leader runs its own
    batch script.
    """

    leader_job_ids = [submit_mixed_hetjob(rest) for _ in range(2)]

    for leader_job_id in leader_job_ids:
        atf.wait_for_job_state(leader_job_id, "RUNNING", fatal=True)

    # One snapshot, so this shows the hetjobs running at the same time rather
    # than each having run at some point.
    jobs_dict = atf.get_jobs(fatal=True)
    for leader_job_id in leader_job_ids:
        assert (
            jobs_dict[leader_job_id]["JobState"] == "RUNNING"
        ), f"Leader {leader_job_id} is not RUNNING alongside the others"

        # Every leader has to have actually launched, not just one of them
        atf.assert_file_contents(
            jobs_dict[leader_job_id]["StdOut"],
            batch_marker,
            contains=True,
            message=f"Leader {leader_job_id} never ran its batch script",
        )


def test_leader_script_wins(rest):
    """
    A regular component may follow an external one, and only the leader's
    batch script runs; the trailing component's own script is ignored.
    """

    leader_marker = "leader_script_ran"
    trailer_marker = "trailer_script_ran"

    leader_job_id = submit_hetjob(
        rest,
        [
            regular_component(
                rest, name="mixed_leader", script=batch_script(leader_marker)
            ),
            external_component(rest),
            regular_component(
                rest, name="mixed_trailer", script=batch_script(trailer_marker)
            ),
        ],
    )
    component_list = running_components(leader_job_id, 3)

    jobs_dict = atf.get_jobs(fatal=True)
    assert_on_compute_nodes(jobs_dict, leader_job_id)
    assert_on_external_nodes(jobs_dict, component_list[1])
    assert_on_compute_nodes(jobs_dict, component_list[2])

    leader_stdout = jobs_dict[leader_job_id]["StdOut"]
    atf.assert_file_contents(
        leader_stdout,
        leader_marker,
        contains=True,
        message=f"Leader {leader_job_id} never ran its batch script",
    )
    output = atf.run_command_output(f"cat {leader_stdout}", fatal=True)
    assert (
        trailer_marker not in output
    ), f"Leader {leader_job_id} ran the trailing component's script: {output}"
