############################################################################
# Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
############################################################################
"""Verify RPC listeners reject non-TLS connections when TLS is required.

Only runs on a TLS-enabled cluster (the -s2n variant). Confirms the
fail-closed behavior from issue 51066: a client connecting without TLS is
rejected with ESLURM_TLS_REQUIRED before its RPC is parsed or dispatched,
rather than being served in cleartext. That the reject really does precede
authorization is pinned separately against slurmd.

The non-TLS client is made with a SLURM_CONF override that includes the real
slurm.conf but sets TLSType=tls/none, pointed at only the client or task
process; control cases use the real config. Each reject case is paired with
a control that runs the same probe against the same listener and differs
only in SLURM_CONF, so a reject is shown to be specific to the missing TLS
rather than a dead listener.

Listeners covered:
  * slurmctld and slurmd, which reject via the http_switch path;
  * srun's step-launch listener, which rejects via
    on_rpc_connection_data() (the client-listener fix), probed from a task
    inside the step. sattach opens the same listener, but only srun names
    its RPC port to tasks (SLURM_SRUN_COMM_PORT) while sattach's is
    indistinguishable from its I/O ports, so srun stands in for both;
  * salloc's allocation-message listener, which rejects via the same path,
    probed at the ephemeral port read from the process itself. This one is
    reject-only: an outside client has no certificate to complete mutual
    TLS with it, so no control is possible (see the test for why that is
    still conclusive).

swait's listener is covered by test_166_3, which needs a stepmgr cluster.
scrun's is not covered anywhere: it is the only listener whose behavior
this change really alters, since it previously requested no TLS
fingerprinting at all, but it needs an OCI runtime that is not built in the
environments this suite runs in. It is the first listener to add if that
changes.

The reject paths landed in different releases, so each test gates on the
component owning its path rather than a cluster-wide version.

What is rejected is a non-TLS *RPC*, not everything on the port.
slurmctld.8 and slurmd.8 document HTTP as served on the same port unless
CommunicationParameters=disable_http, with TLS optional, so a plaintext
HTTP client is still answered; that boundary is pinned here too.
"""

import os
import re

import pytest
import requests

import atf

# Reasons for the per-component version gates below.
CTLD_REASON = "Issue 51066: slurmctld TLS-required reply added in 25.11"
SLURMD_REASON = "Issue 51066: slurmd TLS-required reply added in 25.11"
SRUN_REASON = "Issue 51066: conmgr client-listener reject added in 26.05.4"
# salloc only moved its allocation-message listener onto conmgr in 26.11, so
# an older salloc has no reject to exercise. Marked xfail rather than skipped
# so an older salloc that does reject is reported (xfail_strict) instead of
# silently passing this gate off as correct.
ALLOCATE_MSG_REASON = (
    "Issue 51066: salloc allocation-message listener moved to conmgr in 26.11"
)
# slurm_errno.h, explicitly valued and unchanged since it was added in 25.11.
ESLURM_TLS_REQUIRED = 13000
# slurm_errno.h. What slurmd answers an unprivileged from_slurmctld RPC.
ESLURM_USER_ID_MISSING = 2010
# root and SlurmUser pass slurmd's authorization check, so a ping from them is
# answered rather than refused and cannot show where the check sits.
PRIVILEGED_TEST_USER = (
    atf.properties["test-user-uid"] == 0
    or atf.properties["test-user"] == atf.properties["slurm-user"]
)
PRIVILEGED_REASON = (
    "Issue 51066: needs a test user slurmd refuses, but the suite runs as"
    " root or SlurmUser"
)
# slurm_strerror(ESLURM_TLS_REQUIRED), quoted as the admin-facing symptom in
# the Enforcement section of doc/html/tls.shtml.
TLS_REQUIRED_MSG = "TLS missing but required for connection"


