############################################################################
# Copyright (C) SchedMD LLC.
############################################################################
import pytest
import atf
import os

# Ensure that job ids are not truncated
os.environ["SLURM_BITSTR_LEN"] = "0"


def resolve_array_job(job_id, task_id):
    return int(
        atf.run_command_output(
            f"squeue -j {job_id}_{task_id} --noheader --state=all --format=%A"
        ).strip()
    )


@pytest.fixture(scope="module", autouse=True)
def setup():
    # Test needs to run a het job with 3 components and 9 parallel tasks of an arrary
    # We need 9 nodes to be able to run with select_linear
    atf.require_nodes(9)
    atf.require_slurm_running()


@pytest.fixture(scope="function", autouse=True)
def cancel_jobs():
    yield
    atf.cancel_all_jobs()


def test_single_job():
    output = atf.run_command_output(
        f"squeue --noheader --only-job-state --format='%i=%T'"
    ).strip()
    assert output == ""

    job_id = atf.submit_job_sbatch("--wrap='srun sleep infinity'")
    atf.wait_for_step(job_id, 0, fatal=True, timeout=60)

    output = atf.run_command_output(
        f"squeue --noheader --only-job-state --format='%i=%T'"
    ).strip()
    assert output == f"{job_id}=RUNNING"

    output = atf.run_command_output(
        f"squeue --noheader --only-job-state --format='%i=%T' -j {job_id}"
    ).strip()
    assert output == f"{job_id}=RUNNING"

    output = atf.run_command_output(
        f"squeue --noheader --only-job-state --format='%i=%T' -j {job_id - 1}"
    ).strip()
    assert output == ""

    atf.cancel_jobs([job_id])
    atf.wait_for_job_state(job_id, "DONE", fatal=True, timeout=60)

    output = atf.run_command_output(
        f"squeue --noheader --only-job-state --format='%i=%T' -j {job_id}"
    ).strip()
    assert output == ""

    output = atf.run_command_output(
        f"squeue --noheader --only-job-state --format='%i=%T'"
    ).strip()
    assert output == ""

    output = atf.run_command_output(
        f"squeue --noheader --state=all --only-job-state --format='%i=%T' -j {job_id}"
    ).strip()
    assert output == f"{job_id}=CANCELLED"


def test_het_job():
    job_id = atf.submit_job_sbatch("-n1 : -n1 : -n1 --wrap='srun sleep infinity'")
    atf.wait_for_step(job_id, 0, fatal=True, timeout=60)

    output = (
        atf.run_command_output(f"squeue --noheader --only-job-state --format='%i=%T'")
        .strip()
        .splitlines()
    )
    assert f"{job_id}+0=RUNNING" in output
    assert f"{job_id}+1=RUNNING" in output
    assert f"{job_id}+2=RUNNING" in output
    assert len(output) == 3

    output = (
        atf.run_command_output(
            f"squeue --noheader --only-job-state --format='%i=%T' -j {job_id}"
        )
        .strip()
        .splitlines()
    )
    assert f"{job_id}+0=RUNNING" in output
    assert f"{job_id}+1=RUNNING" in output
    assert f"{job_id}+2=RUNNING" in output
    assert len(output) == 3

    output = (
        atf.run_command_output(
            f"squeue --noheader --only-job-state --format='%i=%T' -j {job_id - 1}"
        )
        .strip()
        .splitlines()
    )
    assert len(output) == 0

    atf.cancel_jobs([job_id])
    atf.wait_for_job_state(job_id, "DONE", fatal=True, timeout=60)

    output = (
        atf.run_command_output(
            f"squeue --noheader --only-job-state --format='%i=%T' -j {job_id}"
        )
        .strip()
        .splitlines()
    )
    assert len(output) == 0

    output = (
        atf.run_command_output(f"squeue --noheader --only-job-state --format='%i=%T'")
        .strip()
        .splitlines()
    )
    assert len(output) == 0

    output = (
        atf.run_command_output(
            f"squeue --state=all --noheader --only-job-state --format='%i=%T' -j {job_id}"
        )
        .strip()
        .splitlines()
    )
    assert f"{job_id}+0=CANCELLED" in output
    assert f"{job_id}+1=CANCELLED" in output
    assert f"{job_id}+2=CANCELLED" in output
    assert len(output) == 3


