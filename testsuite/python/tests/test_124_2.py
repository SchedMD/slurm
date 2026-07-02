############################################################################
# Copyright (C) SchedMD LLC.
############################################################################
# A running multi-QOS job (--qos=low,high) forced to start under the
# non-highest-priority member must stay attributed to it across a state reload.
# Before the fix, reloading job state re-resolved the running job to the
# highest-priority member, flipping its QOS and inflating that member's per-user
# gres/gpu usage (spuriously tripping QOSMaxGRESPerUser).
#
# Two reload paths are exercised, because they are not equivalent: an in-place
# "scontrol reconfigure" and a slurmctld restart (which reloads job state from
# StateSaveLocation via job_mgr_load_job_state, the path the fix targets).
############################################################################
import atf
import re
import pytest

# Test-scoped QOS/account names so a pre-existing local config cannot collide.
QOS_LOW = "qa_124_2_low"
QOS_HIGH = "qa_124_2_high"
ACCT = "qa_124_2_acct"

# One 4-GPU node. QOS_HIGH caps per-user gpu at GPU_CAP; a QOS_HIGH blocker fills
# that cap so a --qos=QOS_LOW,QOS_HIGH job cannot fit under QOS_HIGH and is
# forced onto the non-head member QOS_LOW.
GPU_PER_NODE = 4
GPU_CAP = 2
BLOCK_GPUS = 2  # fills QOS_HIGH's per-user cap
JOB_GPUS = 2  # cannot also fit under QOS_HIGH -> runs under QOS_LOW


@pytest.fixture(scope="module", autouse=True)
def setup():
    atf.require_auto_config("creates QOS, per-user GRES limits and a multi-QOS job")
    atf.require_config_parameter("SelectType", "select/cons_tres")
    atf.require_config_parameter("SelectTypeParameters", "CR_CPU")
    atf.require_config_parameter_includes("GresTypes", "gpu")
    for i in range(GPU_PER_NODE):
        atf.require_tty(i)
    atf.require_config_parameter(
        "Name", {"gpu": {"File": f"/dev/tty[0-{GPU_PER_NODE - 1}]"}}, source="gres"
    )
    atf.require_nodes(1, [("Gres", f"gpu:{GPU_PER_NODE}"), ("CPUs", GPU_PER_NODE)])
    atf.require_accounting(modify=True)
    atf.require_config_parameter_includes("AccountingStorageTRES", "gres/gpu")
    atf.require_config_parameter_includes("AccountingStorageEnforce", "qos")
    atf.require_config_parameter_includes("AccountingStorageEnforce", "limits")
    atf.require_slurm_running()

    su = atf.properties["slurm-user"]
    user = atf.get_user_name()
    atf.run_command(f"sacctmgr -i add qos {QOS_LOW} Priority=1", user=su, fatal=True)
    atf.run_command(
        f"sacctmgr -i add qos {QOS_HIGH} Priority=100 MaxTRESPerUser=gres/gpu={GPU_CAP}",
        user=su,
        fatal=True,
    )
    atf.run_command(f"sacctmgr -i add account {ACCT}", user=su, fatal=True)
    atf.run_command(
        f"sacctmgr -i add user {user} DefaultAccount={ACCT} account={ACCT} "
        f"qos=normal,{QOS_LOW},{QOS_HIGH}",
        user=su,
        fatal=True,
    )
    # Wait until the controller has ingested the account association (and thus
    # the QOS) so the first submit below cannot race association propagation.
    atf.repeat_until(
        lambda: atf.run_command_output("scontrol show assoc_mgr flags=assoc", user=su),
        lambda out: re.search(rf"Account={ACCT}\b", out) is not None,
        fatal=True,
    )

    yield

    atf.run_command(
        f"sacctmgr -i modify user {user} set qos=normal", user=su, quiet=True
    )
    atf.run_command(f"sacctmgr -i remove account {ACCT}", user=su, quiet=True)
    atf.run_command(f"sacctmgr -i remove qos {QOS_LOW} {QOS_HIGH}", user=su, quiet=True)


