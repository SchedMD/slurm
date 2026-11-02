############################################################################
# Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
############################################################################
"""slurmctld and slurmdbd communicating with HashPlugin=hash/sha3.

Both daemons hash with SHA3, and each also loads hash/k12, so a client still
using K12 is accepted. Traffic is checked in both directions between the
daemons: a job record reaching slurmdbd from slurmctld, and an account update
reaching slurmctld from slurmdbd, which travels over a connection slurmdbd
opens and hashes on its own.
"""

import os

import pytest

import atf

account = "test_hash_acct"


@pytest.fixture(scope="module", autouse=True)
def setup():
    atf.require_config_parameter("HashPlugin", "hash/sha3")
    atf.require_config_parameter("HashPlugin", "hash/sha3", source="slurmdbd")
    atf.require_accounting(modify=True)
    atf.require_slurm_running()


@pytest.fixture(scope="module")
def k12_env(module_setup):
    """SLURM_CONF setting that makes only the client hash with K12."""
    real_conf = f"{atf.properties['slurm-config-dir']}/slurm.conf"
    path = "k12_slurm.conf"
    atf.run_command(
        f"printf 'include {real_conf}\\nHashPlugin=hash/k12\\n' > {path}",
        fatal=True,
    )
    return f"SLURM_CONF={os.path.abspath(path)}"


@pytest.mark.parametrize("command", ["sacctmgr -n show cluster", "sinfo"])
def test_k12_client_accepted(k12_env, command):
    """A hash/k12 client is accepted by hash/sha3 slurmdbd and slurmctld"""

    atf.run_command(command, env_vars=k12_env, fatal=True)


def test_job_reaches_slurmdbd():
    """A completed job's record reaches slurmdbd"""

    job_id = atf.submit_job_sbatch("--wrap=true", fatal=True)
    atf.wait_for_job_state(job_id, "COMPLETED", fatal=True)

    for t in atf.timer(fatal=True):
        if atf.run_command_output(
            f"sacct -j {job_id} -n -X -o jobid", fatal=True
        ).strip():
            break


def test_account_update_reaches_slurmctld():
    """An account added through slurmdbd reaches slurmctld"""

    atf.run_command(
        f"sacctmgr -i add account {account}",
        user=atf.properties["slurm-user"],
        fatal=True,
    )

    for t in atf.timer(fatal=True):
        if f"Account={account}" in atf.run_command_output(
            f"scontrol show assoc_mgr flags=assoc accounts={account}",
            user=atf.properties["slurm-user"],
            fatal=True,
        ):
            break
