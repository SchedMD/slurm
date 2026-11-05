############################################################################
# Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
############################################################################
"""Test job --extra with SchedulerParameters=extra_constraints.

A job's --extra is an expression the controller parses at submission and
matches against each node's Extra JSON. A request that does not parse is
rejected. A valid request that no node matches pends with Reason=Constraints
for salloc, srun and sbatch alike, and starts once a node matches.

Parsing and matching do not depend on the command, so the expressions are only
submitted with sbatch.

Ticket 18133.
"""

import json
import shlex

import pytest

import atf

pytestmark = pytest.mark.slow

INVALID_EXTRA = "Invalid extra constraints specification"
COMMANDS = ["salloc", "srun", "sbatch"]

# Extra set on every extra node. The first keys are the example from the
# extra_constraints documentation.
NODE_EXTRA = {
    "a": 1.23,
    "b": True,
    "c": 0,
    "foo": "bar",
    "zed": 23,
    "f": 2.000009,
    "g": -1.000009,
    "n": None,
    " k": "v",
    "k ": "v",
    "w": " v ",
    "bf": False,
    "s": "23",
    "o": {"x": 1},
    "l": [1, 2],
}

# Requests matching NODE_EXTRA. Numbers are equal if they differ by less than
# 0.00001. Apart from the documented c>=0.00001 example, cases near that keep at
# least a factor of ten from it, as closer differences are not reliable.
MATCHING = [
    # Examples from the documentation
    "a=1.23",
    "a!=1.24",
    "a!=1.23|foo!=blah",
    "b=200",
    "b=true",
    "foo<baz",
    "(c<=0.0001&a=1.25)|zed=23.0",
    "((c<=0.0001&a=1.25)|zed=23.0)&(a<1|b=false|c>=0.00000001)",
    "((c<=0.0001&a=1.25)|zed=23.0)&(a<1|b=true|c>=0.1)",
    # A requested value is compared as the type of the node's value
    "b<=0.05",
    # Numbers are equal only within 0.00001
    "f=2.000008",
    "g=-1.000008",
    "a>1.229",
    # False is 0 or any case of false, f, no, n, off, null or ~, anything else
    # is true
    "bf=0",
    "bf=N",
    "bf=off",
    "bf=~",
    "b=yes",
    # Strings compare byte by byte, and a number in quotes is a string
    "foo>BAR",
    "s=23",
    "s<3",
    # Comma and & are both AND, so they can be mixed
    "a=1.23,b=true,c=0",
    "a=1.23&b=true,c=0",
    "(a=1.23&b=true)|(c=3&zed=1)",
    "((a=1|b=true)&(c=0|zed=1))",
    # Whitespace is part of keys and values
    " k=v",
    "k =v",
    "w= v ",
    "( k=v)&(k =v)",
]

NOT_MATCHING = [
    # Examples from the documentation
    "a!=1.23",
    "b=0",
    "b=false",
    "foo>baz",
    "((c<=0.0001&a=1.25)|zed=23.0)&(a<1|b=false|c>=0.00001)",
    "b<0.05",
    "a=   b",
    "( a=b)",
    "(a=1&b=2)|c=foobar",
    # Numbers are equal only within 0.00001
    "f=2.0001",
    "g=-1.0001",
    # Equal within 0.00001, so not less than
    "a<1.230001",
    # Numerical suffixes are not supported
    "zed=23kb",
    "a=1.23&foo=baz",
    "a=1|foo=baz",
    "missing=1",
    "n=null",
    # A value of another type, a missing key or null fails every operator
    "zed!=23kb",
    "zed<23kb",
    "missing!=1",
    "missing<1",
    "n!=1",
    # Booleans compare as false or true
    "b=FALSE",
    "b=off",
    "b=~",
    "bf=true",
    # Strings compare byte by byte, and a number in quotes is a string
    "foo=BAR",
    "s=23.0",
    "s>3",
    # Objects and arrays never match
    "o=1",
    "o!=1",
    "l=1",
    "l!=1",
    # Whitespace and quotes are part of keys and values
    "k=v",
    "w=v",
    " k=v & k =v ",
    'foo="bar"',
    "foo='bar'",
]

ILLEGAL = [
    # Examples from the documentation
    " (a=b)",
    "a,<=6",
    "a<=6<=",
    "a=5&&&b=5",
    "a=5|||b=5",
    "a====5",
    "b<=<=5",
    "a=5&()",
    "a=5&b=5|c=5",
    "(a=1)&(b=2)|(c=3)",
    "a=1(b=2)",
    "(a=1)(b=2)",
    "(((a=1)b=2))",
    "a=1&b=2|c=foobar",
    # Keys and values cannot be empty
    "=1",
    "a=",
    # Other malformed expressions
    "a",
    "1",
    "a==1",
    "a=!1",
    "a=>1",
    "a=<1",
    "a=1=2",
    "a=1 b=2",
    "a=1&",
    "&a=1",
    "(a=1",
    "a=1)",
    ")",
    "((a=1)&(b=2)",
    "(a=1))(b=2)",
    "(b>=2&)",
    "()",
    "(())",
    "(a=1|())",
    # Mixed boolean operators need parentheses
    "a=1,b=2|c=3",
    "(a=1&b=2|c=3)",
    "((a=1|b=2)&c=3|d=4)",
]


