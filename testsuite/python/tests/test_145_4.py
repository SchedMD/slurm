############################################################################
# Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
############################################################################
"""Verify --prefer validation, placement, and combination with --constraint.

--prefer is validated at submit time exactly like --constraint: a feature name
no node carries is rejected with ESLURM_INVALID_PREFER rather than being
ignored. That only changes when SchedulerParameters=ignore_prefer_validation is
set, which test_145_5 covers.

--prefer and --constraint are never evaluated together. A job carrying both is
tested against --prefer first and, only if that cannot be satisfied, retested
against --constraint; it is always one list or the other, never an intersection
of the two.

This file is the --prefer counterpart of test_145_2 (the validation matrix) and
of test_145_1's invalid-expression cases.
"""

import re

import pytest

import atf

# Error text for ESLURM_INVALID_PREFER. The --constraint equivalent is
# "Invalid feature specification"; asserting the exact string is what keeps a
# prefer failure from silently being reported through the hard-constraint error
# path.
INVALID_PREFER = "Invalid preferred feature specification"


@pytest.fixture(scope="module", autouse=True)
def setup():
    # Every assertion below about an unsatisfiable --prefer being rejected only
    # holds while validation is on, so make sure the site has not enabled it.
    atf.require_config_parameter_excludes(
        "SchedulerParameters", "ignore_prefer_validation"
    )
    # Same layout as test_145_2 so the --prefer matrix mirrors the --constraint
    # one: one node per single feature, plus one node carrying both.
    atf.require_nodes(1, [("CPUs", 1), ("Features", "f1")])
    atf.require_nodes(1, [("CPUs", 1), ("Features", "f2")])
    atf.require_nodes(1, [("CPUs", 1), ("Features", "f1,f2")])
    # f3 is carried by a node holding neither f1 nor f2, so a --prefer=f3
    # request can never also satisfy --constraint=f1.
    atf.require_nodes(1, [("CPUs", 1), ("Features", "f3")])
    atf.require_slurm_running()


def node_features(node):
    """Return the list of features a node carries."""

    return atf.get_node_parameter(node, "features", default=[])


@pytest.mark.parametrize(
    "prefer,xfail",
    [
        ("invalid", True),
        ("f1,invalid", True),
        ("f2,invalid", True),
        ("invalid,f1", True),
        ("invalid,f2", True),
        ("f1", False),
        ("f2", False),
        ("f1,f2", False),
        ("f2,f1", False),
    ],
)
@pytest.mark.parametrize("command", ["srun", "salloc", "sbatch"])
def test_prefer(prefer, xfail, command):
    """Verify --prefer option"""

    if command == "sbatch":
        cmd = f"sbatch --prefer={prefer} -o /dev/null --wrap hostname"
    else:
        cmd = f'{command} --prefer={prefer} env | grep -E "^SLURM_JOB_NODELIST="'
    result = atf.run_command(cmd, xfail=xfail)

    if xfail:
        assert result["exit_code"] != 0, "Verify that job was NOT submitted"
        assert (
            INVALID_PREFER in result["stderr"]
        ), f"Verify that '{INVALID_PREFER}' is in stderr"
        return

    assert result["exit_code"] == 0, "Verify that job was submitted"

    if command == "sbatch":
        match = re.search(r"Submitted \S+ job (\d+)", result["stdout"])
        assert match, "Verify sbatch reported the submitted job id"
        job_id = int(match.group(1))
        atf.properties["submitted-jobs"].append(job_id)
        atf.wait_for_job_state(job_id, "DONE", fatal=True)
        node = atf.get_job_parameter(job_id, "NodeList")
    else:
        match = re.search(r"SLURM_JOB_NODELIST=(\S+)", result["stdout"])
        assert match, f"Verify {command} reported the node it ran on"
        node = match.group(1)

    # Nothing else is running, so a satisfiable --prefer is always honored.
    features = node_features(node)
    for feature in prefer.split(","):
        assert feature in features, f"Verify {node} has feature {feature}"


@pytest.mark.parametrize(
    "f1, f2",
    [
        (0, 1),
        (1, 0),
        ("!", "*"),
        ("*", "!"),
        ("#", "^"),
        ("^", "#"),
    ],
)
def test_bad_prefer(f1, f2):
    """Verify invalid --prefer expressions are rejected like --constraint ones

    The man pages say --prefer "behaves the same way" as --constraint, so the
    invalid count and operator expressions from test_145_1 are rejected here
    too, but reported as an invalid preferred feature specification.
    """

    params = f'--ntasks-per-node=1 --prefer "[f1*{f1}&f2*{f2}]"'
    output = atf.run_command_error(f"srun {params} hostname", xfail=True)

    assert INVALID_PREFER in output, f"Verify that '{INVALID_PREFER}' message"


def test_prefer_evaluated_instead_of_constraint():
    """Verify a satisfiable --prefer is used instead of --constraint, not with it

    The man pages document --prefer as operating "independently from
    --constraint" and overriding "whatever is set there if possible", so
    --constraint takes no part in a pass that --prefer can satisfy.

    test_145_5 covers the other half of the switch: once --prefer cannot be
    satisfied, the job is retested against --constraint alone.
    """

    # No node holds both f3 and f1. Landing on the f3 node, which lacks f1,
    # shows --constraint was not applied alongside --prefer.
    job_id = atf.submit_job(
        "sbatch",
        "-N1 --constraint=f1 --prefer=f3 -o /dev/null",
        "hostname",
        fatal=True,
    )
    atf.wait_for_job_state(job_id, "DONE", fatal=True)

    nodes = atf.node_range_to_list(atf.get_job_parameter(job_id, "NodeList"))
    assert len(nodes) == 1, f"Verify the job ran on a single node, got {nodes}"

    features = node_features(nodes[0])
    assert "f3" in features, f"Verify {nodes[0]} has the preferred feature f3"
    assert (
        "f1" not in features
    ), f"Verify {nodes[0]} was chosen by --prefer alone, ignoring --constraint=f1"


def test_invalid_prefer_with_valid_constraint():
    """Verify an unknown --prefer feature is rejected even with a valid --constraint"""

    result = atf.run_command(
        "sbatch -N1 --constraint=f1 --prefer=invalid -o /dev/null --wrap 'hostname'",
        xfail=True,
    )

    assert result["exit_code"] != 0, "Verify that job was NOT submitted"
    assert (
        INVALID_PREFER in result["stderr"]
    ), f"Verify that '{INVALID_PREFER}' is in stderr"
