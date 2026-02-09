##########################################################################
# Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
##########################################################################
import re

import pytest

import atf

pytestmark = pytest.mark.slow

fed_name = "fed"
# The names of the clusters, set by setup()
cls1 = cls2 = cls3 = None

user_name = None


@pytest.fixture(scope="module", autouse=True)
def setup():
    global cls1, cls2, cls3
    cls1, cls2, cls3 = atf.require_multicluster(3, federation=True)
    for _ in atf.foreach_cluster():
        atf.require_nodes(1)
        # This test checks the federated view of sacct
        atf.require_config_parameter_includes("FederationParameters", "fed_display")
    atf.require_slurm_running()


@pytest.fixture(scope="module")
def federation_setup():
    """Module-level federation setup and cleanup."""
    global user_name

    user_name = atf.get_user_name()

    cancel_federation_jobs()

    # Add cluster features to the ATF-created federation
    for cluster, feature in [(cls1, "fa"), (cls2, "fb"), (cls3, "fc")]:
        output = atf.run_command_output(
            f"sacctmgr -i modify cluster {cluster} set features={feature}",
            user=atf.properties["slurm-user"],
            fatal=True,
        )
        assert (
            "Setting" in output
        ), f"Expected 'Setting' in sacctmgr output for {cluster}"
        assert re.search(
            rf"Feature\s+=\s+{feature}", output
        ), f"Expected 'Feature = {feature}' in sacctmgr output for {cluster}"
        assert (
            "Modified cluster" in output
        ), f"Expected 'Modified cluster' in sacctmgr output for {cluster}"
    atf.wait_for_federation_sync()

    yield

    cancel_federation_jobs()


def cancel_federation_jobs():
    """Cancel all federation jobs for the test user."""
    if user_name:
        atf.run_command(f"scancel -M{cls1},{cls2},{cls3} --user {user_name}")
        atf.repeat_until(
            lambda: atf.run_command_output(
                f"squeue -M{cls1},{cls2},{cls3} --user {user_name} --noheader",
            ),
            lambda output: output.strip() == "",
            timeout=10,
        )


def build_regex(jid, cid, state):
    """Build regex pattern for matching sacct output lines.

    Matches: <job_id>...<state><whitespace><cluster>
    Based on sacct format: jobid,submit,start,end,jobname,account,alloccpus,state,cluster
    """
    return rf"{jid}.+{state}\s+{cid}"


def run_sacct(job_ids, options=""):
    """Run sacct with given options and return output."""
    job_list = ",".join(str(j) for j in job_ids)
    return atf.run_command_output(
        f"sacct --noheader"
        f" -o jobid,submit,start,end,jobname,account,alloccpus,state,cluster"
        f" -j {job_list} {options}",
        fatal=True,
    )


