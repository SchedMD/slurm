############################################################################
# Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
############################################################################
"""Test PMI2 graceful termination on MPI_Abort (requires MPICH).

Open MPI dropped PMI2 client support, so MPICH is required to exercise the
PMI2 server-side abort path in src/plugins/mpi/pmi2/. PMI2 has no broken-
socket _errhandler() equivalent, so only the explicit MPI_Abort() path is
covered here.
"""

import atf
import os
import pytest
import shutil

pytestmark = pytest.mark.skipif(
    atf.get_version() < (26, 5),
    reason="PMI2 graceful termination via SIG_TERM_KILL added in 26.05",
)


def find_mpich_mpicc():
    """Locate MPICH's mpicc with `mpichversion`."""

    mpichversion = shutil.which("mpichversion")
    if mpichversion:
        return os.path.join(os.path.dirname(mpichversion), "mpicc")
    return None


MPICH_MPICC = find_mpich_mpicc()


@pytest.fixture(scope="module", autouse=True)
def setup():
    if not MPICH_MPICC or not os.path.isfile(MPICH_MPICC):
        pytest.skip(
            "Requires MPICH (mpichversion not found)",
            allow_module_level=True,
        )
    atf.require_mpi("pmi2", MPICH_MPICC)
    atf.require_auto_config("needs MPI/PMI2 configuration")
    atf.require_config_parameter("MpiDefault", "pmi2")
    atf.require_config_parameter("KillWait", "5")
    atf.require_nodes(1, [("CPUs", 4)])
    atf.require_slurm_running()


@pytest.fixture(scope="module")
def mpich_program():
    """Compile mpi_signal_test.c with MPICH's mpicc."""
    src = atf.properties["testsuite_scripts_dir"] + "/mpi_signal_test.c"
    bin_path = os.getcwd() + "/mpi_signal_test_pmi2"
    atf.run_command(f"{MPICH_MPICC} -o {bin_path} {src}", fatal=True)
    return bin_path


@pytest.mark.parametrize("trap", [False, True])
def test_pmi2_abort_kills_failed_step(mpich_program, trap):
    """Surviving ranks are killed when an MPICH/PMI2 rank calls MPI_Abort().

    - trap=False: SIGTERM (default action) terminates ranks immediately.
    - trap=True:  ranks catch SIGTERM and continue; the final SIGKILL after
                  KillWait must still kill them.
    """
    tag = "trap" if trap else "notrap"
    file_out = f"out_{tag}"
    file_err = f"err_{tag}"
    script = f"job_{tag}.sh"
    args = "abort trap" if trap else "abort"

    atf.make_bash_script(script, f"srun --mpi=pmi2 -n4 {mpich_program} {args}")

    job_id = atf.submit_job_sbatch(
        f"-N1 -n4 --output={file_out} --error={file_err} {script}",
        fatal=True,
    )
    atf.wait_for_job_state(job_id, "DONE", fatal=True)
    atf.wait_for_file(file_out, fatal=True)

    stdout = atf.run_command_output(f"cat {file_out}", fatal=True)
    stderr = atf.run_command_output(f"cat {file_err}", fatal=False)

    # Stepd's "DUE TO TASK FAILURE" message proves SIG_TERM_KILL was used.
    assert (
        "DUE TO TASK FAILURE" in stderr
    ), f"Expected SIG_TERM_KILL termination. stderr:\n{stderr}"

    if trap:
        assert (
            "rank_got_sigterm" in stdout
        ), f"Surviving ranks should have caught SIGTERM. stdout:\n{stdout}"
