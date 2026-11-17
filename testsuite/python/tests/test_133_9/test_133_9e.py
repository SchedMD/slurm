##########################################################################
# Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
##########################################################################
import re

import pytest

import atf

from .conftest import (
    cls1,
    cls2,
    cls3,
    run_scontrol_test,
    submit_fed_job,
    wait_for_running_cluster,
    wait_for_steps_started,
)

pytestmark = pytest.mark.slow


def test_federated_scontrol_output(federation_setup, cleanup):
    """Test federated scontrol show jobs output."""
    # ji3 stays federation-wide for the --sibling test below.
    ji0 = submit_fed_job(cls1)
    atf.wait_for_job_state(ji0, "RUNNING", siblings=True, clusters=cls1, fatal=True)
    ji1 = submit_fed_job(cls2)
    atf.wait_for_job_state(ji1, "RUNNING", siblings=True, clusters=cls2, fatal=True)
    ji2 = submit_fed_job(cls3)
    atf.wait_for_job_state(ji2, "RUNNING", siblings=True, clusters=cls3, fatal=True)
    ji3 = submit_fed_job()

    # ji3 stays pending, so it has no steps to wait for
    wait_for_steps_started({ji0: cls1, ji1: cls2, ji2: cls3})

    tasks = f"({ji0}|{ji1}|{ji2}|{ji3})"
    regex_j = f"(JobId=({ji0}|{ji1}|{ji2}|{ji3}) JobName=\\S+\\s*\\n?)"

    run_scontrol_test("jobs", tasks, regex_j + "{4}", "")
    run_scontrol_test("jobs", tasks, regex_j + "{6}", "--sibling")

    for cluster in [cls1, cls2, cls3]:
        run_scontrol_test("jobs", tasks, regex_j + "{2}", f"-M{cluster}")

    # Cluster-specific scontrol job/step mapping: verify each cluster shows exactly
    # the right jobs (1 RUNNING + 1 PENDING for the pending job ji3)
    cluster_map = {
        ji0: wait_for_running_cluster(ji0),
        ji1: wait_for_running_cluster(ji1),
        ji2: wait_for_running_cluster(ji2),
    }
    for cluster in [cls1, cls2, cls3]:
        result = atf.run_command_output(
            f"scontrol -M{cluster} show jobs -a", fatal=True
        )
        # The running job on this cluster should be present
        running_job = [jid for jid, c in cluster_map.items() if c == cluster]
        for jid in running_job:
            assert re.search(
                rf"JobId={jid}\b", result
            ), f"Running job {jid} not found on {cluster}:\n{result}"

    # With stepmgr the steps are only shown when the job is given (see squeue.1).
    # The job of the local cluster first, as the ones of the others fail below.
    for jid in [ji0, ji1, ji2]:
        if jid != ji0 and atf.config_parameter_includes(
            "SlurmctldParameters", "enable_stepmgr"
        ):
            pytest.xfail(
                "Issue 51550: the federated view of the steps of a job of another"
                " cluster misses the ones of its stepmgr"
            )
        regex_s = f"(StepId={jid}\\.[0-1].+ TimeLimit=UNLIMITED\\s*\\n?)"
        run_scontrol_test(f"steps {jid}", tasks, regex_s + "{2}", "")
        run_scontrol_test(f"steps {jid}", tasks, regex_s + "{2}", "--sibling")