@pytest.fixture(scope="module", autouse=True)
def setup():
    atf.require_tls()
    atf.require_tool("gcc")
    # With CommunicationParameters=disable_http the listener is a plain RPC
    # listener, so the http_switch reply the slurmctld legs assert is never
    # reached and the reject comes from the conmgr fix instead. A slurmctld
    # satisfying CTLD_REASON but predating that fix accepts the plaintext
    # probe. test_166_2 covers that configuration.
    atf.require_config_parameter_excludes("CommunicationParameters", "disable_http")
    atf.require_slurm_running()


@pytest.fixture(scope="module")
def non_tls_conf(module_setup):
    """Path to a slurm.conf that includes the real config but disables TLS.

    Only the process pointed at this via SLURM_CONF speaks plaintext; srun and
    the daemons keep the real (TLS) configuration.

    Returned absolute: the tests consuming this run in their own per-function
    directory, and the srun legs read it from inside a job step.
    """
    real_conf = f"{atf.properties['slurm-config-dir']}/slurm.conf"
    path = "non_tls_slurm.conf"
    atf.run_command(
        f"printf 'include {real_conf}\\nTLSType=tls/none\\n' > {path}",
        fatal=True,
    )
    return os.path.abspath(path)


@pytest.fixture(scope="module")
def slurmctld_target():
    """The "<host> <port>" probe argument for the slurmctld RPC listener.

    SlurmctldPort is reported as "first-last" when SlurmctldPortCount > 1, so
    take the first port rather than handing the range to the probe.
    """
    port = str(atf.properties["slurmctld_port"]).split("-")[0]
    return f"{atf.properties['slurmctld_host']} {port}"


@pytest.fixture(scope="module")
def slurmd_target():
    """The "<host> <port>" probe argument for a slurmd RPC listener.

    Read per node rather than from SlurmdPort, since a NodeName line may
    override the port and the suite's clusters commonly do. These are the
    live "scontrol show nodes --json" field names.
    """
    node = sorted(atf.get_nodes())[0]
    host = atf.get_node_parameter(node, "address") or node
    port = atf.get_node_parameter(node, "port")
    return f"{host} {port}"


@pytest.fixture(scope="module")
def probe(module_setup):
    """Compile the non-TLS probe helper and return its path.

    Internal RPC helpers live in libslurmfull; internal headers need the source
    tree on the include path.

    Returned absolute for the same reason as non_tls_conf, and because the
    srun legs exec it as a task.
    """
    source = f"{atf.properties['testsuite_scripts_dir']}/tls_reject_probe.c"
    dest = "tls_reject_probe"
    atf.compile_against_libslurm(source, dest, full=True, fatal=True)
    return os.path.abspath(dest)


@pytest.fixture(scope="module")
def pmi_probe(module_setup):
    """Compile a minimal libpmi client and return its absolute path.

    libpmi lives in the install lib dir rather than next to libslurmfull, and
    that dir is lib64 on distributions that split it.
    """
    prefix = atf.properties["slurm-prefix"]
    lib_dir = "lib64" if os.path.isfile(f"{prefix}/lib64/libpmi.so") else "lib"
    source = f"{atf.properties['testsuite_scripts_dir']}/pmi_tls_probe.c"
    dest = "pmi_tls_probe"
    atf.compile_against_libslurm(
        source,
        dest,
        build_args=f"-Wl,-rpath={prefix}/{lib_dir} -L{prefix}/{lib_dir} -lpmi",
        full=True,
        fatal=True,
    )
    return os.path.abspath(dest)


# --------------------------------------------------------------------------- #
# slurmctld listener (http_switch reject path)
# --------------------------------------------------------------------------- #
@pytest.mark.skipif(atf.get_version("sbin/slurmctld") < (25, 11), reason=CTLD_REASON)
def test_slurmctld_rejects_non_tls(slurmctld_target, non_tls_conf, probe):
    """A non-TLS client is rejected by slurmctld's RPC listener.

    The probe compares the numeric return code against ESLURM_TLS_REQUIRED,
    so this does not depend on the wording of the error message.
    """
    result = atf.run_command(
        f"{probe} {slurmctld_target}",
        env_vars=f"SLURM_CONF={non_tls_conf}",
        fatal=True,
    )

    output = result["stdout"] + result["stderr"]
    assert (
        "PROBE_RESULT=TLS_REQUIRED" in output
    ), f"Expected slurmctld to reject the non-TLS probe, got:\n{output}"