def test_array_job():
    job_id = atf.submit_job_sbatch("--array=1-10 --hold --wrap='srun sleep infinity'")
    atf.wait_for_job_state(job_id, "PENDING", fatal=True, timeout=60)

    output = atf.run_command_output(
        f"squeue --noheader --only-job-state --format='%i=%T'"
    ).strip()
    assert output == f"{job_id}_[1-10]=PENDING"

    atf.run_command(f"scontrol release {job_id}_5", fatal=True)

    j5_id = resolve_array_job(job_id, 5)

    atf.wait_for_job_state(j5_id, "RUNNING", fatal=True, timeout=60)

    output = (
        atf.run_command_output(f"squeue --noheader --only-job-state --format='%i=%T'")
        .strip()
        .splitlines()
    )
    assert f"{job_id}_[1-4,6-10]=PENDING" in output
    assert f"{job_id}_5=RUNNING" in output
    assert len(output) == 2

    output = (
        atf.run_command_output(
            f"squeue --noheader --only-job-state --format='%i=%T' -j {job_id}"
        )
        .strip()
        .splitlines()
    )
    assert f"{job_id}_[1-4,6-10]=PENDING" in output
    assert f"{job_id}_5=RUNNING" in output
    assert len(output) == 2

    output = (
        atf.run_command_output(
            f"squeue --noheader --only-job-state --format='%i=%T' -j {j5_id}"
        )
        .strip()
        .splitlines()
    )
    assert f"{job_id}_5=RUNNING" in output
    assert len(output) == 1

    output = (
        atf.run_command_output(
            f"squeue --noheader --only-job-state --format='%i=%T' -j {job_id}_5"
        )
        .strip()
        .splitlines()
    )
    assert f"{job_id}_5=RUNNING" in output
    assert len(output) == 1

    atf.cancel_jobs([j5_id])
    atf.wait_for_job_state(j5_id, "DONE", fatal=True, timeout=60)

    output = (
        atf.run_command_output(
            f"squeue --noheader --only-job-state --format='%i=%T' -j {job_id}"
        )
        .strip()
        .splitlines()
    )
    assert f"{job_id}_[1-4,6-10]=PENDING" in output
    assert len(output) == 1

    output = (
        atf.run_command_output(
            f"squeue --noheader --only-job-state --format='%i=%T' -j {job_id} --state=all"
        )
        .strip()
        .splitlines()
    )
    assert f"{job_id}_[1-4,6-10]=PENDING" in output
    assert f"{job_id}_5=CANCELLED" in output
    assert len(output) == 2

    output = (
        atf.run_command_output(
            f"squeue --noheader --only-job-state --format='%i=%T' -j {j5_id}"
        )
        .strip()
        .splitlines()
    )
    assert len(output) == 0

    output = (
        atf.run_command_output(
            f"squeue --noheader --only-job-state --format='%i=%T' -j {j5_id} --state=all"
        )
        .strip()
        .splitlines()
    )
    assert f"{job_id}_5=CANCELLED" in output
    assert len(output) == 1

    tasks = [1, 2, 3, 4, 6, 7, 8, 9, 10]
    atf.run_command(f"scontrol release {job_id}_[1-4,6-10]", fatal=True)
    for i in tasks:
        atf.wait_for_job_state(
            resolve_array_job(job_id, i), "RUNNING", fatal=True, timeout=60
        )

    output = (
        atf.run_command_output(f"squeue --noheader --only-job-state --format='%i=%T'")
        .strip()
        .splitlines()
    )
    for i in tasks:
        assert f"{job_id}_{i}=RUNNING" in output
    assert len(output) == len(tasks)

    output = (
        atf.run_command_output(
            f"squeue --noheader --only-job-state --format='%i=%T' -j {job_id}"
        )
        .strip()
        .splitlines()
    )
    for i in tasks:
        assert f"{job_id}_{i}=RUNNING" in output
    assert len(output) == len(tasks)

    for i in tasks:
        atf.wait_for_job_state(
            resolve_array_job(job_id, i), "RUNNING", fatal=True, timeout=60
        )

        # now check all array tasks
    tasks.append(5)

    atf.cancel_jobs([job_id])

    for i in tasks:
        atf.wait_for_job_state(
            resolve_array_job(job_id, i), "CANCELLED", fatal=True, timeout=60
        )

    output = (
        atf.run_command_output(
            f"squeue --noheader --only-job-state --format='%i=%T' --state=all -j {job_id}"
        )
        .strip()
        .splitlines()
    )
    for i in tasks:
        assert f"{job_id}_{i}=CANCELLED" in output
    assert len(output) == len(tasks)

    output = (
        atf.run_command_output(
            f"squeue --noheader --only-job-state --format='%i=%T' -j {job_id} --state=all"
        )
        .strip()
        .splitlines()
    )
    for i in tasks:
        assert f"{job_id}_{i}=CANCELLED" in output
    assert len(output) == len(tasks)