@pytest.fixture(scope="module", autouse=True)
def setup():
    atf.require_config_parameter_includes("SchedulerParameters", "extra_constraints")
    atf.require_config_parameter_includes("SchedulerParameters", ("sched_interval", 1))
    atf.require_nodes(3)
    atf.require_slurm_running()


@pytest.fixture(scope="module")
def nodes():
    """The nodes, with NODE_EXTRA set on all but the first.

    The first node has no Extra, so it never matches a job's --extra.
    """

    node_list = sorted(atf.get_nodes(fatal=True))
    for node in node_list[1:]:
        set_extra(node, json.dumps(NODE_EXTRA))

    yield node_list

    for node in node_list[1:]:
        set_extra(node, "")


@pytest.fixture
def first_node(nodes):
    """The first node, with any Extra a test sets on it cleared afterwards."""

    yield nodes[0]

    set_extra(nodes[0], "")


def set_extra(node, extra):
    atf.run_command(
        f"scontrol update nodename={node} extra={shlex.quote(extra)}",
        user=atf.properties["slurm-user"],
        fatal=True,
    )


def submit(command, request, args=""):
    """Submit a job with --extra and return its job id, or 0 if rejected."""

    kwargs = {}
    if command == "sbatch":
        args += " -o /dev/null"
    else:
        kwargs["background"] = True
    return atf.submit_job(
        command, f"{args} --extra={shlex.quote(request)}", "true", **kwargs
    )


def assert_runs(job_id, nodes, request):
    assert job_id, f"Verify job with --extra={request!r} is accepted"
    assert atf.wait_for_job_state(
        job_id, "COMPLETED"
    ), f"Verify job with --extra={request!r} runs"
    job_nodes = atf.node_range_to_list(
        atf.get_job_parameter(job_id, "NodeList", fatal=True)
    )
    assert set(job_nodes) <= set(
        nodes
    ), f"Verify job with --extra={request!r} runs on {nodes}, not {job_nodes}"


def assert_pends(job_id, request):
    assert job_id, f"Verify job with --extra={request!r} is accepted"
    assert atf.wait_for_job_state(
        job_id, "PENDING", desired_reason="Constraints"
    ), f"Verify job with --extra={request!r} pends with Reason=Constraints"


@pytest.mark.parametrize("request_expr", MATCHING)
def test_matching(nodes, request_expr):
    """Verify a job runs on the nodes its --extra matches"""

    assert_runs(submit("sbatch", request_expr), nodes[1:], request_expr)


@pytest.mark.parametrize("request_expr", NOT_MATCHING)
def test_not_matching(nodes, request_expr):
    """Verify a job pends when no node matches its --extra"""

    assert_pends(submit("sbatch", request_expr), request_expr)


@pytest.mark.parametrize("command", COMMANDS)
def test_node_count(nodes, command):
    """Verify a job only runs when enough nodes match its --extra"""

    matching = len(nodes) - 1
    assert_runs(submit(command, "a=1.23", f"-N{matching}"), nodes[1:], "a=1.23")
    assert_pends(submit(command, "a=1.23", f"-N{matching + 1}"), "a=1.23")


@pytest.mark.parametrize("command", COMMANDS)
def test_starts_after_node_update(first_node, command):
    """Verify a pending job starts once a node is updated to match its --extra"""

    job_id = submit(command, "late=1")
    assert_pends(job_id, "late=1")

    set_extra(first_node, '{"late":1}')
    assert_runs(job_id, [first_node], "late=1")


@pytest.mark.parametrize("request_expr", ILLEGAL)
def test_illegal(nodes, request_expr):
    """Verify a job with an --extra that does not parse is rejected"""

    error = atf.run_command_error(
        f"sbatch --extra={shlex.quote(request_expr)} -o /dev/null --wrap true",
        fatal=True,
        xfail=True,
    )
    assert INVALID_EXTRA in error, f"Verify --extra={request_expr!r} is rejected"


@pytest.mark.parametrize("command", ["salloc", "srun"])
def test_illegal_allocation(nodes, command):
    """Verify salloc and srun are rejected like sbatch when --extra does not parse"""

    error = atf.run_command_error(
        f"{command} --extra='a=1&' true", fatal=True, xfail=True
    )
    assert INVALID_EXTRA in error, "Verify --extra='a=1&' is rejected"


def test_update_job_extra(nodes):
    """Verify scontrol update job only accepts an --extra that parses"""

    job_id = atf.submit_job_sbatch(
        "-H --extra='a=1.23' -o /dev/null --wrap true", fatal=True
    )

    result = atf.run_command(
        f"scontrol update jobid={job_id} extra='a=1&'",
        user=atf.properties["slurm-user"],
        fatal=True,
        xfail=True,
    )
    assert result["exit_code"] != 0, "Verify the update is rejected"
    assert INVALID_EXTRA in result["stderr"], "Verify scontrol reports why"
    assert (
        atf.get_job_parameter(job_id, "Extra") == "a=1.23"
    ), "Verify the job Extra is unchanged"

    atf.run_command(
        f"scontrol update jobid={job_id} extra='late=1'",
        user=atf.properties["slurm-user"],
        fatal=True,
    )
    atf.run_command(f"scontrol release {job_id}", fatal=True)
    assert_pends(job_id, "late=1")