@pytest.mark.skipif(atf.get_version("sbin/slurmctld") < (25, 11), reason=CTLD_REASON)
def test_slurmctld_accepts_tls(slurmctld_target, probe):
    """Control: the same probe under the real (TLS) config is accepted.

    Differs from the reject case only in SLURM_CONF, so an ACCEPTED result
    shows the reject is caused by the missing TLS and not by the probe.
    """
    result = atf.run_command(
        f"{probe} {slurmctld_target}",
        fatal=True,
    )

    output = result["stdout"] + result["stderr"]
    assert (
        "PROBE_RESULT=ACCEPTED" in output
    ), f"Expected slurmctld to accept the valid TLS probe, got:\n{output}"


@pytest.mark.skipif(atf.get_version("sbin/slurmctld") < (25, 11), reason=CTLD_REASON)
def test_sinfo_rejected_without_tls(non_tls_conf):
    """A documented client command cannot reach slurmctld without TLS.

    The probe legs assert the exact return code over an internal API; this
    asserts the same contract as a user meets it. sinfo reports the errno it
    got, so it pins the message tls.shtml tells admins to look up; scontrol
    ping only reports the controller as DOWN and would pass on any failure.
    """
    result = atf.run_command("sinfo", env_vars=f"SLURM_CONF={non_tls_conf}", xfail=True)

    assert (
        TLS_REQUIRED_MSG in result["stderr"]
    ), f"Expected sinfo to report the TLS-required error, got:\n{result}"


@pytest.mark.skipif(atf.get_version("sbin/slurmctld") < (25, 11), reason=CTLD_REASON)
def test_sinfo_accepted_with_tls():
    """Control: the same command under the real (TLS) config succeeds.

    Distinguishes "rejected for want of TLS" from "controller is down".
    """
    result = atf.run_command("sinfo")

    assert (
        result["exit_code"] == 0
    ), f"Expected sinfo to succeed with TLS, got:\n{result}"


@pytest.mark.skipif(atf.get_version("sbin/slurmctld") < (25, 11), reason=CTLD_REASON)
def test_plaintext_http_still_served():
    """Requiring TLS for RPCs does not close plaintext HTTP on the same port.

    slurmctld.8 promises HTTP on SlurmctldPort with TLS wrapping optional and
    without requiring TLSType, so this pins the documented exception to the
    rejection asserted above. atf.request_slurmctld() speaks plain http://.

    Requests the endpoint index rather than a liveness probe: slurmctld.8
    documents "GET /" as returning the list of endpoints, so asserting on the
    body shows the port still serves response content in cleartext, not just
    that something answers the socket. That is what tls.shtml means when it
    tells admins to set disable_http if the port must not carry cleartext.

    The endpoints that return cluster data would demonstrate it more sharply,
    but /metrics* needs MetricsType configured and /{data_parser}/conf and
    readyz?verbose need a token, neither of which this module otherwise
    requires.
    """
    resp = atf.request_slurmctld("")

    assert (
        resp.ok
    ), f"Expected plaintext HTTP to still be served, got {resp.status_code}"
    assert (
        "/healthz" in resp.text
    ), f"Expected the endpoint index to be served in cleartext, got:\n{resp.text}"


