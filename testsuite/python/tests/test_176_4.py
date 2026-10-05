############################################################################
# Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
############################################################################
"""Test slurmdbd RPC authorization at every authentication level.

Issue 50454: slurmdbd serves RPCs over conmgr and caches the authenticated
identity (slurm_msg_auth_t) on the connection at REQUEST_PERSIST_INIT. These
tests pin the authorization semantics that rely on that cached level:

- connections that never authenticate get no service,
- unprivileged users get public reads only,
- operators cannot reach admin-only RPCs,
- non-root admins cannot reach slurmctld-internal RPCs (not CLI-drivable,
  covered by the persist_conn unit test instead),
- root/SlurmUser can do everything.
"""

import os
import socket
import ssl
import struct
import time

import pytest

import atf

# Teardown slack when waiting for a connection the server should drop.
GRACE_SECONDS = 15

test_user = atf.properties["test-user"]
slurm_user = atf.properties["slurm-user"]

# Created by the module setup, in SlurmDBD and as local users.
op_user = "p4operator"
admin_user = "p4admin"
op_account = "p4operatoracct"
op_qos = "p4operatorqos"
# Sacrificial object the unprivileged user tries (and fails) to delete.
root_account = "p4rootacct"


@pytest.fixture(scope="module", autouse=True)
def setup():
    """Require accounting and create an operator and an admin user."""
    atf.require_accounting()
    atf.require_slurm_running()

    for user in (op_user, admin_user):
        atf.run_command(
            f"id {user} || useradd -m -s /bin/bash {user}",
            user="root",
            fatal=True,
        )
        # Best effort: the user may not exist in the database yet.
        atf.run_command(f"sacctmgr -i delete user {user}", user=slurm_user, quiet=True)
    _assert_allowed(
        _sacctmgr(
            slurm_user,
            f"add user {op_user} account=root AdminLevel=Operator",
        )
    )
    _assert_allowed(
        _sacctmgr(
            slurm_user,
            f"add user {admin_user} account=root AdminLevel=Admin",
        )
    )
    _assert_allowed(_sacctmgr(slurm_user, f"add account {root_account}"))

    yield

    _sacctmgr(slurm_user, f"delete qos {op_qos}", quiet=True)
    _sacctmgr(slurm_user, f"delete account {op_account}", quiet=True)
    _sacctmgr(slurm_user, f"delete account {root_account}", quiet=True)
    for user in (op_user, admin_user):
        _sacctmgr(slurm_user, f"delete user {user}", quiet=True)
        atf.run_command(f"userdel -r {user}", user="root", quiet=True)


def _sacctmgr(user, args, **kwargs):
    """Run a non-interactive sacctmgr command as the given user."""
    return atf.run_command(f"sacctmgr -i {args}", user=user, **kwargs)


def _assert_denied(results):
    assert results["exit_code"] != 0, "denied command unexpectedly succeeded"
    output = (results["stdout"] + results["stderr"]).lower()
    assert any(
        phrase in output for phrase in ("denied", "privilege", "only admins")
    ), f"unexpected denial message: {output}"


def _assert_allowed(results):
    assert (
        results["exit_code"] == 0
    ), f"command failed: {results['stdout']} {results['stderr']}"


def _reconfigure(user):
    """Run sacctmgr reconfigure and return the raw results."""
    return _sacctmgr(user, "reconfigure")


def _assert_reconfigure_allowed(results):
    assert results["exit_code"] == 0, f"reconfigure failed: {results['stderr']}"
    assert (
        "dbd_fini" not in results["stderr"].lower()
    ), f"slurmdbd rejected a permitted DBD_RECONFIG: {results['stderr']}"


@pytest.fixture(scope="module")
def archive_dir():
    """World-writable dump dir, so only the server can reject the dump."""
    path = os.path.abspath("p4archive")
    atf.run_command(
        f"mkdir -p {path} && chmod 777 {path}",
        user="root",
        fatal=True,
    )
    yield path
    atf.run_command(f"rm -rf {path}", user="root", quiet=True, fatal=True)


@pytest.fixture
def max_query_time_range():
    """Set MaxQueryTimeRange; slurmdbd re-reads slurmdbd.conf on reconfigure."""
    atf.set_config_parameter("MaxQueryTimeRange", "1:00:00", source="slurmdbd")
    _assert_allowed(_sacctmgr(slurm_user, "reconfigure"))
    yield
    atf.set_config_parameter("MaxQueryTimeRange", None, source="slurmdbd")
    _sacctmgr(slurm_user, "reconfigure", quiet=True)


@pytest.fixture(scope="module")
def dbd_config():
    """Use slurmdbd's config, not slurmctld's."""
    return {key.lower(): value for key, value in atf.get_config(source="dbd").items()}


@pytest.fixture(scope="module")
def dbd_address(dbd_config):
    return (
        dbd_config.get("dbdaddr") or dbd_config.get("dbdhost", "localhost"),
        int(dbd_config.get("dbdport", 6819)),
    )


@pytest.fixture(scope="module")
def tls_enabled(dbd_config):
    tls_type = dbd_config.get("tlstype")
    return tls_type is not None and str(tls_type).lower() not in (
        "tls/none",
        "none",
        "(null)",
    )


@pytest.fixture(scope="module")
def tls_context():
    """Test TLS transport, not certificate identity."""
    context = ssl.SSLContext(ssl.PROTOCOL_TLS_CLIENT)
    context.check_hostname = False
    context.verify_mode = ssl.CERT_NONE
    return context


