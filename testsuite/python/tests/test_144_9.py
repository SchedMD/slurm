############################################################################
# Copyright (C) SchedMD LLC.
############################################################################
import atf
import pytest
import re

sock_0_reserved_cores = {0, 1, 2, 3}
sock_0_regular_cores = {4, 5}
sock_1_reserved_cores = {6, 7, 8, 9}
sock_1_regular_cores = {10, 11}


# Setup
@pytest.fixture(scope="module", autouse=True)
def setup():
    atf.require_auto_config("wants to create custom gpu files and custom gres")
    atf.require_config_parameter("SelectType", "select/cons_tres")
    atf.require_config_parameter(
        "SelectTypeParameters", "CR_CORE,MULTIPLE_SHARING_GRES_PJ"
    )
    atf.require_config_parameter("GresTypes", "gpu,shard")
    atf.require_nodes(
        2,
        [
            ("Gres", "gpu:4,shard:40"),
            ("Sockets", 2),
            ("CoresPerSocket", 6),
            ("ThreadsPerCore", 1),
            ("RestrictedCoresPerGPU", 2),
        ],
    )

    for tty_num in range(8):
        atf.require_tty(tty_num)

    atf.require_config_parameter(
        "Name",
        "gpu Cores=0-5 File=/dev/tty[0-1]\nName=gpu Cores=6-11 File=/dev/tty[2-3]",
        source="gres",
    )
    atf.require_slurm_running()


def test_config():
    expected = "2(0-3,6-9)"
    res_cores = atf.run_command_output(
        "scontrol show nodes node1 | awk -F 'RestrictedCoresPerGPU=' '{print $2}' | tr -d ' \n'",
        fatal=True,
    )
    assert (
        expected == res_cores
    ), f"RestrictedCoresPerGPU config not what expected. {res_cores}(scontrol output) != {expected}(expected)\n"