# --------------------------------------------------------------------------- #
# slurmd listener (http_switch reject path)
# --------------------------------------------------------------------------- #
@pytest.mark.skipif(atf.get_version("sbin/slurmd") < (25, 11), reason=SLURMD_REASON)
def test_slurmd_rejects_non_tls(slurmd_target, non_tls_conf, probe):
    """A non-TLS client is rejected by slurmd's RPC listener.

    slurmd hands its listening socket to the same http_switch as slurmctld,
    so this pins the second daemon tls.shtml names rather than assuming the
    two share a code path.
    """
    result = atf.run_command(
        f"{probe} {slurmd_target}",
        env_vars=f"SLURM_CONF={non_tls_conf}",
        fatal=True,
    )

    output = result["stdout"] + result["stderr"]
    assert (
        "PROBE_RESULT=TLS_REQUIRED" in output
    ), f"Expected slurmd to reject the non-TLS probe, got:\n{output}"


@pytest.mark.skipif(atf.get_version("sbin/slurmd") < (25, 11), reason=SLURMD_REASON)
def test_slurmd_accepts_tls(slurmd_target, probe):
    """Control: the same probe under the real (TLS) config reaches slurmd.

    slurmd marks REQUEST_PING from_slurmctld, so what a dispatched ping
    answers depends on who runs the probe: an unprivileged sender is refused
    ESLURM_USER_ID_MISSING at the authorization check, while root or SlurmUser
    reaches _rpc_ping() and gets RESPONSE_PING_SLURMD, which
    slurm_get_return_code() maps to SLURM_SUCCESS. Accept either; both are
    only reachable once the listener has accepted the connection and
    dispatched the RPC, which is all this control needs to show.
    """
    result = atf.run_command(
        f"{probe} {slurmd_target}",
        fatal=True,
    )

    output = result["stdout"] + result["stderr"]
    assert (
        "PROBE_RESULT=ACCEPTED" in output or f"rc={ESLURM_USER_ID_MISSING}" in output
    ), f"Expected slurmd to accept and dispatch the TLS probe, got:\n{output}"


@pytest.mark.skipif(atf.get_version("sbin/slurmd") < (25, 11), reason=SLURMD_REASON)
def test_slurmd_plaintext_http_still_served(slurmd_target):
    """slurmd also keeps serving plaintext HTTP on its RPC port.

    slurmd.8 documents the same HTTP server and the same "GET /" endpoint
    index as slurmctld.8, so the documented exception is pinned for both
    daemons rather than assumed to carry over.
    """
    host, port = slurmd_target.split()
    resp = requests.get(f"http://{host}:{port}/")

    assert (
        resp.ok
    ), f"Expected plaintext HTTP to still be served, got {resp.status_code}"
    assert (
        "/healthz" in resp.text
    ), f"Expected the endpoint index to be served in cleartext, got:\n{resp.text}"


# --------------------------------------------------------------------------- #
# Ordering of the reject relative to authorization
# --------------------------------------------------------------------------- #
@pytest.mark.skipif(atf.get_version("sbin/slurmd") < (25, 11), reason=SLURMD_REASON)
@pytest.mark.skipif(PRIVILEGED_TEST_USER, reason=PRIVILEGED_REASON)
def test_reject_precedes_authorization(slurmd_target, non_tls_conf, probe):
    """A rejected connection never reaches the listener's authorization check.

    tls.shtml promises the connection is closed "before the RPC is parsed,
    authenticated, or dispatched". Every other leg here sends a well-formed,
    credentialed RPC that the listener would accept, so a listener that
    authorized first and rejected afterwards would look identical to one that
    rejects first.

    slurmd's REQUEST_PING distinguishes them: it is marked from_slurmctld, so
    for an unprivileged sender authorize-then-reject answers
    ESLURM_USER_ID_MISSING while reject-then-authorize answers
    ESLURM_TLS_REQUIRED. The TLS run establishes that this RPC really is one
    slurmd refuses at authorization, which is what makes the non-TLS run's
    answer evidence of the ordering. That only holds while the probe runs as
    a uid slurmd refuses, hence the skip above.
    """
    dispatched = atf.run_command(f"{probe} {slurmd_target}", fatal=True)
    rejected = atf.run_command(
        f"{probe} {slurmd_target}",
        env_vars=f"SLURM_CONF={non_tls_conf}",
        fatal=True,
    )

    dispatched_output = dispatched["stdout"] + dispatched["stderr"]
    rejected_output = rejected["stdout"] + rejected["stderr"]

    assert f"rc={ESLURM_USER_ID_MISSING}" in dispatched_output, (
        "Expected slurmd to refuse this RPC at authorization when it is"
        f" dispatched, got:\n{dispatched_output}"
    )
    assert (
        "PROBE_RESULT=TLS_REQUIRED" in rejected_output
    ), f"Expected the non-TLS connection to be rejected, got:\n{rejected_output}"
    assert f"rc={ESLURM_USER_ID_MISSING}" not in rejected_output, (
        "Expected the non-TLS connection to be rejected before authorization,"
        f" but it reached the authorization check:\n{rejected_output}"
    )


