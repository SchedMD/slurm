############################################################################
# Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
############################################################################
"""Known Slurm issues that may make any test fail.

Classifiers used by conftest.py to label a failure with its Issue/Ticket, and to
xfail it on versions older than the fix.
"""
import datetime
import logging
import re

import atf


def classify_coredump(bin_path, bt_file, slurm_prefix=""):
    """
    Return (xfail, reason) for a coredump of bin_path, given its backtrace in
    bt_file. xfail is True if it's a known issue fixed in a newer version than
    the one tested. Otherwise it's a failure, reported with the known reason or
    as an unknown coredump.

    Some known coredumps are gcores of daemons that were not able to stop, or
    crashes that make a daemon fail to stop. The teardown failures they cause
    are xfailed with them by classify_teardown_failure().
    """
    bt = atf.run_command_output(f"cat {bt_file}", quiet=True, fatal=True)

    reason = "Ticket Unknown: Fixed issue in slurmrestd in 25.05: SIGABRT in con_set_polling(): has_in || has_out"
    component = "sbin/slurmrestd"
    if (
        component in bin_path
        and "Program terminated with signal SIGABRT" in bt
        and "src/conmgr/con.c" in bt
        and "src/conmgr/watch.c" in bt
        and "con_set_polling" in bt
        and "_handle_connection" in bt
        and "has_in || has_out" in bt
    ):
        if atf.get_version(component, slurm_prefix=slurm_prefix) >= (25, 5):
            return False, reason
        else:
            return True, reason

    reason = "Ticket Unknown: Fixed issue in slurmdbd in 25.05: SIGABRT in _connection_fini_callback(): pthread_mutex_lock(): Invalid argument"
    component = "sbin/slurmdbd"
    if (
        component in bin_path
        and "Program terminated with signal SIGABRT" in bt
        and "src/common/log.c" in bt
        and "src/slurmdbd/rpc_mgr.c" in bt
        and "_connection_fini_callback" in bt
        and "_service_connection" in bt
        and "fatal_abort" in bt
        and "pthread_mutex_lock" in bt
    ):
        if atf.get_version(component, slurm_prefix=slurm_prefix) >= (25, 5):
            return False, reason
        else:
            return True, reason

    reason = "Ticket 24853: Issue in slurmdbd in 25.05: SIGABRT in slurm_persist_conn_recv_thread_init: service_conn"
    component = "sbin/slurmdbd"
    if (
        component in bin_path
        and "Program terminated with signal SIGABRT" in bt
        and "src/common/persist_conn.c" in bt
        and "src/slurmdbd/rpc_mgr.c" in bt
        and "slurm_persist_conn_recv_thread_init" in bt
        and "service_conn" in bt
        and "__xassert_failed" in bt
    ):
        if atf.get_version(component, slurm_prefix=slurm_prefix)[:2] != (25, 5):
            return False, reason
        else:
            return True, reason

    reason = "Ticket 22310: Known issue when shutting down slurmdbd: SIGSEGV in _service_connection()"
    component = "sbin/slurmdbd"
    if (
        component in bin_path
        and "Program terminated with signal SIGSEGV" in bt
        and "src/common/persist_conn.c" in bt
        and "_service_connection" in bt
    ):
        if atf.get_version(component, slurm_prefix=slurm_prefix) >= (26, 5):
            return False, reason
        else:
            return True, reason

    reason = "Ticket 22326: Known issue in slurmdbd: SIGABRT in  _list_find_first_lock(): Assertion (l != NULL)"
    component = "sbin/slurmdbd"
    if (
        component in bin_path
        and "Program terminated with signal SIGABRT" in bt
        and "src/common/list.c" in bt
        and "src/slurmdbd/proc_req.c" in bt
        and "_list_find_first_lock" in bt
        and "_process_service_connection" in bt
        and "l != NULL" in bt
    ):
        if atf.get_version(component, slurm_prefix=slurm_prefix) >= (25, 5):
            return False, reason
        else:
            return True, reason

    reason = "Ticket 22370: Known issue in slurmd with TLS: SIGSEGV in _pack_buf_msg() relaying a response to slurmstepd. Fixed in 25.11+"
    component = "sbin/slurmd"
    if (
        component in bin_path
        and "Program terminated with signal SIGSEGV" in bt
        and "src/common/stepd_proxy.c" in bt
        and "in _pack_buf_msg" in bt
        and "in _slurmd_pack_msg_to_stepd" in bt
        and "in _on_data_local_socket" in bt
    ):
        if atf.get_version(component, slurm_prefix=slurm_prefix) >= (25, 11):
            return False, reason
        else:
            return True, reason

    reason = "Ticket 24122: Known issue in slurmctld: SIGABRT in conn_g_recv(): Assertion (plugin_inited == PLUGIN_INITED) failed"
    component = "sbin/slurmctld"
    if (
        component in bin_path
        and "Program terminated with signal SIGABRT" in bt
        and "src/interfaces/conn.c" in bt
        and "src/common/slurm_protocol_socket.c" in bt
        and "src/common/forward.c" in bt
        and "conn_g_recv" in bt
        and "_fwd_tree_thread" in bt
        and "plugin_inited == PLUGIN_INITED" in bt
    ):
        # Fixed in 25.11.5 and 25.5.8
        if (
            (atf.get_version(component, slurm_prefix=slurm_prefix) < (25, 5))
            or (
                (25, 5) < atf.get_version(component, slurm_prefix=slurm_prefix)
                and atf.get_version(component, slurm_prefix=slurm_prefix) < (25, 5, 8)
            )
            or (
                (25, 11) < atf.get_version(component, slurm_prefix=slurm_prefix)
                and atf.get_version(component, slurm_prefix=slurm_prefix) < (25, 11, 5)
            )
        ):
            return True, reason
        else:
            return False, reason

    reason = "Ticket 24562: Known issue when shutting down slurmdbd: SIGABORT in acct_storage_g_close_connection(): Assertion (plugin_inited != PLUGIN_NOT_INITED). Fixed in 25.11.7+."
    component = "sbin/slurmdbd"
    if (
        component in bin_path
        and "Program terminated with signal SIGABRT" in bt
        and "src/interfaces/accounting_storage.c" in bt
        and "acct_storage_g_close_connection" in bt
        and "plugin_inited != PLUGIN_NOT_INITED" in bt
    ):
        if atf.get_version(component, slurm_prefix=slurm_prefix) >= (25, 11, 7):
            return False, reason
        else:
            return True, reason
    component = "sbin/slurmctld"
    if (
        component in bin_path
        and "Program terminated with signal SIGABRT" in bt
        and "src/interfaces/accounting_storage.c" in bt
        and "clusteracct_storage_g_node_up" in bt
        and "plugin_inited != PLUGIN_NOT_INITED" in bt
    ):
        if atf.get_version(component, slurm_prefix=slurm_prefix) >= (25, 11, 7):
            return False, reason
        else:
            return True, reason

    reason = "Ticket 24905: Known issue with slurmstepd with stepmgr"
    component = "sbin/slurmstepd"
    if (
        component in bin_path
        and "Program terminated with signal SIGSEGV" in bt
        and "src/slurmd/slurmstepd/mgr.c" in bt
        and "_spawn_job_container" in bt
        and "for (uint32_t i = 0; i < step->node_tasks; i++)" in bt
    ):
        # "slurmstepd -V" won't work, but slurmd should have the same version
        if atf.get_version("sbin/slurmd", slurm_prefix=slurm_prefix) >= (25, 11, 6):
            return False, reason
        else:
            return True, reason

    reason = "Ticket 24907: Known issue with slurmstepd and jobacct_gather"
    component = "sbin/slurmstepd"
    if (
        component in bin_path
        and "Program terminated with signal SIGABRT" in bt
        and "src/interfaces/jobacct_gather.c" in bt
        and "jobacctinfo_aggregate" in bt
        and "step_partial_comp" in bt
        and "dest=0x0" in bt
    ):
        # "slurmstepd -V" won't work, but slurmd should have the same version
        if atf.get_version("sbin/slurmd", slurm_prefix=slurm_prefix) > (25, 11, 6):
            return False, reason
        else:
            return True, reason

    reason = "Ticket 24952: Known issue with slurmctld auth_g_destroy(): Assertion (g_context_num > 0)"
    component = "sbin/slurmctld"
    if (
        component in bin_path
        and "Program terminated with signal SIGABRT" in bt
        and "src/interfaces/auth.c" in bt
        and "auth_g_destroy" in bt
        and "slurm_receive_msgs" in bt
        and "g_context_num > 0" in bt
    ):
        if atf.get_version(component, slurm_prefix=slurm_prefix) >= (25, 5):
            return False, reason
        else:
            return True, reason

    reason = "Ticket 24991: Known issue with slurmstepd and _wait_for_job_running"
    component = "sbin/slurmstepd"
    if (
        component in bin_path
        and "Program terminated with signal SIGSEGV" in bt
        and "src/slurmd/slurmstepd/req.c" in bt
        and "_wait_for_job_running" in bt
        and "___pthread_mutex_lock" in bt
    ):
        # TODO: Add version when t24991 is fixed
        return False, reason

    reason = "Ticket 25013: Known issue with slurmctld: Assertion (con_flag(con, FLAG_READ_EOF) || con_flag(con, FLAG_IS_LISTEN)). Fixed in 25.05.8+"
    component = "sbin/slurmctld"
    if (
        component in bin_path
        and "Program terminated with signal SIGABRT" in bt
        and "src/conmgr/con.c" in bt
        and "in close_con" in bt
        and "con_flag(con, FLAG_READ_EOF) || con_flag(con, FLAG_IS_LISTEN)" in bt
    ):
        if atf.get_version(component, slurm_prefix=slurm_prefix) >= (25, 5, 8):
            return False, reason
        else:
            return True, reason

    reason = "Ticket 25070: Known issue with slurmd: fatal: _forward_thread: pthread_mutex_lock()/unlock(): Invalid argument. Fixed in 25.11.6+"
    component = "sbin/slurmd"
    if (
        component in bin_path
        and "Program terminated with signal SIGABRT" in bt
        and "src/common/log.c" in bt
        and "in fatal_abort" in bt
        and "in _forward_thread" in bt
        and (
            "%s: pthread_mutex_lock(): %m" in bt
            or "%s: pthread_mutex_unlock(): %m" in bt
        )
    ):
        if atf.get_version(component, slurm_prefix=slurm_prefix) >= (25, 11, 6):
            return False, reason
        else:
            return True, reason

    reason = "Ticket 25095: Known issue with slurmctld: SIGABRT: slurmdb_destroy_assoc_usage fixed in 25.11.6"
    component = "sbin/slurmctld"
    if (
        component in bin_path
        and "Program terminated with signal SIGABRT" in bt
        and "src/common/slurmdb_defs.c" in bt
        and "in slurm_xfree" in bt
        and "in slurmdb_destroy_assoc_usage" in bt
        and "in slurmdb_free_assoc_rec_members" in bt
    ):
        if atf.get_version(component, slurm_prefix=slurm_prefix) >= (25, 11, 6):
            return False, reason
        else:
            return True, reason

    reason = "Ticket 25135: Known issue with slurmctld with s2n: SIGSEGV: _on_s2n_error. Fixed in 26.05.2+"
    component = "sbin/slurmctld"
    if (
        component in bin_path
        and "Program terminated with signal SIGSEGV" in bt
        and "src/plugins/tls/s2n/tls_s2n.c" in bt
        and "src/interfaces/conn.c" in bt
        and "in _on_s2n_error" in bt
        and "in _negotiate" in bt
        and "in tls_p_create_conn" in bt
        and "s2n_connection_get_delay" in bt
    ):
        if atf.get_version(component, slurm_prefix=slurm_prefix) >= (26, 5, 2):
            return False, reason
        else:
            return True, reason

    reason = "Ticket 25141: Known issue with slurmctld on reconfigure: SIGABRT in jobacct_storage_g_step_complete(): Assertion (plugin_inited != PLUGIN_NOT_INITED). Fixed in 25.11+"
    component = "sbin/slurmctld"
    if (
        component in bin_path
        and "Program terminated with signal SIGABRT" in bt
        and "src/interfaces/accounting_storage.c" in bt
        and "jobacct_storage_g_step_complete" in bt
        and "_slurm_rpc_step_complete" in bt
        and "plugin_inited != PLUGIN_NOT_INITED" in bt
    ):
        if atf.get_version(component, slurm_prefix=slurm_prefix) >= (25, 11):
            return False, reason
        else:
            return True, reason

    reason = "Ticket 25406: slurmctld stuck in conmgr_quiesce() on shutdown due to a lost poll interrupt. Fixed in 26.05+"
    component = "sbin/slurmctld"
    if (
        component in bin_path
        and "Program terminated with signal" not in bt
        and "in conmgr_quiesce" in bt
        and "in _slurmctld_background" in bt
        and "in pollctl_poll" in bt
    ):
        if atf.get_version(component, slurm_prefix=slurm_prefix) >= (26, 5):
            return False, reason
        else:
            return True, reason

    reason = "Issue 50192: slurmrestd - SIGABRT in _foreach_add_path() on repeated -d data_parser. Fixed in 26.05.5+"
    component = "sbin/slurmrestd"
    if (
        component in bin_path
        and "Program terminated with signal SIGABRT" in bt
        and "src/slurmrestd/openapi.c" in bt
        and "_foreach_add_path" in bt
        and "!data_key_get(spec->paths, path->path)" in bt
    ):
        if atf.get_version(component, slurm_prefix=slurm_prefix) >= (26, 5, 5):
            return False, reason
        else:
            return True, reason

    reason = "Issue 50974: Known issue about slurmd stuck waiting for a completing job during shutdown. Seems fixed in 26.05+"
    component = "sbin/slurmd"
    if (
        component in bin_path
        and "pause_for_job_completion" in bt
        and "_rpc_terminate_job" in bt
    ):
        if atf.get_version(component, slurm_prefix=slurm_prefix) >= (26, 5):
            return False, reason
        else:
            return True, reason

    reason = "Issue 50979: OpenSSL atexit teardown race at shutdown (concurrent with OPENSSL_cleanup). Fixed in 26.11+."
    signals = ["SIGABRT", "SIGSEGV"]
    components = [
        "sbin/slurmd",
        "sbin/slurmctld",
        "bin/srun",
        "bin/salloc",
        "bin/sbatch",
        "bin/scancel",
        "bin/scontrol",
        "bin/sbcast",
    ]
    frames_in_bt = ["OPENSSL_cleanup", "__nptl_deallocate_tsd"]
    component_match = next((c for c in components if bin_path.endswith(c)), None)
    # The main thread may be caught inside OPENSSL_cleanup() (atexit), or
    # already past it running DSO destructors (_dl_fini & co.).
    teardown_frames = (
        "exit",
        "__GI_exit",
        "__run_exit_handlers",
        "__funcs_on_exit",
        "_dl_fini",
        "_dl_call_fini",
        "__cxa_finalize",
        "__do_global_dtors_aux",
    )
    in_teardown = any(f"in {frame} (" in bt for frame in teardown_frames)
    # libcrypto used after OPENSSL_cleanup() freed its global locks, e.g. a
    # TLS handshake on srun's listener while srun exits (the main thread may
    # not be unwindable by then).
    freed_crypto_lock = "rwlock=0x0" in bt and any(
        f"in CRYPTO_THREAD_{lock}_lock (" in bt for lock in ("read", "write")
    )
    if (
        component_match
        and any(f"Program terminated with signal {s}" in bt for s in signals)
        and "libcrypto.so" in bt
        and (
            (
                in_teardown
                and (any(f in bt for f in frames_in_bt) or "in s2n_negotiate (" in bt)
            )
            or freed_crypto_lock
        )
    ):
        if atf.get_version(component_match, slurm_prefix=slurm_prefix) >= (26, 11):
            return False, reason
        else:
            return True, reason

    reason = "Issue 51060: slurmctld - SIGABRT: _kill_job_step(): Assertion (job_ptr->job_id == job_step_kill_msg->step_id.job_id) failed"
    component = "sbin/slurmctld"
    if (
        component in bin_path
        and "Program terminated with signal SIGABRT" in bt
        and "src/slurmctld/job_mgr.c" in bt
        and "_kill_job_step" in bt
        and "job_ptr->job_id == job_step_kill_msg->step_id.job_id" in bt
    ):
        # TODO: Add version when i51060 is fixed
        return False, reason

    reason = "Issue 51111: srun/salloc - SIGABRT on shutdown: tls_create(): Assertion (con->input_fd >= 0) failed"
    components = ["bin/srun", "bin/salloc"]
    if (
        any(bin_path.endswith(c) for c in components)
        and "Program terminated with signal SIGABRT" in bt
        and "src/conmgr/tls.c" in bt
        and "in tls_create" in bt
        and "con->input_fd >= 0" in bt
    ):
        # TODO: Add version when i51111 is fixed
        return False, reason

    reason = "Issue 51119: srun - SIGSEGV in slurm_free_node_alias_addrs on powered down cloud node with stepmgr. Fixed in 26.05."
    component = "bin/srun"
    if (
        component in bin_path
        and "Program terminated with signal SIGSEGV" in bt
        and "src/common/xmalloc.c" in bt
        and "src/common/slurm_protocol_defs.c" in bt
        and "src/api/allocate.c" in bt
        and "in slurm_xfree" in bt
        and "in slurm_free_node_alias_addrs_members" in bt
        and "in slurm_free_node_alias_addrs" in bt
        and "in slurm_job_step_create" in bt
    ):
        if atf.get_version(component, slurm_prefix=slurm_prefix) >= (26, 5):
            return False, reason
        else:
            return True, reason

    reason = "Issue 51358: slurmscriptd stuck in conmgr_fini() waiting for a watch() thread that already returned"
    component = "sbin/slurmctld"
    if (
        component in bin_path
        and "Program terminated with signal" not in bt
        and "slurmctld: slurmscriptd" in bt
        and "in wait_for_watch" in bt
        and "in conmgr_fini" in bt
        and "in slurmscriptd_run_slurmscriptd" in bt
    ):
        # TODO: Add version when i51358 is fixed
        return False, reason

    reason = "Issue 51359: slurmd - SIGABRT on shutdown: slurm_unpack_msg_and_forward(): Assertion (fd >= 0) failed"
    component = "sbin/slurmd"
    if (
        component in bin_path
        and "Program terminated with signal SIGABRT" in bt
        and "in slurm_unpack_msg_and_forward" in bt
        and "in _try_parse_rpc" in bt
        and "fd >= 0" in bt
    ):
        # TODO: Add version when i51359 is fixed
        return False, reason

    reason = "Issue 51369: slurmd - SIGSEGV in s2n_config_free() from _cleanup_tls_conn() racing certificate renewal"
    component = "sbin/slurmd"
    if (
        component in bin_path
        and "Program terminated with signal SIGSEGV" in bt
        and "in s2n_config_free" in bt
        and "in _cleanup_tls_conn" in bt
    ):
        # TODO: Add version when i51369 is fixed
        return False, reason

    reason = "Issue 51380: slurmd - SIGABRT on shutdown: on_extract(): Assertion (!get_buf_offset(con->in)) failed"
    component = "sbin/slurmd"
    if (
        component in bin_path
        and "Program terminated with signal SIGABRT" in bt
        and "in __xassert_failed" in bt
        and "!get_buf_offset(con->in)" in bt
        and "in on_extract" in bt
    ):
        # TODO: Add version when i51380 is fixed
        return False, reason

    # If coredump is unknown, add it as failure
    return False, f"Unknown coredump detected, see {bt_file}"


