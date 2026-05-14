############################################################################
# Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
############################################################################
import atf
import pytest
import re


cluster = None


# Setup
@pytest.fixture(scope="module", autouse=True)
def setup():
    global cluster

    atf.require_version((26, 5), component="bin/sacctmgr")
    atf.require_nodes(1)
    atf.require_config_parameter("AccountingStorageType", "accounting_storage/slurmdbd")
    atf.require_config_parameter("LicenseParameters", "RemoteFuzzyMatch")
    atf.require_config_parameter("Licenses", "simple:100")

    atf.require_slurm_running()

    cluster = atf.get_config_parameter("ClusterName", quiet=True)


@pytest.fixture(scope="function", autouse=True)
def delete_resources():
    yield

    output = atf.run_command_output(
        "sacctmgr show resource -P -n Format=Name",
        user=atf.properties["slurm-user"],
        fatal=True,
    ).strip()

    resources = []
    for line in output.split("\n"):
        if not line:
            continue
        resources.append(line)

    if resources:
        atf.run_command(
            f"sacctmgr -i delete resource {' '.join(resources)}",
            user=atf.properties["slurm-user"],
            fatal=True,
        )


@pytest.fixture(scope="function")
def matlab_res(setup):
    res = "matlab"
    atf.run_command(
        f"sacctmgr -i create resource {res} server=slurmdb count=100 cluster={cluster} allowed=100",
        user=atf.properties["slurm-user"],
        fatal=True,
    )

    yield res

    atf.run_command(
        f"sacctmgr -i delete resource {res}",
        user=atf.properties["slurm-user"],
        fatal=True,
    )


@pytest.mark.parametrize(
    "resources,licenses",
    [
        (
            # fuzzy match rewrites to include server and implicit count
            "matlab",
            "matlab@slurmdb:1",
        ),
        (
            # explicit match: not rewritten, no implicit quantity
            "matlab@slurmdb",
            "matlab@slurmdb",
        ),
        (
            # fuzzy match with explicit quantity
            "matlab:5",
            "matlab@slurmdb:5",
        ),
    ],
)
def test_fuzzy_license_match(matlab_res, resources, licenses):
    """
    Licenses of a submitted job with --resources should be the expect ones
    """
    job_id = atf.submit_job_sbatch(f'--resources={resources} --wrap="hostname"')
    assert job_id != 0, "Job should be accepted with a valid license request"
    assert atf.get_job_parameter(job_id, "Licenses") == licenses


def test_local_license_preferred():
    """
    User should be able to submit a job with request "simple" and have it match
    slurm.conf defined "simple" instead of remote license "simple@slurmdb".
    """
    # create a remote license target resource
    atf.run_command(
        f"sacctmgr -i create resource simple server=slurmdb count=100 cluster={cluster} allowed=100",
        user=atf.properties["slurm-user"],
        fatal=True,
    )

    # submit job hoping to get local license
    job_id = atf.submit_job_sbatch('--resources=simple --wrap="hostname"')
    assert job_id != 0, "Job should be accepted with a valid var"

    ## NOTE fuzzy matcher function should be used which rewrites license
    ## string to simple:1 even though it is picking the local license
    assert atf.get_job_parameter(job_id, "Licenses") == "simple:1"


def test_match_explicit_remote_with_local_present():
    """
    User should be able to explicitly request a remote license "simple@slurmdb"
    even when a local license "simple" is also defined in slurm.conf. The explicit
    @server form is not rewritten and has no implicit quantity added.
    """
    # create a remote license target resource
    atf.run_command(
        f"sacctmgr -i create resource simple server=slurmdb count=100 cluster={cluster} allowed=100",
        user=atf.properties["slurm-user"],
        fatal=True,
    )

    # submit job explicitly requesting the remote license
    job_id = atf.submit_job_sbatch('--resources=simple@slurmdb --wrap="hostname"')
    assert job_id != 0, "Job should be accepted with a valid license request"

    ## NOTE explicit match: not rewritten, no implicit quantity added
    assert atf.get_job_parameter(job_id, "Licenses") == "simple@slurmdb"