# --------------------------------------------------------------------------- #
# srun step-launch listener (on_rpc_connection_data reject path)
# --------------------------------------------------------------------------- #
@pytest.mark.skipif(atf.get_version("bin/srun") < (26, 5, 4), reason=SRUN_REASON)
def test_step_launch_rejects_non_tls(non_tls_conf, probe):
    """srun's step-launch listener rejects a non-TLS connection from a task.

    Exercises on_rpc_connection_data()'s reject path (the client-listener fix,
    shared with the allocate_msg and swait listeners). The probe runs as a task
    with the non-TLS config, so it connects back to srun's step-launch listener
    (SLURM_SRUN_COMM_HOST/PORT) in plaintext.
    """
    result = atf.run_command(
        f"srun -N1 -n1 -t1 env SLURM_CONF={non_tls_conf} {probe}",
        fatal=True,
    )

    output = result["stdout"] + result["stderr"]
    assert (
        "PROBE_RESULT=TLS_REQUIRED" in output
    ), f"Expected srun to reject the non-TLS probe, got:\n{output}"


@pytest.mark.skipif(atf.get_version("bin/srun") < (26, 5, 4), reason=SRUN_REASON)
def test_step_launch_accepts_tls(probe):
    """Control: a valid TLS task is accepted by srun's step-launch listener.

    srun's listener presents a per-allocation certificate that is pinned
    through the step launch (not CA-signed). The probe reads that cert from
    SLURM_SRUN_TLS_CERT (exported by srun for the task) so it can establish
    trusted TLS, the same mechanism libpmi uses to talk back to srun.
    """
    result = atf.run_command(
        f"srun -N1 -n1 -t1 {probe}",
        fatal=True,
    )

    output = result["stdout"] + result["stderr"]
    assert (
        "PROBE_RESULT=ACCEPTED" in output
    ), f"Expected srun to accept the valid TLS probe, got:\n{output}"


@pytest.mark.skipif(atf.get_version("bin/srun") < (26, 5, 4), reason=SRUN_REASON)
def test_pmi_task_rejected_without_tls(non_tls_conf, pmi_probe):
    """A PMI task pointed at a non-TLS config cannot complete its exchange.

    This is the reported reproducer rather than a synthetic RPC: libpmi is a
    real client of srun's step launch listener, so it exercises the same
    rejection through an interface tasks actually use.
    """
    result = atf.run_command(
        f"srun -N1 -n1 -t1 env SLURM_CONF={non_tls_conf} {pmi_probe}",
        xfail=True,
    )

    output = result["stdout"] + result["stderr"]
    assert (
        "PMI_RESULT=OK" not in output
    ), f"Expected the PMI exchange to fail without TLS, got:\n{output}"
    assert (
        f"error_code={ESLURM_TLS_REQUIRED}" in output
    ), f"Expected the PMI failure to be the TLS-required error, got:\n{output}"


