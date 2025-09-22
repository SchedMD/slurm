############################################################################
# Copyright (C) SchedMD LLC.
############################################################################
import atf
import pytest
import logging

# Global variables
qos1 = "qos1"
acct1 = "acct1"
user1 = "user1"

# Data used as test parameter.
# Each element of the data list has 5 values:
# - title: short identificative title of the test. Used as data_id and logging.
# - desc: description of what is exactly being tested. Used for logging.
# - base: the MaxTRES value to be set initially
# - input: the MaxTRES value change to be applied on top of the base value
# - result: the MaxTRES value expected after the change
data = [
    (
        "decrement_from_0",
        "Checking that decrementing unset TRES results in 0",
        None,
        "billing-=3,cpu-=1,mem-=500",
        "billing=0,cpu=0,mem=0",
    ),
    (
        "increment_from_0",
        "Checking that TRES increments from 0 produce expected results",
        "billing=0,cpu=0,mem=0",
        "billing=3000,cpu+=50000000000,mem+=2000",
        "billing=3000,cpu=50000000000,mem=2000M",
    ),
    (
        "basic_decrement",
        "Checking that TRES decrements produce expected results",
        "billing=3000,cpu=50000000000,mem=2000",
        "billing-=500",
        "billing=2500,cpu=50000000000,mem=2000M",
    ),
    (
        "combined_syntax",
        "Checking that combining absolute, incremental and decremental syntax works as expected",
        "billing=2500,cpu=50000000000,mem=2000",
        "billing+=500,cpu-=10000000000,mem=2500",
        "billing=3000,cpu=40000000000,mem=2500M",
    ),
    (
        "decrement_below_0",
        "Checking that TRES decrements resulting in negative values are capped at 0",
        "billing=3000,cpu=40000000000,mem=2500",
        "billing-=4000",
        "billing=0,cpu=40000000000,mem=2500M",
    ),
    (
        "increment_over_max_val64",
        "Checking that TRES increments cannot go over 18446744073709551600 (MAX_VAL64)",
        "billing=0,cpu=40000000000,mem=2500",
        "cpu+=99999999999999999999999",
        "billing=0,cpu=18446744073709551600,mem=2500M",
    ),
    (
        "unset_check",
        "Checking that unsetting TRES syntax still works as intended",
        "billing=0,cpu=18446744073709551600,mem=2500",
        "cpu=-1,mem=-1",
        "billing=0",
    ),
]

data_ids = [d[0] for d in data]


@pytest.fixture(scope="module", autouse=True)
def setup():
    # Ticket 23597: Add support for += and -= syntax to GrpTRESMins
    atf.require_version((25, 11), "bin/sacctmgr")
    atf.require_accounting(True)
    atf.require_slurm_running()


@pytest.fixture(scope="function", autouse=True)
def setup_db():
    # Create test QOS and account
    atf.run_command(
        f"sacctmgr -i add qos {qos1}",
        user=atf.properties["slurm-user"],
        fatal=True,
    )
    atf.run_command(
        f"sacctmgr -i add account {acct1}",
        user=atf.properties["slurm-user"],
        fatal=True,
    )
    atf.run_command(
        f"sacctmgr -i add user {user1} account={acct1}",
        user=atf.properties["slurm-user"],
        fatal=True,
    )

    yield

    atf.run_command(
        f"sacctmgr -i remove user {user1}",
        user=atf.properties["slurm-user"],
        fatal=True,
    )
    atf.run_command(
        f"sacctmgr -i remove account {acct1}",
        user=atf.properties["slurm-user"],
        quiet=True,
    )
    atf.run_command(
        f"sacctmgr -i remove qos {qos1}",
        user=atf.properties["slurm-user"],
        quiet=True,
    )


@pytest.mark.parametrize("limit", ["MaxTRES", "GrpTRESMins"])
@pytest.mark.parametrize("title,desc,base,input,result", data, ids=data_ids)
def test_modify_user_tres_with_amend_syntax(limit, title, desc, base, input, result):
    """Test that user TRES modifications with +=/-= syntax work"""

    logging.info(f"Running case {title}: {desc}")

    if base:
        atf.run_command(
            f"sacctmgr -i mod user {user1} where account={acct1} set {limit}={base}",
            user=atf.properties["slurm-user"],
            fatal=True,
        )

    atf.run_command(
        f"sacctmgr -i mod user {user1} where account={acct1} set {limit}={input}",
        user=atf.properties["slurm-user"],
        fatal=True,
    )
    output = atf.run_command_output(
        f"sacctmgr show assoc -nP where user={user1} account={acct1} format={limit}",
        fatal=True,
    )
    assert (
        output.rstrip() == result
    ), f"Association ({user1}, {acct1}) should have {result}, not {output.rstrip()}"


@pytest.mark.parametrize("limit", ["MaxTRES", "GrpTRESMins"])
@pytest.mark.parametrize("title,desc,base,input,result", data, ids=data_ids)
def test_modify_qos_tres_with_amend_syntax(limit, title, desc, base, input, result):
    """Test that qos TRES modifications with +=/-= syntax work"""

    logging.info(f"Running case {title}: {desc}")

    if base:
        atf.run_command(
            f"sacctmgr -i mod qos {qos1} set {limit}={base}",
            user=atf.properties["slurm-user"],
            fatal=True,
        )

    atf.run_command(
        f"sacctmgr -i mod qos {qos1} set {limit}={input}",
        user=atf.properties["slurm-user"],
        fatal=True,
    )
    output = atf.run_command_output(
        f"sacctmgr show -nP qos {qos1} format={limit}",
        fatal=True,
    )
    assert (
        output.rstrip() == result
    ), f"QoS {qos1} should have {result}, not {output.rstrip()}"
