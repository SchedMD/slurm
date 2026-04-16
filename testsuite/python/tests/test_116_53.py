############################################################################
# Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
############################################################################
"""Test PMIx graceful termination on task failure."""

import atf
import pytest

pytestmark = pytest.mark.skipif(
    atf.get_version() < (26, 5),
    reason="PMIx graceful termination via SIG_TERM_KILL added in 26.05",
)


@pytest.fixture(scope="module", autouse=True)
def setup():
    atf.require_auto_config("needs MPI/PMIx configuration")
    atf.require_config_parameter("MpiDefault", "pmix")
    atf.require_config_parameter("KillWait", "5")
    atf.require_nodes(1, [("CPUs", 4)])
    atf.require_slurm_running()


def run_pmix_failure(mpi_program, mode, trap):
    """Run mpi_signal_test and return (stdout, stderr)."""
    tag = f"{mode}_{'trap' if trap else 'notrap'}"
    file_out = f"out_{tag}"
    file_err = f"err_{tag}"
    script = f"job_{tag}.sh"
    flags = []
    if mode == "abort":
        flags.append("abort")
    if trap:
        flags.append("trap")
    args = " ".join(flags)

    atf.make_bash_script(script, f"srun --mpi=pmix -n4 {mpi_program} {args}")

    job_id = atf.submit_job_sbatch(
        f"-N1 -n4 --output={file_out} --error={file_err} {script}",
        fatal=True,
    )
    atf.wait_for_job_state(job_id, "DONE", fatal=True)
    atf.wait_for_file(file_out, fatal=True)

    stdout = atf.run_command_output(f"cat {file_out}", fatal=True)
    stderr = atf.run_command_output(f"cat {file_err}", fatal=False)
    return stdout, stderr


@pytest.mark.parametrize("mpi_program", ["mpi_signal_test"], indirect=True)
@pytest.mark.parametrize("mode", ["exit", "abort"])
@pytest.mark.parametrize("trap", [False, True])
def test_pmix_kills_failed_step(mpi_program, mode, trap):
    """Surviving ranks are killed when a PMIx rank fails.

    Both PMIx failure paths send SIG_TERM_KILL, which stepd expands to
    SIGCONT+SIGTERM+sleep(KillWait)+SIGKILL:
      - mode='exit':  rank 0 _exit(42), caught by _errhandler()
      - mode='abort': rank 0 MPI_Abort(), caught by pmixp_lib_abort()

    - trap=False: SIGTERM (default) terminates ranks immediately.
    - trap=True:  ranks catch SIGTERM and continue; the final SIGKILL after
                  KillWait must still kill them.
    """
    stdout, stderr = run_pmix_failure(mpi_program, mode=mode, trap=trap)

    # Stepd's "DUE TO TASK FAILURE" message proves SIG_TERM_KILL was used
    # rather than a raw SIGKILL.
    assert (
        "DUE TO TASK FAILURE" in stderr
    ), f"Expected SIG_TERM_KILL termination. stderr:\n{stderr}"

    if trap:
        # Trap fired => SIGTERM was actually delivered to the surviving ranks
        # (i.e. the graceful sequence was used, not just SIGKILL).
        assert (
            "rank_got_sigterm" in stdout
        ), f"Surviving ranks should have caught SIGTERM. stdout:\n{stdout}"
