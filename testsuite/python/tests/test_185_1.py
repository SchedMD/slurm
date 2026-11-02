############################################################################
# Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
############################################################################
"""Test AccountingStorageExternalHost registration with external slurmdbds."""

import os

import pytest

import atf

FIELDS = ["Cluster", "ControlHost", "ControlPort", "RPC", "Flags"]


@pytest.fixture(scope="module", autouse=True)
def setup(module_setup, request):
    atf.require_auto_config("starts extra slurmdbds and creates MySQL databases")
    atf.require_accounting()
    atf.require_tool("mysql")

    port_a = atf.get_open_port()
    for _ in atf.timer(fatal=True):
        port_b = atf.get_open_port()
        if port_b != port_a:
            break
    ext_hosts = [f"localhost:{port_b}", f"localhost:{port_a}"]
    for ext_host in ext_hosts:
        atf.require_config_parameter_includes("AccountingStorageExternalHost", ext_host)

    cluster = atf.get_config_parameter("ClusterName", live=False)
    env_b = start_ext_dbd("b", port_b, request)
    env_a = start_ext_dbd("a", port_a, request)

    # B has the cluster before slurmctld starts, so it is not external
    atf.run_command(
        f"sacctmgr -i add cluster {cluster}",
        env_vars=env_b,
        user=atf.properties["slurm-user"],
        fatal=True,
    )
    b_before = get_cluster(env_b, cluster)
    assert b_before, f"Cluster {cluster} was not added to B"
    assert "External" not in b_before["Flags"].split(","), "Cluster on B is External"
    assert not get_cluster(env_a, cluster), f"Cluster {cluster} is already on A"

    atf.require_slurm_running()
    assert atf.get_config_parameter("AccountingStorageExternalHost") == ",".join(
        ext_hosts
    ), "slurmctld does not use both external hosts"

    return {
        "cluster": cluster,
        "env_a": env_a,
        "env_b": env_b,
        "b_before": b_before,
    }


def start_ext_dbd(name, port, request):
    """Start a slurmdbd with its own port and database, and return its env."""
    slurm_user = atf.properties["slurm-user"]
    conf_dir = atf.properties["slurm-config-dir"]
    dbd_conf = atf.get_config(live=False, source="slurmdbd")
    db = f"slurm_ext_{name}_db"
    db_user = dbd_conf.get("StorageUser", slurm_user)
    run_dir = os.path.dirname(dbd_conf.get("PidFile", "/var/run/slurmdbd.pid"))
    pid_file = f"{run_dir}/slurmdbd_ext_{name}.pid"
    ext_dir = os.path.abspath(f"ext_{name}")
    env = f"SLURM_CONF={ext_dir}/slurm.conf"

    atf.run_command(
        f'mysql -e "DROP DATABASE IF EXISTS {db}; CREATE DATABASE {db};'
        f" GRANT ALL ON {db}.* TO '{db_user}'@'localhost'\"",
        user="root",
        fatal=True,
    )
    request.addfinalizer(lambda: stop_ext_dbd(pid_file, db, db_user))

    dbd_conf.update(
        DbdPort=port,
        StorageLoc=db,
        PidFile=pid_file,
        LogFile=f"{atf.properties['slurm-logs-dir']}/slurmdbd_ext_{name}.log",
    )
    atf.run_command(f"mkdir -p {ext_dir}", user=slurm_user, fatal=True)
    atf.run_command(
        f"cat > {ext_dir}/slurmdbd.conf && chmod 600 {ext_dir}/slurmdbd.conf",
        input="".join(f"{key}={value}\n" for key, value in dbd_conf.items()),
        user=slurm_user,
        fatal=True,
    )
    atf.run_command(
        f"cat > {ext_dir}/slurm.conf",
        input=f"include {conf_dir}/slurm.conf\nAccountingStoragePort={port}\n",
        user=slurm_user,
        fatal=True,
    )
    # auth/slurm daemons read slurm.key from the SLURM_CONF directory
    atf.run_command(
        f"ln -sf {conf_dir}/slurm.key {ext_dir}/slurm.key",
        user=slurm_user,
        fatal=True,
    )

    atf.run_command(
        f"{atf.properties['slurm-sbin-dir']}/slurmdbd",
        env_vars=env,
        user=slurm_user,
        fatal=True,
    )
    for _ in atf.timer(fatal=True):
        if atf.run_command_exit("sacctmgr show cluster", env_vars=env, quiet=True) == 0:
            break
    return env


