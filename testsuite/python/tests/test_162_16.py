############################################################################
# Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
############################################################################
"""Step dependencies require stepmgr, including when enabled per allocation."""

import pytest

import atf


@pytest.fixture(scope="module", autouse=True)
def setup():
    atf.require_version(
        (26, 11),
        "bin/srun",
        reason="--step-dependency requires a 26.11 srun and allocation",
    )
    atf.require_version(
        (26, 11),
        "bin/scontrol",
        reason="step dependency reporting requires a 26.11 scontrol",
    )
    atf.require_version(
        (26, 11),
        "sbin/slurmd",
        reason="step dependencies require a 26.11 stepmgr",
    )
    atf.require_nodes(1, [("CPUs", 2)])
    atf.require_config_parameter("SelectType", "select/cons_tres")
    atf.require_config_parameter("SelectTypeParameters", "CR_CPU")
    atf.require_config_parameter_excludes("SlurmctldParameters", "enable_stepmgr")
    atf.require_config_parameter_includes("PrologFlags", "Contain")
    atf.require_slurm_running()


def test_dependency_without_stepmgr_is_rejected():
    """A plain allocation rejects the dependency without running its payload."""
    atf.make_bash_script(
        "no_stepmgr.sh",
        """srun --exact -n1 bash -c 'touch target_started; until [ -f target_release ]; do sleep 0.2; done' &
until [ -f target_started ]; do sleep 0.2; done
srun --exact -n1 -I --overlap --step-dependency=afterok:$SLURM_JOB_ID.0 touch dependent_ran
DEPENDENT_RC=$?
echo "DEPENDENT_RC=$DEPENDENT_RC"
touch target_release
wait
test "$DEPENDENT_RC" -ne 0
""",
    )
    job_id = atf.submit_job_sbatch(
        "-N1 -n2 -t2 --output=no_stepmgr.out --error=no_stepmgr.out no_stepmgr.sh",
        fatal=True,
    )
    atf.wait_for_job_state(job_id, "COMPLETED", fatal=True)
    atf.assert_file_contents(
        "no_stepmgr.out",
        "Requested operation not supported on this system",
        contains=True,
    )
    atf.run_command("test ! -e dependent_ran", fatal=True)


def test_per_job_stepmgr_honors_dependency():
    """Per-job stepmgr keeps a dependent pending until its target succeeds."""
    atf.make_bash_script(
        "per_job_stepmgr.sh",
        f"""set -e
srun --exact -n1 bash -c 'touch target_started; until [ -f target_release ]; do sleep 0.2; done; touch target_finished' &
TARGET=$!
until [ -f target_started ]; do sleep 0.2; done
srun --exact -n1 --step-dependency=afterok:$SLURM_JOB_ID.0 bash -c 'test -f target_finished && echo DONE > dependent_ran' &
DEPENDENT=$!
{atf.wait_for_step_snippet(1)}scontrol -o show step "$SLURM_JOB_ID.1" > dependent_pending
test ! -f dependent_ran
touch target_release
wait "$TARGET"
wait "$DEPENDENT"
""",
    )
    job_id = atf.submit_job_sbatch(
        "--stepmgr -N1 -n2 -t2 --output=per_job_stepmgr.out "
        "--error=per_job_stepmgr.out per_job_stepmgr.sh",
        fatal=True,
    )
    atf.wait_for_job_state(job_id, "COMPLETED", fatal=True)
    atf.assert_file_contents("dependent_pending", "State=PENDING", contains=True)
    atf.assert_file_contents("dependent_pending", "Reason=Dependency ", contains=True)
    atf.assert_file_contents(
        "dependent_pending", f"StepDependency=afterok:{job_id}.0", contains=True
    )
    atf.assert_file_contents("dependent_ran", "DONE")
