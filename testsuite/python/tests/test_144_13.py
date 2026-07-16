############################################################################
# Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
############################################################################
"""
Verify CountOnly GRES requests do not alter per-socket core distribution.

Ticket 25194: jobs requesting a GRES defined with Flags=CountOnly used to
have their -m/--distribution requirement ignored at allocation time, because
the topology gres filter applied socket/core selection rules to a GRES that
has no physical device. The fix short-circuits the filter for CountOnly GRES.

The test compares the per-socket distribution of an identical allocation
submitted with and without a CountOnly GRES request, under several
-m/--distribution settings; the two distributions must match.
"""

import pytest

import atf

SOCKETS = 2
CORES_PER_SOCKET = 4
TASKS = 4


@pytest.fixture(scope="module", autouse=True)
def setup():
    atf.require_version(
        (26, 5),
        "sbin/slurmctld",
        reason="Ticket 25194: CountOnly GRES topology bypass added in 26.05",
    )
    atf.require_auto_config("wants to configure a CountOnly GRES on the node")
    atf.require_config_parameter("GresTypes", "bandwidth")
    atf.require_nodes(
        1,
        [
            ("Gres", "bandwidth:100"),
            ("Sockets", SOCKETS),
            ("CoresPerSocket", CORES_PER_SOCKET),
            ("ThreadsPerCore", 1),
        ],
    )
    atf.require_config_parameter(
        "Name",
        {"bandwidth": {"Count": 100, "Flags": "CountOnly"}},
        source="gres",
    )
    atf.require_slurm_running()


def _socket_distribution(cpu_ids_range):
    """Bucket the CPU_IDs of an allocation into per-socket counts."""
    cores = set(atf.range_to_list(cpu_ids_range))
    return tuple(
        len(cores & set(range(s * CORES_PER_SOCKET, (s + 1) * CORES_PER_SOCKET)))
        for s in range(SOCKETS)
    )


def _socket_distribution_for(job_args, dist):
    job_str = f'-N1 -n{TASKS} -c1 -m "{dist}" {job_args} --wrap "sleep infinity"'
    job_id = atf.submit_job_sbatch(job_str, fatal=True)
    atf.wait_for_job_state(job_id, "RUNNING", fatal=True)
    job_dict = atf.get_job(job_id)
    return _socket_distribution(job_dict["CPU_IDs"])


@pytest.mark.parametrize("dist", ["*:block", "*:cyclic"])
def test_countonly_gres_preserves_socket_distribution(dist):
    """The CountOnly GRES request must not alter per-socket core distribution."""
    baseline = _socket_distribution_for("", dist)
    atf.cancel_all_jobs(fatal=True, quiet=True)
    with_countonly = _socket_distribution_for("--gres=bandwidth:10", dist)

    assert baseline == with_countonly, (
        f"CountOnly GRES request altered per-socket core distribution under "
        f"-m {dist!r}: baseline={baseline}, with-CountOnly={with_countonly}"
    )
