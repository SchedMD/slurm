##########################################################################
# Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
##########################################################################
import re

import pytest

import atf

fed_name = "fed"
# The names of the clusters, set by setup()
cls1 = cls2 = cls3 = None

user_name = None
node_count = 1


@pytest.fixture(scope="module", autouse=True)
def setup():
    global cls1, cls2, cls3
    cls1, cls2, cls3 = atf.require_multicluster(3, federation=True)
    for _ in atf.foreach_cluster():
        atf.require_nodes(1)
    atf.require_slurm_running()

    global user_name

    user_name = atf.get_user_name()

    # Module-level federation setup
    cancel_federation_jobs()

    # Add cluster features to the ATF-created federation
    for cluster, feature in [(cls1, "fa"), (cls2, "fb"), (cls3, "fc")]:
        atf.run_command(
            f"sacctmgr -i modify cluster {cluster} set features={feature}",
            user=atf.properties["slurm-user"],
            fatal=True,
        )
    atf.wait_for_federation_sync()

    yield

    cancel_federation_jobs()


def cancel_federation_jobs():
    """Cancel all user jobs across federation clusters."""
    if user_name:
        atf.run_command(f"scancel -M{cls1},{cls2},{cls3} --user {user_name}")
        atf.repeat_until(
            lambda: atf.run_command_output(
                f"squeue -M{cls1},{cls2},{cls3} --user {user_name} --noheader",
            ),
            lambda output: output.strip() == "",
            timeout=10,
        )


def submit_fed_batch_job(options="", output_file=None):
    """Submit a batch job through the federation. Returns job ID."""
    # Get node count
    sinfo_output = atf.run_command_output(
        "sinfo --local -h -o '%D' -t idle,alloc,comp",
        fatal=True,
    )
    total = 0
    for line in sinfo_output.strip().splitlines():
        line = line.strip()
        if line.isdigit():
            total += int(line)
    nc = max(total, 1)

    output_opt = f"--output={output_file}" if output_file else ""
    return atf.submit_job_sbatch(
        f"-N{nc} --exclusive {output_opt} {options} --wrap 'sleep infinity'",
        fatal=True,
    )


def fed_squeue():
    """Run squeue across all federation clusters and return output."""
    return atf.run_command_output(
        f"squeue --noheader -M{cls1},{cls2},{cls3}",
        fatal=True,
    )


def _count_notify_messages(output_file, c1, c2, c3):
    """Count 'error: from <cluster>' messages in a job output file."""
    try:
        with open(output_file) as f:
            content = f.read()
    except (FileNotFoundError, OSError):
        return 0
    return len(re.findall(rf"error: from ({c1}|{c2}|{c3})", content))


_srun_proc = None


@pytest.fixture(autouse=True)
def _cleanup_background_srun():
    yield
    global _srun_proc
    if _srun_proc is not None and _srun_proc.poll() is None:
        _srun_proc.kill()
        _srun_proc.wait()
    _srun_proc = None


def test_federated_scontrol_notify():
    """Test federated scontrol notify <message>.

    Verifies that:
    1. Batch jobs submitted to specific federation clusters via -M receive
       notify messages sent from each federation cluster
    2. Notify messages from all 3 clusters appear in batch job output files
    3. An srun job receives notify messages from all 3 clusters on stderr
    """
    cancel_federation_jobs()

    # Create input script for srun job
    file_in = atf.module_tmp_path / "input"
    atf.make_bash_script(file_in, "sleep infinity")

    # Submit batch jobs to specific clusters with explicit output paths
    output0 = atf.module_tmp_path / "slurm-jid0.out"
    output1 = atf.module_tmp_path / "slurm-jid1.out"
    jid0 = submit_fed_batch_job(f"-M{cls1}", output_file=output0)
    jid1 = submit_fed_batch_job(f"-M{cls2}", output_file=output1)

    # Wait for batch jobs to be running
    atf.wait_for_job_state(jid0, "RUNNING", siblings=True, fatal=True)
    atf.wait_for_job_state(jid1, "RUNNING", siblings=True, fatal=True)

    # The stderr of srun goes to a file, to see its notify messages at any time
    global _srun_proc
    srun_err = "srun.err"
    _srun_proc = atf.run_command(f"srun -v {file_in} 2> {srun_err}", background=True)[
        "process"
    ]
    for _ in atf.timer(fatal=True):
        output = atf.run_command_output(f"cat {srun_err}")
        if match := re.search(r"job(?:id)?[ =](\d+)", output, re.IGNORECASE):
            jid2 = int(match.group(1))
            break

    # Wait for srun job to be running on cls3, the only cluster not full
    atf.wait_for_job_state(jid2, "RUNNING", siblings=True, clusters=cls3, fatal=True)

    atf.wait_for_job_state(jid0, "RUNNING", clusters=cls1, timeout=60)
    atf.wait_for_job_state(jid1, "RUNNING", clusters=cls2, timeout=60)
    atf.wait_for_job_state(jid2, "RUNNING", clusters=cls3, timeout=60)

    # Verify all 3 jobs in squeue across clusters
    output = fed_squeue()
    assert re.search(
        rf"{cls1}\s+.*\b{jid0}\b", output
    ), f"Job {jid0} not found on {cls1} in squeue output"
    assert re.search(
        rf"{cls2}\s+.*\b{jid1}\b", output
    ), f"Job {jid1} not found on {cls2} in squeue output"
    assert re.search(
        rf"{cls3}\s+.*\b{jid2}\b", output
    ), f"Job {jid2} not found on {cls3} in squeue output"

    # Verify squeue output lists jobs in cluster order (cls1 < cls2 < cls3)
    lines = output.strip().splitlines()
    cluster_order = []
    for line in lines:
        for cluster in [cls1, cls2, cls3]:
            if cluster in line:
                cluster_order.append(cluster)
                break
    # Check that cluster appearances are in order
    expected_order = sorted(cluster_order, key=lambda c: [cls1, cls2, cls3].index(c))
    assert (
        cluster_order == expected_order
    ), f"squeue should list jobs in cluster order, got {cluster_order}"

    # Send notify from each cluster to jid0 and verify output file
    for cluster in [cls1, cls2, cls3]:
        atf.run_command(
            f"scontrol -M{cluster} notify {jid0} from {cluster}",
            fatal=True,
        )

    atf.repeat_until(
        lambda: _count_notify_messages(output0, cls1, cls2, cls3),
        lambda count: count == 3,
        timeout=30,
        fatal=True,
    )

    # Send notify from each cluster to jid1 and verify output file
    for cluster in [cls1, cls2, cls3]:
        atf.run_command(
            f"scontrol -M{cluster} notify {jid1} from {cluster}",
            fatal=True,
        )

    atf.repeat_until(
        lambda: _count_notify_messages(output1, cls1, cls2, cls3),
        lambda count: count == 3,
        timeout=30,
        fatal=True,
    )

    # Send notify from each cluster to srun job and verify on stderr
    for cluster in [cls1, cls2, cls3]:
        atf.run_command(
            f"scontrol -M{cluster} notify {jid2} from {cluster}",
            fatal=True,
        )
        expected = f"srun: from {cluster}"

        assert atf.repeat_until(
            lambda: atf.run_command_output(f"cat {srun_err}"),
            lambda output: expected in output,
            timeout=30,
        ), f"srun job {jid2} did not receive notify message '{expected}'"
