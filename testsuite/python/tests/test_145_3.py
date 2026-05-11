############################################################################
# Copyright (C) SchedMD LLC.
############################################################################

import atf
import pytest


# Features used throughout the file. Kept as module-level constants so the
# setup fixture and the individual tests stay in sync.
FEATURE_A = "fa"
FEATURE_B = "fb"

QOS_A = "test_qos_low"


@pytest.fixture(scope="module", autouse=True)
def setup():
    """Provision nodes with the features this file relies on.

    We need at least:
      - one node that has FEATURE_A only,
      - one node that has FEATURE_B only,
      - one node that has both FEATURE_A and FEATURE_B (for AND tests).
    """
    atf.require_accounting(modify=True)
    atf.require_config_parameter_includes("AccountingStorageEnforce", "qos")
    atf.require_config_parameter_includes("AccountingStorageEnforce", "associations")
    atf.require_config_parameter("PreemptType", "preempt/qos")
    atf.require_config_parameter("PreemptMode", "REQUEUE")
    atf.require_nodes(1, [("CPUs", 1), ("Features", FEATURE_A)])
    atf.require_nodes(1, [("CPUs", 1), ("Features", FEATURE_B)])
    atf.require_slurm_running()

    cluster = atf.get_config_parameter("ClusterName")
    user = atf.properties["test-user"]
    su = atf.properties["slurm-user"]
    atf.run_command(
        f"sacctmgr -i add qos {QOS_A} Preempt=normal",
        user=su,
        fatal=True,
    )
    atf.run_command(
        f"sacctmgr -i add user {user} cluster={cluster} account=root "
        f"qos=normal,{QOS_A} 2>/dev/null || "
        f"sacctmgr -i modify user {user} where cluster={cluster} "
        f"set qos=normal,{QOS_A}",
        user=su,
        fatal=True,
    )

    yield

    atf.run_command(
        f"sacctmgr -i modify user {user} where cluster={cluster} set qos=normal",
        user=su,
        quiet=True,
    )

    atf.run_command(
        f"sacctmgr -i remove qos {QOS_A}",
        user=su,
        quiet=True,
    )


def test_jobs_with_constraints_do_not_preempt():
    """Submit two jobs with constraints and verify that the second job does not
    preempt the first job"""
    job_a = atf.submit_job_sbatch(
        f'-J jobA -n1 -C {FEATURE_A} -qnormal --wrap "sleep 120"', fatal=True
    )
    atf.wait_for_job_state(job_a, "RUNNING", timeout=30, fatal=True)
    job_b = atf.submit_job_sbatch(
        f'-J jobB -n1 -C "[{FEATURE_A}|{FEATURE_B}]" -q{QOS_A} --wrap "sleep 120"',
        fatal=True,
    )
    atf.wait_for_job_state(job_b, "RUNNING", timeout=30, fatal=True)
    assert job_a > 0 and job_b > 0, "Both jobs should be successfully submitted"

    try:
        state = atf.get_job_parameter(job_a, "JobState")
        assert state == "RUNNING", f"Job {job_a} is in state {state}, expected RUNNING"
    finally:
        atf.cancel_jobs([job_a, job_b], fatal=False, quiet=True)
