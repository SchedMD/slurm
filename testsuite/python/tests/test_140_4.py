############################################################################
# Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
############################################################################
"""License request grammar accepted from users.

Pins the syntax _build_license_list() accepts and the counts it derives, so
that a rewrite of the parser cannot silently change which requests are valid.
Jobs request licenses with -L and --resources, where "(" is an ordinary
character. A reservation reserves a hierarchical resource on one of its
layers, named in parentheses as "name(layer):count".

Some of what is pinned here is quirk rather than design, notably that a space
is rejected in the name but accepted before the count.

Jobs are submitted held: only submit time validation and the requested counts
matter here, not whether the job can be scheduled.
"""

import pytest

import atf

LIC1 = "lgram1"
LIC2 = "lgram2"
LIC_TOTAL = 10
HRES = "lgramhres"
HRES_VAR = "half"
LAYER_COUNT = 100
RESV = "lgram_resv"


@pytest.fixture(scope="module", autouse=True)
def setup():
    atf.require_version(
        (26, 11),
        "sbin/slurmctld",
        reason="resources.yaml layer_name and parent_name were added in 26.11",
    )
    atf.require_nodes(2)
    # Accounting is needed for the requested license counts to show up in TRES.
    atf.require_accounting()
    atf.require_config_parameter_includes(
        "Licenses", f"{LIC1}:{LIC_TOTAL}", source="slurm"
    )
    atf.require_config_parameter_includes(
        "Licenses", f"{LIC2}:{LIC_TOTAL}", source="slurm"
    )
    atf.require_config_parameter_includes(
        "AccountingStorageTRES", f"license/{LIC1},license/{LIC2}"
    )
    atf.require_config_file(
        "resources.yaml",
        f"""
- resource: {HRES}
  mode: MODE_3
  variables:
    - name: {HRES_VAR}
      value: 5
  layers:
    - layer_name: "leaf1"
      parent_name: "top"
      nodes:
        - "node1"
      count: {LAYER_COUNT}
    - layer_name: "leaf2"
      parent_name: "top"
      nodes:
        - "node2"
      count: {LAYER_COUNT}
    - layer_name: "top"
      count: {2 * LAYER_COUNT}
""",
    )
    atf.require_slurm_running()


@pytest.fixture(scope="function")
def delete_resv():
    yield

    atf.run_command(
        f"scontrol delete reservation={RESV}",
        user=atf.properties["slurm-user"],
        quiet=True,
    )


def submit(licenses):
    """Submit a held job requesting licenses, returning its job id or 0."""

    return atf.submit_job_sbatch(f'--hold -L "{licenses}" --wrap="true"')


def submit_invalid(licenses):
    """Submit a job expected to be rejected, returning its job id or 0."""

    return atf.submit_job_sbatch(
        f'--hold -L "{licenses}" --wrap="true"', xfail=True, quiet=True
    )


def requested_tres(job_id):
    """The TRES the job asked for, which carries the license counts."""

    return atf.get_job_parameter(job_id, "ReqTRES", default="", fatal=True, quiet=True)


def create_resv(licenses, **run_command_kwargs):
    """Create a licenses only reservation, returning the scontrol exit code."""

    return atf.run_command_exit(
        f"scontrol create reservation={RESV} "
        f"user={atf.properties['slurm-user']} licenses='{licenses}' "
        "start=now duration=10 flags=LICENSE_ONLY",
        user=atf.properties["slurm-user"],
        **run_command_kwargs,
    )


@pytest.mark.parametrize(
    "request_str, expected_tres",
    [
        # A missing count means one.
        (f"{LIC1}", [f"license/{LIC1}=1"]),
        # Both ':' and '=' introduce the count.
        (f"{LIC1}:2", [f"license/{LIC1}=2"]),
        (f"{LIC1}=2", [f"license/{LIC1}=2"]),
        # ',' and ';' both separate requests.
        (f"{LIC1}:2,{LIC2}:3", [f"license/{LIC1}=2", f"license/{LIC2}=3"]),
        (f"{LIC1}:2;{LIC2}:3", [f"license/{LIC1}=2", f"license/{LIC2}=3"]),
        # Repeated requests for one license are added together.
        (f"{LIC1}:2,{LIC1}:3", [f"license/{LIC1}=5"]),
        # Whitespace before the count is skipped along with it.
        (f"{LIC1}: 2", [f"license/{LIC1}=2"]),
    ],
)
def test_accepted(request_str, expected_tres):
    """Valid requests are accepted and ask for the expected counts."""

    job_id = submit(request_str)
    assert job_id != 0, f"Job should be accepted with -L {request_str}"

    tres = requested_tres(job_id)
    for expected in expected_tres:
        assert (
            expected in tres
        ), f"-L {request_str} should request {expected}, got {tres}"