ok_test_parameters = [
    (
        "-N1 -n1 ",
        0,
        1,
        0,
        0,
        "",
    ),
    (
        "-N1 -n4 ",
        0,
        2,
        0,
        2,
        "",
    ),
    (
        "-N2 -n8 ",
        0,
        2,
        0,
        2,
        "",  # Second node
    ),
    (
        "-N2 -n3 -c2 ",
        0,
        2,
        0,
        0,
        "",  # Second node
    ),
    (
        "-N1 -n1 --gres=gpu:1 ",
        1,
        0,
        0,
        0,
        "gpu:1(IDX:0)",
    ),
    (
        "-N1 -n4 --gres=gpu:1 ",
        2,
        2,
        0,
        0,
        "gpu:1(IDX:0)",
    ),
    (  # Try to allocate all Cores
        "-N1 -c12 --gres=gpu:4 ",
        4,
        2,
        4,
        2,
        "gpu:4(IDX:0-3)",
    ),
    (
        "-N1 -n4 --tres-per-task=gres/gpu:1 ",
        2,
        0,
        2,
        0,
        "gpu:4(IDX:0-3)",
    ),
    (
        "-N2 -n5 --tres-per-task=gres/gpu:1 ",
        1,
        0,
        0,
        0,
        "gpu:1(IDX:0)",  # Second node
    ),
    (
        "-N1 -n1 -c3 --tres-per-task=gres/gpu:1 ",
        2,
        1,
        0,
        0,
        "gpu:1(IDX:0)",
    ),
    (
        "-N1 -n3 -c2 --tres-per-task=gres/gpu:1 ",
        2,
        1,
        3,  # May warrant a fix. Ideally should be 2,0,4,0
        0,
        "gpu:3(IDX:0,2-3)",
    ),
    # Note: There is no guarantee that reserved cores are used in proportion to shared gres allocations
    pytest.param(  # Test allocating all shards with 1 cores (see Note above)
        "-N1 --gres=shard:40 ",
        1,
        0,
        0,
        0,
        "shard:40(10/10,10/10,10/10,10/10)",
        marks=pytest.mark.xfail(
            atf.get_version() < (25, 11),
            reason="Ticket 22391: ResCoresPerGPU with shard support",
        ),
    ),
    pytest.param(  # Test allocating all cores with 4 shards
        "-N1 -n1 -c12 --gres=shard:4 ",
        4,
        2,
        4,
        2,
        "shard:4(4/10,0/10,0/10,0/10)",
        marks=pytest.mark.xfail(
            atf.get_version() < (25, 11),
            reason="Ticket 22391: ResCoresPerGPU with shard support",
        ),
    ),
    pytest.param(
        "-N1 -n1 -c1 --tres-per-task=gres/shard:1 ",
        1,
        0,
        0,
        0,
        "shard:1(1/10,0/10,0/10,0/10)",
        marks=pytest.mark.xfail(
            atf.get_version() < (25, 11),
            reason="Ticket 22391: ResCoresPerGPU with shard support",
        ),
    ),
    pytest.param(
        "-N1 -n3 -c2 --tres-per-task=gres/shard:5 ",
        4,
        2,
        0,
        0,
        "shard:15(10/10,5/10,0/10,0/10)",
        marks=pytest.mark.xfail(
            atf.get_version() < (25, 11),
            reason="Ticket 22391: ResCoresPerGPU with shard support",
        ),
    ),
    pytest.param(
        "-N1 -n1 -c8 --tres-per-task=gres/shard:10 ",
        4,
        2,
        2,
        0,
        "shard:10(10/10,0/10,0/10,0/10)",
        marks=pytest.mark.xfail(
            atf.get_version() < (25, 11),
            reason="Ticket 22391: ResCoresPerGPU with shard support",
        ),
    ),
    pytest.param(
        "-N1 -n1 -c1 --gres=shard:1 ",
        1,
        0,
        0,
        0,
        "shard:1(1/10,0/10,0/10,0/10)",
        marks=pytest.mark.xfail(
            atf.get_version() < (25, 11),
            reason="Ticket 22391: ResCoresPerGPU with shard support",
        ),
    ),
    pytest.param(
        "-N1 -n2 -c3 --gres=shard:3 ",
        4,
        2,
        0,
        0,
        "shard:3(3/10,0/10,0/10,0/10)",
        marks=pytest.mark.xfail(
            atf.get_version() < (25, 11),
            reason="Ticket 22391: ResCoresPerGPU with shard support",
        ),
    ),
    pytest.param(
        "-N2 -n2 --gres=shard:5 ",
        1,
        0,
        0,
        0,
        "shard:5(5/10,0/10,0/10,0/10)",  # Second node
        marks=pytest.mark.xfail(
            atf.get_version() < (25, 11),
            reason="Ticket 22391: ResCoresPerGPU with shard support",
        ),
    ),
]


@pytest.mark.parametrize(
    "job_args,s0_res,s0_reg,s1_res,s1_reg,gres",
    ok_test_parameters,
    ids=[
        param.values[0].strip() if hasattr(param, "values") else param[0].strip()
        for param in ok_test_parameters
    ],
)
def test_ok(job_args, s0_res, s0_reg, s1_res, s1_reg, gres):
    job_str = f'{job_args} --wrap "sleep infinity"'
    job_id = atf.submit_job_sbatch(job_str, fatal=True, quiet=False)
    atf.wait_for_job_state(job_id, "RUNNING", fatal=True)
    job_dict = atf.get_job(job_id)
    atf.cancel_all_jobs(quiet=True)
    atf.wait_for_job_state(job_id, "CANCELLED")

    assert str(job_dict["GRES"]) == gres, f"The job should have this gres: {gres}"

    cores = set(atf.range_to_list(job_dict["CPU_IDs"]))

    print(s0_res, s0_reg, s1_res, s1_reg)

    assert s0_res == len(
        cores & sock_0_reserved_cores
    ), f"The job should have {s0_res} reserved cores on socket 0. We got CPU_IDs={job_dict['CPU_IDs']}"
    assert s0_reg == len(
        cores & sock_0_regular_cores
    ), f"The job should have {s0_reg} not reserved cores on socket 0. We got CPU_IDs={job_dict['CPU_IDs']}"
    assert s1_res == len(
        cores & sock_1_reserved_cores
    ), f"The job should have {s1_res} reserved cores on socket 1. We got CPU_IDs={job_dict['CPU_IDs']}"
    assert s1_reg == len(
        cores & sock_1_regular_cores
    ), f"The job should have {s1_reg} not reserved cores on socket 1. We got CPU_IDs={job_dict['CPU_IDs']}"