@pytest.mark.usefixtures("federation_setup")
def test_federated_sacct_output():
    """Test federated sacct output.

    Submits jobs to specific federation clusters using --cluster-constraint,
    then verifies:
    1. sacct shows each job RUNNING on its assigned cluster
    2. sacct -D additionally shows REVOKED sibling records on the origin cluster
    3. After completion, sacct shows COMPLETED states with same patterns
    """
    # Get node count for exclusive allocation
    output = atf.run_command_output("sinfo -h -o %D --local", fatal=True)
    node_count = output.strip().split("\n")[0]

    # Create test script
    test_script = atf.module_tmp_path / "sacct_test.sh"
    atf.make_bash_script(str(test_script), "sleep 60")

    cancel_federation_jobs()

    # Submit 3 jobs, each constrained to a different cluster
    ji0 = atf.submit_job_sbatch(
        f"-N{node_count} --exclusive --output=/dev/null"
        f" --error=/dev/null --cluster-constraint=fa {test_script}",
        fatal=True,
    )

    ji1 = atf.submit_job_sbatch(
        f"-N{node_count} --exclusive --output=/dev/null"
        f" --error=/dev/null --cluster-constraint=fb {test_script}",
        fatal=True,
    )

    ji2 = atf.submit_job_sbatch(
        f"-N{node_count} --exclusive --output=/dev/null"
        f" --error=/dev/null --cluster-constraint=fc {test_script}",
        fatal=True,
    )

    job_ids = [ji0, ji1, ji2]

    # Wait for all jobs to start and track which cluster each runs on
    jid = {}
    for job_id in job_ids:
        atf.wait_for_job_state(job_id, "RUNNING", siblings=True, fatal=True)
        running = atf.get_job_clusters(job_id, "RUNNING")
        assert len(running) == 1, f"Job {job_id} should run on one cluster: {running}"
        jid[running[0]] = job_id

    assert cls1 in jid, f"No job running on {cls1}"
    assert cls2 in jid, f"No job running on {cls2}"
    assert cls3 in jid, f"No job running on {cls3}"

    # Build RUNNING regex patterns for each job on its assigned cluster
    runr1 = build_regex(str(jid[cls1]), cls1, "RUNNING")
    runr2 = build_regex(str(jid[cls2]), cls2, "RUNNING")
    runr3 = build_regex(str(jid[cls3]), cls3, "RUNNING")

    # REVOKED sibling patterns: jobs submitted from cls1 but routed to
    # cls2/cls3 leave REVOKED sibling records on cls1
    runrv1 = build_regex(str(jid[cls2]), cls1, "REVOKED")
    runrv2 = build_regex(str(jid[cls3]), cls1, "REVOKED")

    # Verify sacct shows all 3 jobs RUNNING on their clusters, and without
    # the duplicated REVOKED records (see -D in sacct.1)
    def check_sacct_running():
        output = run_sacct(job_ids)
        return (
            "REVOKED" not in output
            and re.search(runr1, output)
            and re.search(runr2, output)
            and re.search(runr3, output)
        )

    assert atf.repeat_until(
        check_sacct_running, lambda x: x, timeout=60
    ), "sacct did not show all 3 jobs RUNNING on their clusters (or showed REVOKED records)"

    # Verify sacct -D also shows REVOKED sibling records on cls1
    def check_sacct_d_running():
        output = run_sacct(job_ids, "-D")
        if not (
            re.search(runr1, output)
            and re.search(runr2, output)
            and re.search(runr3, output)
            and re.search(runrv1, output)
            and re.search(runrv2, output)
        ):
            return False
        # Verify exactly 2 REVOKED records
        revoked_count = len(re.findall(r"REVOKED", output))
        return revoked_count == 2

    assert atf.repeat_until(
        check_sacct_d_running, lambda x: x, timeout=60
    ), "sacct -D did not show RUNNING jobs, REVOKED siblings, or wrong REVOKED count"

    # Wait for all jobs to complete
    for job_id in job_ids:
        atf.wait_for_job_state(job_id, "DONE", siblings=True, timeout=120, fatal=True)

    # Build COMPLETED regex patterns
    compr1 = build_regex(str(jid[cls1]), cls1, "COMPLETED")
    compr2 = build_regex(str(jid[cls2]), cls2, "COMPLETED")
    compr3 = build_regex(str(jid[cls3]), cls3, "COMPLETED")

    # Verify sacct shows all 3 jobs COMPLETED, and without the duplicated
    # REVOKED records
    def check_sacct_completed():
        output = run_sacct(job_ids)
        return (
            "REVOKED" not in output
            and re.search(compr1, output)
            and re.search(compr2, output)
            and re.search(compr3, output)
        )

    assert atf.repeat_until(
        check_sacct_completed, lambda x: x, timeout=60
    ), "sacct did not show all 3 jobs COMPLETED on their clusters (or showed REVOKED records)"

    # Verify sacct -D shows COMPLETED + REVOKED siblings
    def check_sacct_d_completed():
        output = run_sacct(job_ids, "-D")
        if not (
            re.search(compr1, output)
            and re.search(compr2, output)
            and re.search(compr3, output)
            and re.search(runrv1, output)
            and re.search(runrv2, output)
        ):
            return False
        # Verify exactly 2 REVOKED records
        revoked_count = len(re.findall(r"REVOKED", output))
        return revoked_count == 2

    assert atf.repeat_until(
        check_sacct_d_completed, lambda x: x, timeout=60
    ), "sacct -D did not show COMPLETED jobs, REVOKED siblings, or wrong REVOKED count"