def test_request_recorded_verbatim():
    """The request is recorded as written, it is not rewritten to canonical form."""

    request_str = f"{LIC1}:2,{LIC1}:3"
    job_id = submit(request_str)
    assert job_id != 0, f"Job should be accepted with -L {request_str}"
    assert (
        atf.get_job_parameter(job_id, "Licenses", default=None, fatal=True, quiet=True)
        == request_str
    ), "The license request should be recorded exactly as submitted"


def test_or_operator():
    """'|' requests alternatives rather than a sum."""

    request_str = f"{LIC1}:2|{LIC2}:3"
    job_id = submit(request_str)
    assert job_id != 0, "Job should be accepted with an OR license request"
    assert (
        atf.get_job_parameter(job_id, "Licenses", default=None, fatal=True, quiet=True)
        == request_str
    ), "The OR license request should be recorded with '|'"


def test_zero_count_dropped():
    """A zero count is dropped rather than rejected."""

    job_id = submit(f"{LIC1}:0")
    assert job_id != 0, "Job should be accepted with a zero license count"
    assert f"license/{LIC1}" not in requested_tres(
        job_id
    ), "A zero count should request no licenses"


@pytest.mark.parametrize(
    "request_str",
    [
        # Mixing OR with AND is ambiguous.
        f"{LIC1}:2|{LIC2}:3,{LIC1}:1",
        f"{LIC1}:2|{LIC2}:3;{LIC1}:1",
        # Whitespace is not allowed in the name.
        f"{LIC1} :2",
        # The count must be a number, and the only thing after it.
        f"{LIC1}:-1",
        f"{LIC1}:2x",
        f"{LIC1}:2:3",
        # The license has to exist.
        "lgram_missing:1",
    ],
)
def test_rejected(request_str):
    """Invalid requests are rejected at submit."""

    assert (
        submit_invalid(request_str) == 0
    ), f"Job should be rejected with -L {request_str}"


@pytest.mark.skipif(
    atf.get_version("bin/sbatch") < (25, 11),
    reason="sbatch --resources was added in 25.11",
)
@pytest.mark.parametrize(
    "request_str",
    [
        f"{HRES}:5",
        # A count may name one of the resource's variables.
        f"{HRES}:{HRES_VAR}",
    ],
)
def test_hres_accepted(request_str):
    """A job may request a hierarchical resource by count or variable."""

    job_id = atf.submit_job_sbatch(f'--hold --resources="{request_str}" --wrap="true"')
    assert job_id != 0, f"Job should be accepted with --resources {request_str}"


@pytest.mark.parametrize(
    "request_str",
    [
        # Unknown variable.
        f"{HRES}:lgram_missing_var",
        # A job cannot name layers, "(" is part of the name here.
        f"{HRES}(leaf1):1",
    ],
)
def test_hres_rejected(request_str):
    """Invalid hierarchical resource requests are rejected at submit."""

    assert (
        submit_invalid(request_str) == 0
    ), f"Job should be rejected with -L {request_str}"


def test_resv_layer_accepted(delete_resv):
    """A reservation may name the layer it reserves."""

    request_str = f"{HRES}(leaf1):10"
    assert (
        create_resv(request_str) == 0
    ), f"Reservation should be created with licenses={request_str}"


@pytest.mark.parametrize(
    "request_str",
    [
        # The layer has to exist.
        f"{HRES}(lgram_missing):10",
        # The parenthesis has to be closed.
        f"{HRES}(leaf1:10",
        # The layer cannot hold more than it has.
        f"{HRES}(leaf1):{LAYER_COUNT + 1}",
    ],
)
def test_resv_layer_rejected(request_str, delete_resv):
    """Invalid layer requests are rejected when creating a reservation."""

    assert (
        create_resv(request_str, xfail=True, quiet=True) != 0
    ), f"Reservation should be rejected with licenses={request_str}"