@pytest.mark.skipif(atf.get_version("bin/srun") < (26, 5, 4), reason=SRUN_REASON)
def test_pmi_task_accepted_with_tls(pmi_probe):
    """Control: the same PMI task under the real (TLS) config completes."""
    result = atf.run_command(
        f"srun -N1 -n1 -t1 {pmi_probe}",
        fatal=True,
    )

    output = result["stdout"] + result["stderr"]
    assert (
        "PMI_RESULT=OK" in output
    ), f"Expected the PMI exchange to succeed with TLS, got:\n{output}"


# --------------------------------------------------------------------------- #
# salloc allocation-message listener (on_rpc_connection_data reject path)
# --------------------------------------------------------------------------- #

# Long enough that salloc is still holding the allocation, with its
# allocation-message listener open, when the probe connects.
SALLOC_HOLD_SECS = 60
# salloc prints this once the allocation is granted; waited for so the probe
# runs against an established allocation rather than a half-started salloc.
GRANTED_RE = re.compile(r"Granted job allocation (\d+)")


def _allocate_msg_target():
    """Return "<host> <port>" for salloc's listening TCP socket, or None.

    salloc's allocation-message listener is the only socket it keeps in LISTEN
    while running a command, and its ephemeral port is never reported to a
    client, so it is read from salloc's own sockets. lsof matches by command
    name, not PID, because atf runs the backgrounded salloc under a bash
    wrapper whose PID is the one the caller sees.
    """
    listeners = atf.run_command_output(
        "lsof -a -c salloc -iTCP -sTCP:LISTEN -P -n -Fn", quiet=True
    )
    for line in listeners.splitlines():
        if not line.startswith("n"):
            continue
        host, _, port = line[1:].rpartition(":")
        host = host.strip("[]")
        if host in ("*", "", "0.0.0.0", "::"):
            host = atf.properties["slurmctld_host"]
        return f"{host} {port}"
    return None


@pytest.fixture
def salloc_listener():
    """Hold an allocation with salloc and yield its allocate_msg "<host port>".

    salloc runs a local command for the life of the allocation, keeping the
    allocation-message listener open, and its ephemeral port is read from
    salloc's own sockets. salloc is terminated when the test finishes,
    including when it fails first.
    """
    atf.require_tool("lsof")
    log = "salloc.out"
    process = atf.run_command(
        f"salloc -N1 sleep {SALLOC_HOLD_SECS} > {log} 2>&1",
        background=True,
    )["process"]

    job_id = None
    target = None
    for _ in atf.timer(fatal=True):
        log_output = atf.run_command_output(f"cat {log}", quiet=True)
        if process.poll() is not None:
            pytest.fail(f"salloc exited before opening its listener:\n{log_output}")
        if job_id is None:
            granted = GRANTED_RE.search(log_output)
            if granted:
                job_id = granted[1]
        target = _allocate_msg_target()
        if job_id and target:
            break

    yield target

    process.terminate()


@pytest.mark.xfail(atf.get_version("bin/salloc") < (26, 11), reason=ALLOCATE_MSG_REASON)
def test_allocate_msg_rejects_non_tls(non_tls_conf, probe, salloc_listener):
    """A non-TLS client is rejected by salloc's allocation-message listener.

    No valid-TLS control is probed: an outside client has no certificate to
    complete mutual TLS with salloc's listener, and salloc sends no reply to a
    bare REQUEST_PING even over TLS. TLS_REQUIRED is proof enough on its own,
    since only a live listener enforcing TLS returns it; one that was down or
    unreachable would report NO_RESPONSE.
    """
    result = atf.run_command(
        f"{probe} {salloc_listener}",
        env_vars=f"SLURM_CONF={non_tls_conf}",
        fatal=True,
    )

    output = result["stdout"] + result["stderr"]
    assert (
        "PROBE_RESULT=TLS_REQUIRED" in output
    ), f"Expected salloc's listener to reject the non-TLS probe, got:\n{output}"