# Teardown failures that a crash (or a gcore of a hung daemon) of a component
# typically causes. They take the verdict of the known coredump that explains
# them, see classify_teardown_failure().
JOBS_NOT_CANCELLED = "Not all jobs were successfully cancelled"
DAEMONS_NOT_STOPPED = "Not all Slurm daemons were successfully stopped"
TEARDOWN_SIDE_EFFECTS = {
    "sbin/slurmctld": [JOBS_NOT_CANCELLED, DAEMONS_NOT_STOPPED],
    "sbin/slurmd": [JOBS_NOT_CANCELLED, DAEMONS_NOT_STOPPED],
    "sbin/slurmdbd": [
        DAEMONS_NOT_STOPPED,
        "Failed to drop the accounting database",
        "Failed to create the accounting database",
        "Failed to restore the accounting database dump",
    ],
}


def classify_teardown_failure(message, coredumps):
    """
    Return (xfail, reason) for a teardown failure message.

    The failure is xfailed if it's a typical side effect of a known coredump
    to xfail (see TEARDOWN_SIDE_EFFECTS). Otherwise it's a failure with the
    message as reason.

    coredumps is the list of (bin_path, (xfail, reason)) of the coredumps found
    in the module, as classified by classify_coredump().
    """
    for bin_path, (xfail, reason) in coredumps:
        if not xfail:
            continue
        for component, side_effects in TEARDOWN_SIDE_EFFECTS.items():
            if bin_path.endswith(component) and any(
                side_effect in message for side_effect in side_effects
            ):
                return True, reason

    return False, message


