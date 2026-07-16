############################################################################
# Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
############################################################################
import logging

import pytest

import atf

# Ticket 24975: "sacctmgr load" failed when the input file contained a typed
# TRES (e.g. "gres/gpu:a100") because the ':' that separates the GRES name
# from its type was being treated as the file-format column separator,
# truncating the option early.

gres_name = "gpu"
gres_type = "a100"
typed_tres = f"gres/{gres_name}:{gres_type}"

qos1 = "qos_test_102_12"
dump_file = "test.dump"
redump_file = "test.dump.after"


@pytest.fixture(scope="module", autouse=True)
def setup():
    atf.require_version((26, 5), "bin/sacctmgr")
    atf.require_config_parameter_includes("GresTypes", gres_name)
    atf.require_config_parameter_includes("AccountingStorageTRES", typed_tres)
    atf.require_accounting(modify=True)
    atf.require_slurm_running()


@pytest.fixture(scope="function", autouse=True)
def setup_db():
    # User and account necessary for sacctmgr dump to work
    atf.run_command(
        f"sacctmgr -i add account {atf.properties["slurm-user"]}",
        user=atf.properties["slurm-user"],
        fatal=True,
    )

    atf.run_command(
        f"sacctmgr -i add user {atf.properties["slurm-user"]} Account={atf.properties["slurm-user"]} AdminLevel=Admin",
        user=atf.properties["slurm-user"],
        fatal=True,
    )

    atf.run_command(
        f"sacctmgr -i add qos {qos1} MaxTRESPerUser={typed_tres}=1",
        user=atf.properties["slurm-user"],
        fatal=True,
    )

    yield

    atf.run_command(
        f"sacctmgr -i remove qos {qos1}",
        user=atf.properties["slurm-user"],
        quiet=True,
    )

    atf.run_command(
        f"sacctmgr -i remove user {atf.properties["slurm-user"]} Account={atf.properties["slurm-user"]}",
        user=atf.properties["slurm-user"],
        quiet=True,
    )

    atf.run_command(
        f"sacctmgr -i remove account {atf.properties["slurm-user"]}",
        user=atf.properties["slurm-user"],
        quiet=True,
    )


def test_sacctmgr_load_typed_tres():
    """'sacctmgr load' must accept a dumped file containing a
    typed TRES (e.g. gres/gpu:a100) without failing to parse the colon,
    and must leave the relevant database contents unchanged."""

    cluster_name = atf.get_config_parameter("ClusterName")
    dump_path = f"{atf.module_tmp_path}/{dump_file}"
    redump_path = f"{atf.module_tmp_path}/{redump_file}"

    qos_show_before = atf.run_command_output(
        f"sacctmgr show -nP qos {qos1} format=MaxTRESPU",
        fatal=True,
    )
    assert (
        typed_tres in qos_show_before
    ), f"QoS {qos1} should have MaxTRESPerUser={typed_tres}=1, got: {qos_show_before.rstrip()}"

    # Generate the dump file
    atf.run_command(
        f"sacctmgr -i dump Cluster={cluster_name} file={dump_path}",
        user=atf.properties["slurm-user"],
        fatal=True,
    )

    # Load it back. Before the fix, the ':' inside '{typed_tres}' was treated
    # as a column separator and the load failed with a parse error.
    load_result = atf.run_command(
        f"sacctmgr -i load file={dump_path}",
        user=atf.properties["slurm-user"],
    )

    hint = (
        f"Hint: confirm that 'sacctmgr load' parses the typed TRES '{typed_tres}' "
        f"without treating its ':' as the file-format column separator."
    )
    assert load_result["exit_code"] == 0, (
        f"sacctmgr load failed (exit={load_result['exit_code']}):"
        f"\nstderr={load_result['stderr'].strip()}"
        f"\nstdout={load_result['stdout'].strip()}"
        f"\n{hint}"
    )

    # Verify the typed-TRES limit on the QoS specifically survived the load.
    qos_show_after = atf.run_command_output(
        f"sacctmgr show -nP qos {qos1} format=MaxTRESPU",
        fatal=True,
    )
    assert qos_show_before == qos_show_after, (
        f"QoS {qos1} MaxTRESPerUser changed across 'sacctmgr load':\n"
        f"  before: {qos_show_before.rstrip()}\n"
        f"  after:  {qos_show_after.rstrip()}\n{hint}"
    )

    # Re-dump the cluster after load and compare against the original dump.
    atf.run_command(
        f"sacctmgr -i dump Cluster={cluster_name} file={redump_path}",
        user=atf.properties["slurm-user"],
        fatal=True,
    )
    with open(dump_path) as f:
        dump_before = f.read()
    with open(redump_path) as f:
        dump_after = f.read()
    assert dump_before == dump_after, (
        "sacctmgr dump output differs before vs after 'sacctmgr load' — "
        "database contents changed.\n"
        f"--- before ---\n{dump_before}"
        f"--- after ---\n{dump_after}"
    )

    logging.info(
        f"sacctmgr load completed successfully and DB unchanged for '{typed_tres}'"
    )