def used_gpu_for_qos(qos_name):
    """Per-user gres/gpu USED for our user under the named QOS, parsed from
    `scontrol -o show assoc_mgr flags=qos` (one QOS per line). Fails loudly if
    the QOS line is present but the per-user gres/gpu value cannot be parsed, so
    a format change never silently reads as 0."""
    su = atf.properties["slurm-user"]
    user = atf.get_user_name()
    out = atf.run_command_output(
        "scontrol -o show assoc_mgr flags=qos", user=su, fatal=True
    )
    for line in out.splitlines():
        name = re.search(r"QOS=([^\s(]+)\(", line)
        if not name or name.group(1) != qos_name:
            continue
        used = re.search(
            re.escape(user)
            + r"\(\d+\)=\{[^}]*?MaxTRESPU=[^}]*?gres/gpu=[^()]*\((\d+)\)",
            line,
        )
        if not used:
            pytest.fail(f"cannot parse gres/gpu usage for QOS {qos_name}")
        return int(used.group(1))
    return 0


def test_multiqos_gres_usage_survives_reconfigure():
    su = atf.properties["slurm-user"]

    # job_id1: blocker fills QOS_HIGH's per-user gpu cap.
    job_id1 = atf.submit_job_sbatch(
        f'--account={ACCT} --qos={QOS_HIGH} --gres=gpu:{BLOCK_GPUS} -N1 --wrap "sleep 600"',
        fatal=True,
    )
    atf.wait_for_job_state(job_id1, "RUNNING", fatal=True)

    # job_id2: multi-QOS job cannot fit under QOS_HIGH -> forced onto QOS_LOW.
    job_id2 = atf.submit_job_sbatch(
        f'--account={ACCT} --qos={QOS_LOW},{QOS_HIGH} --gres=gpu:{JOB_GPUS} -N1 --wrap "sleep 600"',
        fatal=True,
    )
    atf.wait_for_job_state(job_id2, "RUNNING", fatal=True)

    # Baseline: job_id2 charged to QOS_LOW, job_id1 to QOS_HIGH.
    assert atf.get_job_parameter(job_id2, "QOS") == QOS_LOW
    assert used_gpu_for_qos(QOS_LOW) == JOB_GPUS
    assert used_gpu_for_qos(QOS_HIGH) == BLOCK_GPUS

    # State reload. Pre-fix this re-attributed the running job_id2 to QOS_HIGH.
    atf.run_command("scontrol reconfigure", user=su, fatal=True)

    # Attribution must be unchanged.
    assert atf.get_job_parameter(job_id2, "QOS") == QOS_LOW, "QOS flipped on reload"
    assert used_gpu_for_qos(QOS_LOW) == JOB_GPUS, "QOS_LOW usage lost on reload"
    assert used_gpu_for_qos(QOS_HIGH) == BLOCK_GPUS, "QOS_HIGH usage inflated on reload"


def test_multiqos_gres_usage_survives_restart():
    # job_id1: blocker fills QOS_HIGH's per-user gpu cap.
    job_id1 = atf.submit_job_sbatch(
        f'--account={ACCT} --qos={QOS_HIGH} --gres=gpu:{BLOCK_GPUS} -N1 --wrap "sleep 600"',
        fatal=True,
    )
    atf.wait_for_job_state(job_id1, "RUNNING", fatal=True)

    # job_id2: multi-QOS job cannot fit under QOS_HIGH -> forced onto QOS_LOW.
    job_id2 = atf.submit_job_sbatch(
        f'--account={ACCT} --qos={QOS_LOW},{QOS_HIGH} --gres=gpu:{JOB_GPUS} -N1 --wrap "sleep 600"',
        fatal=True,
    )
    atf.wait_for_job_state(job_id2, "RUNNING", fatal=True)

    # Baseline: job_id2 charged to QOS_LOW, job_id1 to QOS_HIGH.
    assert atf.get_job_parameter(job_id2, "QOS") == QOS_LOW
    assert used_gpu_for_qos(QOS_LOW) == JOB_GPUS
    assert used_gpu_for_qos(QOS_HIGH) == BLOCK_GPUS

    # State reload via slurmctld restart (job_mgr_load_job_state path). Pre-fix
    # this re-attributed the running job_id2 to QOS_HIGH.
    atf.restart_slurmctld()
    atf.wait_for_job_state(job_id2, "RUNNING", fatal=True)

    # Attribution must be unchanged.
    assert atf.get_job_parameter(job_id2, "QOS") == QOS_LOW, "QOS flipped on reload"
    assert used_gpu_for_qos(QOS_LOW) == JOB_GPUS, "QOS_LOW usage lost on reload"
    assert used_gpu_for_qos(QOS_HIGH) == BLOCK_GPUS, "QOS_HIGH usage inflated on reload"
