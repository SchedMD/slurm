############################################################################
# Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
############################################################################
"""test_102_20.py - sacctmgr load clean=account and clean=qos (destructive).

Isolated tests for sacctmgr load clean=account and clean=qos against the
configured ClusterName. These operations wipe cluster-wide accounts or
global QOS definitions; each test snapshots accounting state before running
and restores it in teardown so later modules are not affected.
Requires Slurm 25.05+ sacctmgr and auto-config.
"""

import pytest

import atf

test_account1 = "test_acct1"
test_account2 = "test_acct2"
test_qos1 = "test_qos1"
test_qos2 = "test_qos2"


@pytest.fixture(scope="module", autouse=True)
def setup():
    atf.require_auto_config(
        "clean=account/qos load wipes cluster-wide accounts and global QOS"
    )
    atf.require_accounting(modify=True)
    atf.require_slurm_running()


@pytest.fixture(scope="function", autouse=True)
def setup_db():
    """Grant slurm user on root before each test; remove test entities after."""
    atf.run_command(
        f"sacctmgr -i add user {atf.properties['slurm-user']} account=root",
        user=atf.properties["slurm-user"],
    )
    yield
    atf.run_command(
        f"sacctmgr -i remove account {test_account1},{test_account2}",
        user=atf.properties["slurm-user"],
    )
    atf.run_command(
        f"sacctmgr -i remove qos {test_qos1},{test_qos2}",
        user=atf.properties["slurm-user"],
    )


@pytest.fixture
def accounting_snapshot():
    """Snapshot configured-cluster sacctmgr dump; restore after the test.

    The restore is an additive `sacctmgr load` of that dump, so it puts back
    every association row (accounts, users, limits) the dump named. It
    cannot undo two things a clean load can do that the dump does not
    capture: clean=qos removes every global QOS not named in the file, not
    only the ones this test created, and an unreferenced QOS left behind by
    an earlier module is gone with no record in this dump to restore it
    from; clean=account deletes and re-creates the cluster row, and the
    additive reload restores the root association's limits but not
    registration metadata (ControlHost, RPCVersion, Dimensions), which only
    slurmctld sets again on re-register. Full recovery from either relies on
    require_accounting(modify=True)'s SQL backup, which conftest.py restores
    at module teardown; this fixture only needs to undo one test's clean
    load well enough for the next test in the module to start clean.
    """
    cluster = atf.get_config_parameter("ClusterName")
    snap = "accounting_snapshot.cfg"
    atf.run_command(
        f"sacctmgr dump {cluster} file={snap}",
        user=atf.properties["slurm-user"],
        fatal=True,
    )
    yield
    atf.run_command(
        f"sacctmgr -i load file={snap}",
        user=atf.properties["slurm-user"],
        fatal=True,
    )


def _accounting_cluster_count():
    """Return number of clusters registered in the accounting database."""
    out = atf.run_command_output(
        "sacctmgr -n -P list cluster format=Cluster",
        user=atf.properties["slurm-user"],
        fatal=True,
    )
    lines = [ln for ln in out.strip().split("\n") if ln.strip()]
    return len(lines)


def _require_single_accounting_cluster():
    """clean=account/user/qos is only valid when one cluster exists in the DB."""
    count = _accounting_cluster_count()
    if count != 1:
        pytest.skip(
            f"clean=account/user/qos requires one accounting cluster, found {count}"
        )


def _qos_name_exists(qos_name):
    """Return whether qos_name appears in sacctmgr list qos output."""
    out = atf.run_command_output(
        "sacctmgr -n -P list qos format=Name",
        user=atf.properties["slurm-user"],
        fatal=True,
    )
    for ln in out.strip().split("\n"):
        if ln.strip().split("|")[0].strip() == qos_name:
            return True
    return False


def write_load_file(path, content):
    """Write a sacctmgr load file as the slurm user (remote-exec safe)."""
    atf.run_command(
        f"cat > {path}",
        input=content,
        user=atf.properties["slurm-user"],
        fatal=True,
    )


def test_load_clean_account_keyword_removes_unlisted(accounting_snapshot):
    """clean=account removes accounts not in the load file.

    Uses the configured ClusterName only (no extra cluster row) so the
    single-cluster requirement for clean=account is satisfied in ATF.
    """
    _require_single_accounting_cluster()
    cluster = atf.get_config_parameter("ClusterName")
    cfg1 = "clean_acct_kw1.cfg"
    cfg2 = "clean_acct_kw2.cfg"
    config1 = f"""Cluster - '{cluster}'
Parent - 'root'
Account - '{test_account1}':Fairshare=50
Account - '{test_account2}':Fairshare=30
"""
    config2 = f"""Cluster - '{cluster}'
Parent - 'root'
Account - '{test_account1}':Fairshare=60
"""
    write_load_file(cfg1, config1)
    write_load_file(cfg2, config2)
    atf.run_command(
        f"sacctmgr -i load {cfg1}",
        user=atf.properties["slurm-user"],
        fatal=True,
    )
    atf.run_command(
        f"sacctmgr -i load {cfg2} clean=account",
        user=atf.properties["slurm-user"],
        fatal=True,
    )
    out = atf.run_command_output(
        f"sacctmgr -n -P list assoc account={test_account2} cluster={cluster} format=Account",
        fatal=True,
    )
    assert test_account2 not in out, "clean=account should drop unlisted accounts"
    out1 = atf.run_command_output(
        f"sacctmgr -n -P list assoc account={test_account1} cluster={cluster} format=Fairshare",
        fatal=True,
    )
    assert out1.strip() == "60", "Listed account should keep updated Fairshare"


def test_load_clean_qos_removes_unlisted_qos_definitions(accounting_snapshot):
    """clean=qos drops QOS definitions not present in the load file.

    Uses the configured ClusterName only so clean=qos sees one cluster in the DB.
    Keep the cluster default QOS (normal) in both files so clean=qos does not
    remove it; the accounting_snapshot restore needs that QOS still present.
    """
    _require_single_accounting_cluster()
    cluster = atf.get_config_parameter("ClusterName")
    cfg1 = "clean_qos_def1.cfg"
    cfg2 = "clean_qos_def2.cfg"
    config1 = f"""QOS - 'normal':Description='Normal QOS default'
QOS - '{test_qos1}':Priority=10
QOS - '{test_qos2}':Priority=20
Cluster - '{cluster}'
Parent - 'root'
Account - '{test_account1}':Fairshare=50
"""
    config2 = f"""QOS - 'normal':Description='Normal QOS default'
QOS - '{test_qos1}':Priority=10
Cluster - '{cluster}'
Parent - 'root'
Account - '{test_account1}':Fairshare=50
"""
    write_load_file(cfg1, config1)
    write_load_file(cfg2, config2)
    atf.run_command(
        f"sacctmgr -i load {cfg1}",
        user=atf.properties["slurm-user"],
        fatal=True,
    )
    assert _qos_name_exists(test_qos2), "Second QOS should exist before clean=qos"
    atf.run_command(
        f"sacctmgr -i load {cfg2} clean=qos",
        user=atf.properties["slurm-user"],
        fatal=True,
    )
    assert not _qos_name_exists(
        test_qos2
    ), "QOS not in file should be removed with clean=qos"
    assert _qos_name_exists(
        "normal"
    ), "Cluster default QOS must remain when listed under clean=qos"