def stop_ext_dbd(pid_file, db, db_user):
    """Stop the slurmdbd of pid_file and drop its database."""
    slurm_user = atf.properties["slurm-user"]
    stopped = True
    pid = atf.run_command_output(f"cat {pid_file}", user=slurm_user).strip()
    if pid:
        atf.run_command(f"kill {pid}", user=slurm_user)
        for _ in atf.timer():
            if atf.run_command_exit(f"ps -p {pid}", quiet=True) != 0:
                break
        else:
            stopped = False
            atf.run_command(f"kill -9 {pid}", user=slurm_user)
    atf.run_command(
        f'mysql -e "DROP DATABASE IF EXISTS {db};'
        f" REVOKE ALL ON {db}.* FROM '{db_user}'@'localhost'\"",
        user="root",
        fatal=True,
    )
    assert stopped, f"Extra slurmdbd {pid} did not stop on SIGTERM"


def get_cluster(env, cluster):
    """Return the cluster record from the slurmdbd that env points to."""
    output = atf.run_command_output(
        f"sacctmgr -n -P show cluster name={cluster} format={','.join(FIELDS)}",
        env_vars=env,
        fatal=True,
    ).strip()
    if not output:
        return None
    return dict(zip(FIELDS, output.split("|")))


def wait_registered(env, cluster):
    """Return the cluster record once slurmctld has registered on it."""
    for _ in atf.timer():
        record = get_cluster(env, cluster)
        if record and record["ControlPort"] != "0":
            return record
    return None


def test_self_add_external(setup):
    """The cluster adds itself to an empty external slurmdbd as External."""
    cluster = setup["cluster"]

    record = wait_registered(setup["env_a"], cluster)
    assert record, "Cluster did not add itself to the external slurmdbd"
    assert "External" in record["Flags"].split(
        ","
    ), "Cluster on the external slurmdbd is not External"

    local = wait_registered(None, cluster)
    assert local, "Cluster did not register with its own slurmdbd"
    assert "External" not in local["Flags"].split(
        ","
    ), "Cluster on its own slurmdbd is External"


def test_registration_fields(setup):
    """The external record has a ControlHost and slurmctld's port and RPC."""
    cluster = setup["cluster"]

    record = wait_registered(setup["env_a"], cluster)
    local = wait_registered(None, cluster)
    assert record, "Cluster did not register with the external slurmdbd"
    assert local, "Cluster did not register with its own slurmdbd"

    assert record["ControlHost"], "ControlHost is empty"
    assert (
        record["ControlPort"] == local["ControlPort"]
    ), "ControlPort differs from the local slurmdbd"
    assert local["RPC"] != "0", "RPC is not set on the local slurmdbd"
    assert record["RPC"] == local["RPC"], "RPC differs from the local slurmdbd"


def test_cluster_option(setup):
    """Clients of the external slurmdbd reach slurmctld with -M."""
    cluster = setup["cluster"]

    assert wait_registered(
        setup["env_a"], cluster
    ), "Cluster did not register with the external slurmdbd"
    nodes = set(atf.run_command_output("sinfo -h -N -o %N", fatal=True).split())
    assert nodes, "sinfo shows no nodes"

    result = atf.run_command(f"sinfo -M {cluster} -h -N -o %N", env_vars=setup["env_a"])
    assert result["exit_code"] == 0, "sinfo -M failed through the external slurmdbd"
    assert (
        set(result["stdout"].split()) == nodes
    ), "sinfo -M did not show the nodes of slurmctld"


def test_cluster_option_submit(setup):
    """Clients of the external slurmdbd submit jobs to slurmctld with -M."""
    cluster = setup["cluster"]

    assert wait_registered(
        setup["env_a"], cluster
    ), "Cluster did not register with the external slurmdbd"
    job_id = atf.submit_job_sbatch(f"-M {cluster} --wrap true", env_vars=setup["env_a"])
    assert job_id, "sbatch -M failed through the external slurmdbd"
    assert atf.wait_for_job_state(
        job_id, "COMPLETED"
    ), f"Job {job_id} submitted with -M did not complete"


def test_non_external_cluster_ignored(setup):
    """slurmctld does not register where its cluster exists as non-external."""
    cluster = setup["cluster"]
    env_b = setup["env_b"]

    assert (
        get_cluster(env_b, cluster) == setup["b_before"]
    ), "slurmctld changed the non-external cluster"
    result = atf.run_command(
        f"sinfo -M {cluster} -h -N -o %N", env_vars=env_b, xfail=True
    )
    assert result["exit_code"] != 0, "sinfo -M worked through the non-external cluster"

    # Without the non-external cluster, B must accept the registration
    atf.run_command(
        f"sacctmgr -i remove cluster {cluster}",
        env_vars=env_b,
        user=atf.properties["slurm-user"],
        fatal=True,
    )
    atf.restart_slurmctld()
    record = wait_registered(env_b, cluster)
    assert record, "Cluster did not add itself to B after its removal"
    assert "External" in record["Flags"].split(
        ","
    ), "Cluster on B is not External after its removal"
