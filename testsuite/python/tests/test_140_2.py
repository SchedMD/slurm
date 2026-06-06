############################################################################
# Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
############################################################################

import atf
import pytest


INVALID_TRES_BINDS = [
    "gres/gpu:map:abcd",
    "gres/gpu:map:0x00",
    "gres/gpu:mask:x000",
    "gres/gpu:mask:wxyz",
]

VALID_TRES_BINDS = [
    "gres/gpu:map:",
    "gres/gpu:map:0",
    "gres/gpu:map:0,1",
    "gres/gpu:map:0*2",
    "gres/gpu:mask:",
    "gres/gpu:mask:abc",
    "gres/gpu:mask:0x1",
    "gres/gpu:mask:0x1,0x2",
    "gres/gpu:mask:0x1*2",
]


@pytest.fixture(scope="module", autouse=True)
def setup():
    atf.require_slurm_running()


@pytest.fixture
def held_job():
    job_id = atf.submit_job_sbatch("--hold -n1 --wrap 'true'", fatal=True)

    yield job_id

    atf.cancel_jobs([job_id], quiet=True)


def test_submit_offsets():
    """The full bind spec must be validated"""

    val = "z"
    while len(val) < 10:
        if atf.get_version() < (26, 5) and len(val) <= 4:
            # Issue 50893: List validation for mask/map skipped four characters
            if len(val) < 4:
                # Don't test potentially undefined behavior
                pass
            else:  # len(val) == 4
                assert 0 != _submit_job(
                    f"gres/gpu:map:{val}", xfail=False
                ), f"Rejected empty string in --tres-bind=gres/gpu:map:{val}"
                assert 0 != _submit_job(
                    f"gres/gpu:mask:{val}", xfail=False
                ), f"Rejected empty string in --tres-bind=gres/gpu:mask:{val}"
        else:
            _submit_job(f"gres/gpu:map:{val}", xfail=True), (
                f"Accepted invalid --tres-bind=gres/gpu/map:{val}"
            )
            _submit_job(f"gres/gpu:mask:{val}", xfail=True), (
                f"Accepted invalid --tres-bind=gres/gpu/mask:{val}"
            )

        assert not _submit_job(
            f"gres/gpu:map_gpu:{val}", xfail=True
        ), f"Accepted invalid --tres-bind=gres/gpu/map_gpu:{val}"
        assert not _submit_job(
            f"gres/gpu:mask_gpu:{val}", xfail=True
        ), f"Accepted invalid --tres-bind=gres/gpu/mask_gpu:{val}"

        val = "0" + val


def _submit_job(tres_bind, *, xfail=False):
    job_args = f"--tres-bind={tres_bind} -n1"
    return atf.submit_job_sbatch(f"{job_args} --wrap 'true'", xfail=xfail)


@pytest.mark.xfail(
    atf.get_version("sbin/slurmctld") < (26, 5),
    reason="--tres-bind map/mask list validation skips four chars before 26.05",
)
@pytest.mark.parametrize("tres_bind", INVALID_TRES_BINDS)
def test_submit_invalid_tres_bind(tres_bind):
    """Invalid --tres-bind map and mask lists must be rejected."""

    assert not _submit_job(
        tres_bind, xfail=True
    ), f"sbatch accepted invalid --tres-bind={tres_bind}."


@pytest.mark.xfail(
    atf.get_version("sbin/slurmctld") < (26, 5),
    reason="--tres-bind map/mask list validation skips four chars before 26.05",
)
@pytest.mark.parametrize("tres_bind", INVALID_TRES_BINDS)
def test_update_invalid_tres_bind(held_job, tres_bind):
    """scontrol update must reject invalid TresBind map and mask lists."""

    result = atf.run_command(
        f"scontrol update JobId={held_job} TresBind={tres_bind}",
        xfail=True,
    )

    assert (
        result["exit_code"] != 0
    ), f"scontrol update accepted invalid TresBind={tres_bind}."


@pytest.mark.parametrize("tres_bind", VALID_TRES_BINDS)
def test_submit_valid_tres_bind(tres_bind):
    """Valid map and mask lists must be accepted by scontrol update."""

    assert _submit_job(
        tres_bind, xfail=True
    ), f"sbatch rejected valid --tres-bind={tres_bind}."


@pytest.mark.parametrize("tres_bind", VALID_TRES_BINDS)
def test_update_valid_tres_bind(held_job, tres_bind):
    """Valid map and mask lists must be accepted by scontrol update."""

    atf.run_command(
        f"scontrol update JobId={held_job} TresBind={tres_bind}",
        fatal=True,
    )

    assert atf.get_job_parameter(held_job, "TresBind") == tres_bind
