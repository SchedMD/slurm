############################################################################
# Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
############################################################################
"""Verify the HTTP status slurmrestd returns for a rejected request.

A request that is well formed but refused for the values in it is a client
error, not a server fault, so it must come back as 422 rather than 500.
"""

import pytest

import atf

# Any data_parser works: the status code is chosen before the response is
# serialized. Pinned to the newest so it survives version removals longest.
data_parser = "v0.0.46"
array_size = 3

# Job fields the slurmctld refuses: "array" values that violate the documented
# --array syntax (a range whose end precedes its start, a zero step, a
# non-numeric index and a separator carrying no index at all), and a max_pids
# of 0, which would let no process start.
INVALID_FIELDS = [
    pytest.param({"array": "3-2"}, id="array-range"),
    pytest.param({"array": "0-10:0"}, id="array-step"),
    pytest.param({"array": "abc"}, id="array-non-numeric"),
    pytest.param({"array": ","}, id="array-empty"),
    pytest.param(
        {"max_pids": {"set": True, "infinite": False, "number": 0}},
        id="max-pids-zero",
    ),
]

# Array task id suffixes a signal request refuses: a range whose end precedes
# its start, a zero step, a negative index, a non-numeric index, an empty index
# list, an empty index, a range with no end, and an index above the maximum in
# both the plain and the bracketed form. "{max}" is replaced with MaxArraySize.
INVALID_ARRAY_IDS = [
    pytest.param("_[3-2]", id="range"),
    pytest.param("_[0-10:0]", id="step"),
    pytest.param("_-1", id="negative"),
    pytest.param("_abc", id="non-numeric"),
    pytest.param("_[]", id="empty-list"),
    pytest.param("_", id="empty"),
    pytest.param("_[0-]", id="open-range"),
    pytest.param("_{max}", id="too-large"),
    pytest.param("_[{max}]", id="bracketed-too-large"),
]


@pytest.fixture(scope="module", autouse=True)
def setup():
    atf.require_version(
        (26, 11),
        "sbin/slurmrestd",
        reason="Ticket 25423: slurmrestd answered HTTP 500 instead of 422 for the job array error codes added in 26.11",
    )
    atf.require_config_parameter("MaxArraySize", array_size, ">=")
    atf.require_config_parameter_includes("AuthAltTypes", "auth/jwt")
    atf.require_slurmrestd("slurmctld,util", data_parser)
    atf.require_slurm_running()

    # Setup OpenAPI client with OpenAPI-Generator once Slurm(restd) is running
    atf.require_openapi_generator("7.3.0")


@pytest.fixture(scope="function")
def slurm(setup):
    yield atf.openapi_slurm()


def submit_request(**fields):
    """Returns a minimal batch job submission with the given job fields."""
    from openapi_client.models.v0046_job_desc_msg import V0046JobDescMsg
    from openapi_client.models.v0046_job_submit_req import V0046JobSubmitReq

    return V0046JobSubmitReq(
        script="#!/bin/bash\ndate\n",
        job=V0046JobDescMsg(
            environment=["PATH=/bin"],
            current_working_directory="/tmp",
            standard_output="/dev/null",
            **fields,
        ),
    )


def signal_request(job_id_str):
    """Returns a request to cancel the job named by job_id_str."""
    from openapi_client.models.v0046_kill_jobs_msg import V0046KillJobsMsg

    return V0046KillJobsMsg(jobs=[job_id_str])


@pytest.fixture
def array_job_id():
    """Submits a held job array and returns its job id."""
    job_id = atf.submit_job_sbatch(
        f"-a 0-{array_size - 1} -t5 --hold --wrap 'sleep infinity'",
        fatal=True,
    )
    yield job_id


def array_task_ids(job_id):
    """Returns the sorted task ids of a job array that were not cancelled."""
    return sorted(
        atf.run_command_output(
            f"squeue --noheader --jobs={job_id} --array --Format=ArrayTaskId",
            fatal=True,
        ).split()
    )


def assert_rejected(slurm, request, response_type, what):
    """Asserts that request answers 422 with one error that says why."""
    from openapi_client.api_response import ApiResponse
    from openapi_client.exceptions import ApiException

    try:
        request()
        pytest.fail(f"{what} should have been rejected")
    except ApiException as e:
        assert e.status == 422, f"{what} answered {e.status}: {e.body}"
        response = slurm.api_client.deserialize(
            response=ApiResponse(data=e.body), response_type=response_type
        )
        assert len(response.errors) == 1, f"{what} should report one error"
        assert response.errors[0].error, f"{what} should describe its error"
        assert not response.warnings, f"{what} should warn about nothing"


@pytest.mark.parametrize("fields", INVALID_FIELDS)
def test_invalid_job_returns_422(slurm, fields):
    """Verify a job rejected for one of its fields answers 422, and says why."""
    assert_rejected(
        slurm,
        lambda: slurm.slurm_v0046_post_job_submit_with_http_info(
            submit_request(**fields)
        ),
        "V0046OpenapiJobSubmitResponse",
        fields,
    )


def test_valid_array_returns_200(slurm):
    """Verify an accepted job still answers 200."""
    response = slurm.slurm_v0046_post_job_submit(
        submit_request(array=f"0-{array_size - 1}")
    )
    assert len(response.errors) == 0, f"submission reported {response.errors}"
    assert response.job_id, "submission should return a job id"


@pytest.mark.parametrize("expr", INVALID_ARRAY_IDS)
def test_invalid_array_id_signal_returns_422(slurm, array_job_id, expr):
    """Verify signalling an invalid array task id answers 422 and cancels nothing."""
    max_array_size = atf.get_config_parameter("MaxArraySize")
    job_id_str = f"{array_job_id}{expr.format(max=max_array_size)}"
    assert_rejected(
        slurm,
        lambda: slurm.slurm_v0046_delete_jobs_with_http_info(
            signal_request(job_id_str)
        ),
        "V0046OpenapiKillJobsResp",
        job_id_str,
    )
    assert array_task_ids(array_job_id) == [
        str(task_id) for task_id in range(array_size)
    ], f"Signalling {job_id_str} should leave every task of {array_job_id}"


def test_valid_array_id_signal_returns_200(slurm, array_job_id):
    """Verify signalling a valid array task id answers 200 and cancels it."""
    cancelled = array_size - 1
    response = slurm.slurm_v0046_delete_jobs(
        signal_request(f"{array_job_id}_{cancelled}")
    )
    assert len(response.errors) == 0, f"signal reported {response.errors}"
    assert array_task_ids(array_job_id) == [
        str(task_id) for task_id in range(cancelled)
    ], f"Only task {cancelled} of {array_job_id} should have been cancelled"