def classify_test_failure():
    """
    Return (xfail, reason) for a known Slurm issue that explains a test
    failure, or None if it's not known.
    xfail is True if the issue is fixed in a newer version than the one tested,
    otherwise the failure is kept but reported with the known reason.

    This is the test-failure counterpart of classify_coredump(): instead of
    matching a coredump backtrace, it looks for the fingerprint of a known issue
    in the Slurm logs.

    The whole logs are checked, not only the part of the failing test, so
    the fingerprint of an earlier test of the same module may also match.

    Using this should be very exceptional. Only for cases that may randomly
    happen in any test.
    """
    if not atf.properties["auto-config"]:
        return None

    # Gather evidences
    ctld_log = atf.get_log("slurmctld")

    reason = "Issue 51306: slurmctld stalled sending an SRUN_* message to srun with an untrusted cert. Fixed in 26.11."
    component = "sbin/slurmctld"
    if (
        atf.get_config_parameter(
            "TLSType", default="tls/none", live=False, quiet=True
        ).casefold()
        != "tls/none"
    ):
        stalls = []
        last_spawn = None
        for line in ctld_log.splitlines():
            spawn = "Spawning RPC agent for msg_type SRUN_" in line
            if not spawn and "S2N_ERR_CERT_UNTRUSTED" not in line:
                continue
            match = re.match(r"\[(\d{4}-\d\d-\d\dT[\d:.]+)\]", line)
            if not match:
                continue
            ts = datetime.datetime.fromisoformat(match.group(1)).timestamp()
            if spawn:
                last_spawn = (ts, line)
            elif last_spawn and ts - last_spawn[0] >= 5:
                stalls += [last_spawn[1], line]
        if stalls:
            logging.warning(f"{reason}:\n" + "\n".join(stalls))
            if atf.get_version(component) >= (26, 11):
                return False, reason
            else:
                # convert to xfail
                return True, reason

    reason = "Ticket 25141: Known issue with slurmctld on reconfigure: SIGABRT in jobacct_storage_g_step_complete(): Assertion (plugin_inited != PLUGIN_NOT_INITED). Fixed in 25.11+"
    component = "sbin/slurmctld"
    if (
        "jobacct_storage_g_step_complete(): Assertion (plugin_inited != PLUGIN_NOT_INITED) failed."
        in ctld_log
    ):
        if atf.get_version(component) >= (25, 11):
            return False, reason
        else:
            return True, reason

    return None
