############################################################################
# Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
############################################################################
"""Overcommit with GRES or mixed tres-per-task.

Ticket 25536: overcommit placement must remain disabled when tres_per_task
includes gres or other non-cpu TRES, even if cpu=N is also present. Only a
cpu-only tres_per_task (e.g. from -cN) is allowed to keep the overcommit
placement path.
"""

import pytest

import atf


@pytest.fixture(scope="module", autouse=True)
def setup():
    atf.require_config_parameter_includes("GresTypes", "gpu")
    atf.require_config_parameter("SelectType", "select/cons_tres")
    atf.require_config_parameter("SelectTypeParameters", "CR_CPU")
    atf.require_nodes(1, [("Gres", "gpu:2"), ("CPUs", 1)])
    for tty_num in range(2):
        atf.require_tty(tty_num)
    atf.require_config_parameter(
        "Name", {"gpu": {"File": "/dev/tty[0-1]"}}, source="gres"
    )
    atf.require_slurm_running()


def test_overcommit_gres_per_task():
    """Verify a gres-only tres-per-task request is not overcommit-placed.

    On the one-CPU node the two tasks need two CPUs; with overcommit
    correctly disabled for gres tres_per_task the request cannot be
    satisfied, so sbatch rejects it at submit.
    """
    atf.run_command(
        "sbatch -N1 -n2 -O --tres-per-task=gres/gpu:1 -t1 --wrap '/bin/true'",
        xfail=True,
        fatal=True,
    )


def test_overcommit_mixed_tres_per_task():
    """Verify mixed cpu+gres tres-per-task keeps overcommit placement disabled.

    The cpu=1 component alone would keep the overcommit path, but the gres
    component must disable it, so the job is accepted yet never placed.
    """
    job_id = atf.submit_job_sbatch(
        "-N1 -n2 -O --tres-per-task=cpu=1,gres/gpu:1 -t1 --wrap '/bin/true'",
        fatal=True,
    )
    # Bounded negative wait: the job must never complete, so cap the wait
    # instead of using atf's longer default.
    assert not atf.wait_for_job_state(
        job_id, "COMPLETED", timeout=15
    ), "mixed tres_per_task must not get the overcommit placement path"