array_test_parameters = [
    (  # Test with no gres, should use only regular cores
        "--array=[0-100] -N1 -n1 --nodelist=node1 ",
        4,
    ),
    (  # Test with gres gpu, should use all gpus
        "--array=[0-100] -N1 -n1 --nodelist=node1 --gres=gpu:1 ",
        4,
    ),
    pytest.param(  # Test with gres shard, should use all cores
        "--array=[0-100] -N1 -n1 --nodelist=node1 --gres=shard:1 ",
        12,
        marks=pytest.mark.xfail(
            atf.get_version() < (25, 11),
            reason="Ticket 22391: ResCoresPerGPU with shard support",
        ),
    ),
    pytest.param(  # Test with gres shard, should use all shards
        "--array=[0-100] -N1 -n1 --nodelist=node1 --gres=shard:5 ",
        8,
        marks=pytest.mark.xfail(
            atf.get_version() < (25, 11),
            reason="Ticket 22391: ResCoresPerGPU with shard support",
        ),
    ),
]


@pytest.mark.parametrize(
    "job_args,running_count",
    array_test_parameters,
    ids=[
        param.values[0].strip() if hasattr(param, "values") else param[0].strip()
        for param in array_test_parameters
    ],
)
def test_array(job_args, running_count):
    job_str = f'{job_args} --wrap "sleep infinity"'
    job_id = atf.submit_job_sbatch(job_str, fatal=True, quiet=False)
    atf.wait_for_node_state_any("node1", ["ALLOCATED", "MIXED"], fatal=True)
    count = int(atf.run_command_output("squeue --noheader --state=Running| wc -l"))
    atf.cancel_all_jobs(quiet=True)
    atf.wait_for_job_state(job_id, "CANCELLED")

    print(count, running_count)

    assert (
        count == running_count
    ), f"The job array should have {running_count} running tasks"


fail_test_parameters = [
    (
        "-N1 -n3 -c2",
        r"srun: error: .+ Requested node configuration is not available",
    ),
    (  # Try allocating more cores than GPUs * ReservedCoresPerGPU + regular cores
        "-N1 -n4 -c2 --gres=gpu:1",
        r"srun: error: .+ Requested node configuration is not available",
    ),
    (
        "-N1 -n5 -c1",
        r"srun: error: .+ Requested node configuration is not available",
    ),
    (
        "-N2 -n5 -c2",
        r"srun: error: .+ Requested node configuration is not available",
    ),
    (
        "-N2 -n9 -c1",
        r"srun: error: .+ Requested node configuration is not available",
    ),
    # You need at least 1 shard per GPU if you want all Cores despite the fact
    # that they may be on the same GPU. Fully requiring GPU usage to get the
    # cores would be difficult and beyond current scope.
    # This is a quirk of the current implementation of shard allocation with
    # ReservedCoresPerGPU
    (
        "-N1 -n1 -c12 --gres=shard:1",
        r"srun: error: .+ Requested node configuration is not available",
    ),
]


@pytest.mark.parametrize(
    "job_args,expected_msg",
    fail_test_parameters,
    ids=[p[0].strip() for p in fail_test_parameters],
)
def test_fail(job_args, expected_msg):
    output = atf.run_command(
        f"srun {job_args} shostname",
        timeout=1,
        fatal=False,
    )
    assert output["exit_code"] != 0, (
        f"Expected non-zero exit code for job_args: '{job_args}', because of ReservedCoresPerGPU.\n"
        f"STDERR: {output['stderr']}"
    )
    if re.search(expected_msg, str(output["stderr"])) is None:
        raise AssertionError(
            f"STDERR did not match expected regex.\n"
            f"job_args: {job_args}\n"
            f"Expected regex: {expected_msg}\n"
            f"Actual STDERR: {output['stderr']}"
        )