@pytest.fixture
def dbd_socket(dbd_address, tls_enabled, tls_context):
    """Connect, completing TLS if required."""
    sock = socket.create_connection(dbd_address, timeout=GRACE_SECONDS)
    try:
        sock.setsockopt(socket.IPPROTO_TCP, socket.TCP_NODELAY, 1)
        if tls_enabled:
            sock = tls_context.wrap_socket(sock, server_hostname=dbd_address[0])
        yield sock
    finally:
        sock.close()


def _peer_closed(sock, wait_seconds):
    """Wait for EOF or reset."""
    deadline = time.monotonic() + wait_seconds
    while True:
        remaining = deadline - time.monotonic()
        if remaining <= 0:
            return False
        sock.settimeout(remaining)
        try:
            if sock.recv(1) == b"":
                return True
        except socket.timeout:
            return False
        except (ConnectionResetError, ssl.SSLEOFError):
            return True


@pytest.mark.skipif(
    atf.get_version("sbin/slurmdbd") < (26, 11),
    reason="Issue 50454: old slurmdbd did not close a connection on a non-init first RPC",
)
def test_message_before_init_is_rejected(dbd_socket):
    """A connection whose first frame is not an init is closed."""
    # A legacy-framed (pre-26.11 style) non-init message: [size][type].
    dbd_socket.sendall(struct.pack("!II", 4, 0))
    assert _peer_closed(
        dbd_socket, GRACE_SECONDS
    ), "slurmdbd served a connection that never authenticated"


def test_public_reads_allowed():
    """Unprivileged users can run public read-only queries."""
    for args in (
        "show accounts",
        "show associations",
        "show clusters",
        "show qos",
        "show tres",
    ):
        _assert_allowed(_sacctmgr(test_user, args))
    _assert_allowed(atf.run_command("sacct -X", user=test_user))
    _assert_allowed(atf.run_command("sshare", user=test_user))


def test_user_writes_denied():
    """Unprivileged users cannot modify accounting data."""
    for args in (
        "add account p4deniedacct",
        "add user p4denieduser account=root",
        f"add qos {op_qos}",
        "add cluster p4deniedcluster",
        "modify account root set description=p4denied",
        f"delete account {root_account}",
    ):
        _assert_denied(_sacctmgr(test_user, args))


@pytest.mark.skipif(
    atf.get_version("bin/sacctmgr") < (26, 11),
    reason="Old sacctmgr exited 0 on a denied rollup",
)
def test_user_operator_reads_denied():
    """Unprivileged users cannot run operator-gated read RPCs."""
    _assert_denied(_sacctmgr(test_user, "show wckeys"))
    _assert_denied(_sacctmgr(test_user, "rollup"))


@pytest.mark.skipif(
    atf.get_version("bin/sacctmgr") < (26, 11),
    reason="Issue 50454: old sacctmgr exited 0 on a denied reconfigure",
)
def test_user_admin_rpcs_denied(archive_dir):
    """Unprivileged users cannot run admin-gated RPCs."""
    _assert_denied(_sacctmgr(test_user, f"archive dump directory={archive_dir}"))
    _assert_denied(_reconfigure(test_user))


def test_operator_writes_allowed():
    """Operators can modify ordinary accounting data."""
    _assert_allowed(_sacctmgr(op_user, f"add account {op_account}"))
    _assert_allowed(_sacctmgr(op_user, f"add user p4opuser account={op_account}"))
    _assert_allowed(
        _sacctmgr(op_user, f"modify account {op_account} set description=p4")
    )
    _assert_allowed(_sacctmgr(op_user, "delete user p4opuser"))
    _assert_allowed(_sacctmgr(op_user, f"delete account {op_account}"))


def test_operator_reads_allowed():
    """Operators can run operator-gated read RPCs."""
    _assert_allowed(_sacctmgr(op_user, "show wckeys"))
    _assert_allowed(_sacctmgr(op_user, "rollup"))


@pytest.mark.skipif(
    atf.get_version("bin/sacctmgr") < (26, 11),
    reason="Issue 50454: old sacctmgr exited 0 on a denied reconfigure",
)
def test_operator_admin_rpcs_denied(archive_dir):
    """Operators cannot run admin-gated RPCs or manage QOS."""
    _assert_denied(_sacctmgr(op_user, f"archive dump directory={archive_dir}"))
    _assert_denied(_sacctmgr(op_user, f"add qos {op_qos}"))
    _assert_denied(_reconfigure(op_user))


def test_admin_rpcs_allowed(archive_dir):
    """Non-root admins can run admin-gated RPCs and manage QOS."""
    _assert_allowed(_sacctmgr(admin_user, f"archive dump directory={archive_dir}"))
    _assert_allowed(_sacctmgr(admin_user, f"add qos {op_qos}"))
    _assert_allowed(_sacctmgr(admin_user, f"delete qos {op_qos}"))
    _assert_reconfigure_allowed(_reconfigure(admin_user))


def test_slurm_user_allowed(archive_dir):
    """SlurmUser can run everything, including admin-gated RPCs."""
    _assert_allowed(_sacctmgr(slurm_user, "show wckeys"))
    _assert_allowed(_sacctmgr(slurm_user, f"archive dump directory={archive_dir}"))
    _assert_reconfigure_allowed(_reconfigure(slurm_user))


def test_max_query_time_range(max_query_time_range):
    """Only operators and above may exceed MaxQueryTimeRange."""
    wide_query = "sacct -X -S 2020-01-01"

    results = atf.run_command(wide_query, user=test_user)
    assert results["exit_code"] != 0, "wide query from user not rejected"
    assert (
        "too wide" in (results["stdout"] + results["stderr"]).lower()
    ), f"unexpected rejection: {results['stdout']} {results['stderr']}"

    _assert_allowed(atf.run_command(wide_query, user=op_user))