def test_fuzzy_match_does_not_work_for_multiple_servers():
    """
    User should not be able to perform a fuzzy match if there are multiple
    entries that could match
    """
    # create a remote license target resource
    atf.run_command(
        f"sacctmgr -i create resource matlab server=remote1 count=100 cluster={cluster} allowed=100",
        user=atf.properties["slurm-user"],
        fatal=True,
    )
    atf.run_command(
        f"sacctmgr -i create resource matlab server=remote2 count=100 cluster={cluster} allowed=100",
        user=atf.properties["slurm-user"],
        fatal=True,
    )

    # submit job hoping to get local license
    result = atf.run_job("--resources=matlab hostname", xfail=True)
    assert result["exit_code"] != 0
    assert (
        "Unable to allocate resources: Invalid license specification"
        in result["stderr"]
    )


def test_fuzzy_match_does_not_work_for_partial_name(matlab_res):
    """
    User should not be able to perform a fuzzy match if only a substring of the
    prefix is supplied (e.g., "matla" should not match "matlab")
    """

    # submit job hoping to get local license
    result = atf.run_job("--resources=matla hostname", xfail=True)
    assert result["exit_code"] != 0
    assert (
        "Unable to allocate resources: Invalid license specification"
        in result["stderr"]
    )


def test_show_license_fuzzy_match(matlab_res):
    """
    User should be able to selectively show license information using fuzzy match
    """

    # display the license
    ret = atf.run_command(
        "scontrol show licenses=matlab",
        user=atf.properties["slurm-user"],
    )
    assert ret["exit_code"] == 0, "We should be able to selectively show license info"
    license_name = re.search(r"LicenseName=(\S*)", ret["stdout"]).group(1)
    assert license_name == "matlab@slurmdb"


def test_show_license_one_result_only():
    """
    To facilitate scriptable behaviors, ensure only one result is produced
    by `scontrol show license=<name>`.
    """

    # create a remote license target resource
    atf.run_command(
        f"sacctmgr -i create resource matlab server=slurmdb count=100 cluster={cluster} allowed=100",
        user=atf.properties["slurm-user"],
        fatal=True,
    )
    # create a remote license target resource
    atf.run_command(
        f"sacctmgr -i create resource matlab server=slurmdb2 count=100 cluster={cluster} allowed=100",
        user=atf.properties["slurm-user"],
        fatal=True,
    )

    # display the license
    ret = atf.run_command(
        "scontrol show licenses=matlab",
        user=atf.properties["slurm-user"],
        xfail=True,
    )
    assert ret["exit_code"] == 1
    assert (
        ret["stderr"].strip()
        == 'scontrol: error: query "matlab" matched more than one result, exiting.'
    )
    assert ret["stdout"] == ""


def test_show_license_invalid_query(matlab_res):
    """
    To facilitate scriptable behaviors, ensure only exit code non-zero if no
    matching licenses are found
    """

    # display the license
    ret = atf.run_command(
        "scontrol show licenses=invalid",
        user=atf.properties["slurm-user"],
        xfail=True,
    )
    assert ret["exit_code"] == 1
    assert (
        ret["stderr"].strip()
        == 'scontrol: error: query "invalid" matched zero licenses.'
    )
    assert ret["stdout"] == ""


def test_resv_license_fuzzy_match(matlab_res):
    """
    User should be able to create a reservation using a fuzzy-matched name
    """

    # create a reservation
    ret = atf.run_command(
        f"scontrol create reservation=resv user={atf.properties['slurm-user']} licenses=matlab:100 start=now duration=unlimited flags=ANY_NODES",
        user=atf.properties["slurm-user"],
    )
    assert ret["exit_code"] == 0

    # verify the reservation has the license
    assert atf.get_reservation_parameter("resv", "Licenses") == "matlab@slurmdb:100"

    show = atf.run_command_output("scontrol show licenses=matlab@slurmdb", fatal=True)
    resv_count = re.search(r"Reserved=(\S*)", show).group(1)
    assert resv_count == "100"

    # submit job using fuzzy match and reservation
    job_id = atf.submit_job_sbatch(
        '--resources=matlab:10 --reservation=resv --wrap="sleep infinity"',
        user=atf.properties["slurm-user"],
    )
    assert job_id != 0, "Job should be accepted with a valid var"
    atf.wait_for_job_state(job_id, "RUNNING", fatal=True)

    show = atf.run_command_output("scontrol show licenses=matlab@slurmdb", fatal=True)
    resv_count = re.search(r"Reserved=(\S*)", show).group(1)
    used_count = re.search(r"Used=(\S*)", show).group(1)
    assert resv_count == "100"
    assert used_count == "10"