def test_all_jobs():
    job_id1 = atf.submit_job_sbatch(
        "--overcommit --oversubscribe --mem=0 --array=1-100 --hold --wrap='srun sleep infinity'"
    )
    job_id2 = atf.submit_job_sbatch(
        "--overcommit --oversubscribe --mem=0 --array=1-100:4 --hold --wrap='srun sleep infinity'"
    )
    job_id3 = atf.submit_job_sbatch(
        "--overcommit --oversubscribe --mem=0 -n1 : -n1 : -n1 : -n1 --hold --wrap='srun sleep infinity'"
    )
    job_id4 = atf.submit_job_sbatch(
        "--overcommit --oversubscribe --mem=0 -n6 --hold --wrap='srun sleep infinity'"
    )

    atf.wait_for_job_state(job_id1, "PENDING", fatal=True, timeout=60)
    atf.wait_for_job_state(job_id2, "PENDING", fatal=True, timeout=60)
    atf.wait_for_job_state(job_id3, "PENDING", fatal=True, timeout=60)
    atf.wait_for_job_state(job_id4, "PENDING", fatal=True, timeout=60)

    output = (
        atf.run_command_output(f"squeue --noheader --only-job-state --format='%i=%T'")
        .strip()
        .splitlines()
    )
    assert f"{job_id1}_[1-100]=PENDING" in output
    assert (
        f"{job_id2}_[1,5,9,13,17,21,25,29,33,37,41,45,49,53,57,61,65,69,73,77,81,85,89,93,97]=PENDING"
        in output
    )
    assert f"{job_id3}+0=PENDING" in output
    assert f"{job_id3}+1=PENDING" in output
    assert f"{job_id3}+2=PENDING" in output
    assert f"{job_id3}+3=PENDING" in output
    assert f"{job_id4}=PENDING" in output
    assert len(output) == 7

    job1_tasks = [2, 55, 74]
    atf.run_command(f"scontrol release {job_id1}_[2,55,74]", fatal=True)
    for i in job1_tasks:
        atf.wait_for_job_state(
            resolve_array_job(job_id1, i), "RUNNING", fatal=True, timeout=60
        )

    output = (
        atf.run_command_output(f"squeue --noheader --only-job-state --format='%i=%T'")
        .strip()
        .splitlines()
    )
    assert f"{job_id1}_[1,3-54,56-73,75-100]=PENDING" in output
    assert f"{job_id1}_2=RUNNING" in output
    assert f"{job_id1}_55=RUNNING" in output
    assert f"{job_id1}_74=RUNNING" in output
    assert (
        f"{job_id2}_[1,5,9,13,17,21,25,29,33,37,41,45,49,53,57,61,65,69,73,77,81,85,89,93,97]=PENDING"
        in output
    )
    assert f"{job_id3}+0=PENDING" in output
    assert f"{job_id3}+1=PENDING" in output
    assert f"{job_id3}+2=PENDING" in output
    assert f"{job_id3}+3=PENDING" in output
    assert f"{job_id4}=PENDING" in output
    assert len(output) == 10

    for i in job1_tasks:
        atf.cancel_jobs([resolve_array_job(job_id1, i)])

    job2_tasks = [13, 49, 65, 81]
    atf.run_command(f"scontrol release {job_id2}_[13,49,65,81]", fatal=True)
    for i in job2_tasks:
        atf.wait_for_job_state(
            resolve_array_job(job_id2, i), "RUNNING", fatal=True, timeout=60
        )

    output = (
        atf.run_command_output(f"squeue --noheader --only-job-state --format='%i=%T'")
        .strip()
        .splitlines()
    )
    assert f"{job_id1}_[1,3-54,56-73,75-100]=PENDING" in output
    assert (
        f"{job_id2}_[1,5,9,17,21,25,29,33,37,41,45,53,57,61,69,73,77,85,89,93,97]=PENDING"
        in output
    )
    assert f"{job_id2}_13=RUNNING" in output
    assert f"{job_id2}_49=RUNNING" in output
    assert f"{job_id2}_65=RUNNING" in output
    assert f"{job_id2}_81=RUNNING" in output
    assert f"{job_id3}+0=PENDING" in output
    assert f"{job_id3}+1=PENDING" in output
    assert f"{job_id3}+2=PENDING" in output
    assert f"{job_id3}+3=PENDING" in output
    assert f"{job_id4}=PENDING" in output
    assert len(output) == 11

    for i in job2_tasks:
        atf.cancel_jobs([resolve_array_job(job_id2, i)])

    atf.run_command(f"scontrol release {job_id3}", fatal=True)
    atf.wait_for_job_state(job_id3, "RUNNING", fatal=True, timeout=60)

    atf.run_command(f"scontrol release {job_id4}", fatal=True)
    atf.wait_for_job_state(job_id4, "RUNNING", fatal=True, timeout=60)

    output = (
        atf.run_command_output(f"squeue --noheader --only-job-state --format='%i=%T'")
        .strip()
        .splitlines()
    )
    assert f"{job_id1}_[1,3-54,56-73,75-100]=PENDING" in output
    assert (
        f"{job_id2}_[1,5,9,17,21,25,29,33,37,41,45,53,57,61,69,73,77,85,89,93,97]=PENDING"
        in output
    )
    assert f"{job_id3}+0=RUNNING" in output
    assert f"{job_id3}+1=RUNNING" in output
    assert f"{job_id3}+2=RUNNING" in output
    assert f"{job_id3}+3=RUNNING" in output
    assert f"{job_id4}=RUNNING" in output
    assert len(output) == 7
