############################################################################
# Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
############################################################################
"""
GRES-autofill coverage for dynamic-normal slurmds (`slurmd -Z`).

Each test spins up a fresh slurmd under a disposable node name, waits
for IDLE registration, then inspects its Gres string and slurmd log to
verify the autofill behavior of gres_get_dynamic_gpu_str() called from
_dynamic_init().
"""

import atf
import pytest
import re

pytestmark = pytest.mark.slow


# Needs to be in the form of nodeNN for the s2n variant
dyn_node = "node100"
reg_timeout = 30


def _write_raw_conf(filename, content):
    path = f"{atf.properties['slurm-config-dir']}/{filename}"
    atf.run_command(
        f"cat > {path}",
        input=content,
        user=atf.properties["slurm-user"],
        fatal=True,
        quiet=True,
    )


def _write_gres_conf(content):
    _write_raw_conf("gres.conf", content)


def _write_fake_gpus(content):
    _write_raw_conf("fake_gpus.conf", content)


def _slurmd_log_path(node):
    return f"{atf.properties['slurm-logs-dir']}/slurmd.{node}.log"


def _truncate_slurmd_log(node):
    """Truncate the per-node slurmd log so log scans only see the new run.

    All `slurmd -Z -N <node>` invocations append to the same file, so without
    this, log-text assertions would catch lines emitted by previous tests'
    slurmds that registered under the same node name.
    """
    atf.run_command(
        f": > {_slurmd_log_path(node)}",
        user="root",
        quiet=True,
    )


def _slurmd_log_text(node):
    """Read the per-node slurmd log."""
    return (
        atf.run_command_output(f"cat {_slurmd_log_path(node)}", user="root", quiet=True)
        or ""
    )


def _autofill_log_re(node):
    return rf"Autodetected Gres for dynamic node {re.escape(node)}:"


def _assert_gres_has(node, expected_gres):
    gres = atf.get_node_parameter(node, "gres") or ""
    assert re.search(rf"{re.escape(expected_gres)}\b", gres, re.IGNORECASE), (
        f"expected {expected_gres} in Gres on dynamic node {node}, "
        f"got Gres={gres!r}"
    )


def _wait_for_node_idle(node_name, timeout=reg_timeout):
    """Wait for ``node_name`` to be IDLE and not DRAIN.

    ``atf.wait_for_node_state`` polls via ``get_node_parameter`` which calls
    ``pytest.fail`` while the node is still arriving in the controller's
    table; that exception escapes ``repeat_until`` instead of being treated
    as "not yet, keep polling". Drive the poll directly off ``get_nodes`` so
    the missing-node window is tolerated. Failing fast on DRAIN also keeps
    us from masking a real INVALID_REG with a generic timeout.
    """

    def _state():
        nodes = atf.get_nodes(quiet=True)
        return nodes.get(node_name, {}).get("state")

    return atf.repeat_until(
        _state,
        lambda s: bool(s) and "IDLE" in s and "DRAIN" not in s,
        timeout=timeout,
    )


@pytest.fixture(scope="module", autouse=True)
def setup():
    atf.require_auto_config("dynamic-norm Autodetect=full default and fake_gpus.conf")
    # Needed so `slurmd -Z` dynamic registrations are accepted.
    atf.require_config_parameter("MaxNodeCount", 4)
    atf.require_config_parameter("TreeWidth", 65533)
    atf.require_config_parameter_includes("SlurmctldParameters", "cloud_reg_addrs")
    atf.require_config_parameter("SelectType", "select/cons_tres")
    atf.require_config_parameter("SelectTypeParameters", "CR_CPU")
    atf.require_config_parameter_includes("GresTypes", "gpu")
    atf.require_config_parameter_includes("GresTypes", "bandwidth")
    atf.require_tty(0)
    atf.require_tty(1)
    atf.require_slurm_running()


@pytest.fixture
def dynamic_slurmd():
    """Start `slurmd -Z -b` with a disposable node name; kill+delete it."""
    started = []

    def _start(node_name=dyn_node, extra_conf=""):
        _truncate_slurmd_log(node_name)
        extra_conf_arg = f" --conf '{extra_conf}'" if extra_conf else ""
        atf.run_command(
            f"{atf.properties['slurm-sbin-dir']}/slurmd "
            f"-N {node_name} -Z -v -b{extra_conf_arg}",
            user="root",
            fatal=True,
        )
        started.append(node_name)
        assert _wait_for_node_idle(
            node_name, timeout=reg_timeout
        ), f"dynamic node {node_name} never became IDLE (no DRAIN)"
        return node_name

    yield _start

    for name in started:
        pid_out = (
            atf.run_command_output(
                f"pgrep -f '{atf.properties['slurm-sbin-dir']}/slurmd -N {name} -Z'",
                quiet=True,
            )
            or ""
        )
        for pid in pid_out.split():
            atf.run_command(f"kill {pid}", user="root", quiet=True)
        atf.run_command(
            f"scontrol delete NodeName={name}",
            user=atf.properties["slurm-user"],
            quiet=True,
        )


@pytest.mark.skipif(
    atf.get_version("sbin/slurmd") < (26, 5),
    reason="Issue 50653: slurmd -Z defaults to Autodetect=Full in 26.05",
)
def test_dynamic_norm_default_autodetect_full_adopts_fake_gpu(dynamic_slurmd):
    """Empty gres.conf: autofill promotes to GPU_FULL and adopts the fake GPU."""
    _write_gres_conf("")
    _write_fake_gpus("(null)|2|(null)|(null)|/dev/tty0|(null)|nvidia_gpu_env\n")
    atf.restart_slurm(quiet=True)

    node = dynamic_slurmd()
    _assert_gres_has(node, "gpu:1")


@pytest.mark.skipif(
    atf.get_version("sbin/slurmd") < (26, 5),
    reason="Issue 50653: Autodetect=Full in gres.conf added in 26.05",
)
def test_dynamic_norm_explicit_autodetect_full_adopts_fake_gpu(dynamic_slurmd):
    """Autodetect=full in gres.conf uses the full plugin probe path."""
    _write_gres_conf("Autodetect=full\n")
    _write_fake_gpus("a100|2|(null)|(null)|/dev/tty0|(null)|nvidia_gpu_env\n")
    atf.restart_slurm(quiet=True)

    node = dynamic_slurmd()
    _assert_gres_has(node, "gpu:a100:1")


@pytest.mark.skipif(
    atf.get_version("sbin/slurmd") < (26, 5),
    reason="Issue 50653: slurmd -Z defaults to Autodetect=Full in 26.05",
)
def test_dynamic_norm_autofills_gres_into_nodeline(dynamic_slurmd):
    """Bare `slurmd -Z` autofills Gres= and emits the autofill info log line.

    Without the autofill, the controller would see Gres=(null) and cap the
    count to 0, even though the slurmd-side adopt-all sees the GPUs locally.
    """
    _write_gres_conf("")
    _write_fake_gpus("a100|2|(null)|(null)|/dev/tty0|(null)|nvidia_gpu_env\n")
    atf.restart_slurm(quiet=True)

    node = dynamic_slurmd()

    _assert_gres_has(node, "gpu:a100:1")

    log_text = _slurmd_log_text(node)
    assert re.search(
        rf"Autodetected Gres for dynamic node {re.escape(node)}: gpu:a100:1\b",
        log_text,
    ), (
        f"slurmd log for {node} did not contain the autofill info line; "
        f"_dynamic_init() autofill path may not have fired"
    )


def test_dynamic_norm_autodetect_full_no_gpus_starts_without_gres(dynamic_slurmd):
    """Default Autodetect=full with no GPUs falls back cleanly."""
    _write_gres_conf("")
    _write_fake_gpus("")
    atf.restart_slurm(quiet=True)

    node = dynamic_slurmd()
    gres = atf.get_node_parameter(node, "gres") or ""
    assert not re.search(
        r"\bgpu\b", gres, re.IGNORECASE
    ), f"expected no GPU GRES when no GPUs are detected, got Gres={gres!r}"

    log_text = _slurmd_log_text(node)
    assert not re.search(
        _autofill_log_re(node), log_text
    ), "autofill log line found even though no GPUs were detected"


def test_dynamic_norm_skips_autofill_when_gres_conf_has_name_gpu(dynamic_slurmd):
    """Name=gpu in gres.conf for this node suppresses the autofill probe."""
    _write_gres_conf(f"NodeName={dyn_node} Name=gpu File=/dev/tty0 Type=a100\n")
    _write_fake_gpus("a100|2|(null)|(null)|/dev/tty0|(null)|nvidia_gpu_env\n")
    atf.restart_slurm(quiet=True)

    node = dynamic_slurmd()
    log_text = _slurmd_log_text(node)
    assert not re.search(_autofill_log_re(node), log_text), (
        "autofill log line found despite Name=gpu in gres.conf; "
        "early-return guard in gres_get_dynamic_gpu_str() did not fire"
    )


@pytest.mark.skipif(
    atf.get_version("sbin/slurmd") < (26, 5),
    reason="Issue 50653: slurmd -Z defaults to Autodetect=Full in 26.05",
)
def test_dynamic_norm_autofills_when_name_gpu_scoped_to_other_node(dynamic_slurmd):
    """Name=gpu scoped to a different NodeName= must NOT suppress autofill here.

    Regression test for the bug where gres_node_name was assigned only
    *after* s_p_parse_file, leaving the NodeName= filter inert and causing
    the foreign Name=gpu record to leak into the local gres_conf_list.
    """
    _write_gres_conf("NodeName=some_other_node Name=gpu File=/dev/tty1 Type=v100\n")
    _write_fake_gpus("a100|2|(null)|(null)|/dev/tty0|(null)|nvidia_gpu_env\n")
    atf.restart_slurm(quiet=True)

    node = dynamic_slurmd()
    _assert_gres_has(node, "gpu:a100:1")

    log_text = _slurmd_log_text(node)
    assert re.search(_autofill_log_re(node), log_text), (
        f"slurmd log for {node} did not contain the autofill info line; "
        f"_parse_gres_config_node() may have failed to filter the foreign "
        f"NodeName= record"
    )


def test_dynamic_norm_honors_autodetect_off(dynamic_slurmd):
    """Autodetect=off suppresses the autofill probe."""
    _write_gres_conf("Autodetect=off\n")
    _write_fake_gpus("a100|2|(null)|(null)|/dev/tty0|(null)|nvidia_gpu_env\n")
    atf.restart_slurm(quiet=True)

    node = dynamic_slurmd()
    log_text = _slurmd_log_text(node)
    assert not re.search(_autofill_log_re(node), log_text), (
        "autofill log line found despite Autodetect=off in gres.conf; "
        "GRES_AUTODETECT_GPU_OFF guard in gres_get_dynamic_gpu_str() did "
        "not fire"
    )


def test_dynamic_norm_preserves_other_gres_without_gpu_autofill(dynamic_slurmd):
    """Preserve non-GPU --conf Gres= when GPU autofill returns NULL."""
    _write_gres_conf("Autodetect=off\n")
    _write_fake_gpus("a100|2|(null)|(null)|/dev/tty0|(null)|nvidia_gpu_env\n")
    atf.restart_slurm(quiet=True)

    node = dynamic_slurmd(extra_conf="Gres=bandwidth:lustre:no_consume:4G Feature=f1")
    gres = atf.get_node_parameter(node, "gres") or ""
    assert re.search(r"bandwidth:lustre\b", gres, re.IGNORECASE), (
        f"expected non-GPU GRES from --conf to be preserved after "
        f"gres_get_dynamic_gpu_str() returned NULL, got Gres={gres!r}"
    )

    log_text = _slurmd_log_text(node)
    assert not re.search(_autofill_log_re(node), log_text), (
        "autofill log line found despite Autodetect=off in gres.conf; "
        "test did not exercise the NULL-return path"
    )


def test_dynamic_norm_preserves_explicit_real_memory(dynamic_slurmd):
    """Preserve explicit --conf RealMemory= through dynamic nodeline rewrite."""
    _write_gres_conf("Autodetect=off\n")
    _write_fake_gpus("")
    atf.restart_slurm(quiet=True)

    node = dynamic_slurmd(extra_conf="RealMemory=1 Feature=f1")
    real_memory = atf.get_node_parameter(node, "real_memory")
    assert int(real_memory) == 1, (
        f"expected --conf RealMemory=1 to be preserved, got "
        f"RealMemory={real_memory!r}"
    )


def test_dynamic_norm_honors_specific_autodetect(dynamic_slurmd):
    """Autodetect=<gpu_plugin> must NOT be silently promoted to FULL."""
    _write_gres_conf("Autodetect=nvml\n")
    _write_fake_gpus("a100|2|(null)|(null)|/dev/tty0|(null)|nvidia_gpu_env\n")
    atf.restart_slurm(quiet=True)

    node = dynamic_slurmd()
    log_text = _slurmd_log_text(node)
    assert not re.search(r"autodetect=full selected", log_text), (
        "_gpu_plugin_init_full() ran despite Autodetect=nvml in gres.conf; "
        "gres_get_dynamic_gpu_str() incorrectly promoted to GPU_FULL"
    )


def test_dynamic_norm_explicit_gres_conf_disables_adopt_all(dynamic_slurmd):
    """Name=gpu plus --conf 'Gres=...:1' caps the merge at the admin count."""
    _write_gres_conf(f"NodeName={dyn_node} Name=gpu File=/dev/tty0 Type=a100\n")
    _write_fake_gpus(
        "a100|2|(null)|(null)|/dev/tty0|(null)|nvidia_gpu_env\n"
        "a100|2|(null)|(null)|/dev/tty1|(null)|nvidia_gpu_env\n"
    )
    atf.restart_slurm(quiet=True)

    node = dynamic_slurmd(extra_conf="Gres=gpu:a100:1")
    gres = atf.get_node_parameter(node, "gres") or ""
    assert re.search(
        r"gpu:a100:1\b", gres, re.IGNORECASE
    ), f"expected gpu:a100:1 (merge path), got Gres={gres!r}"
    assert not re.search(
        r"gpu:a100:2\b", gres, re.IGNORECASE
    ), f"adopt-all appears to have fired, got Gres={gres!r}"


@pytest.mark.skipif(
    atf.get_version("sbin/slurmd") < (26, 5),
    reason="Issue 50653: slurmd -Z defaults to Autodetect=Full in 26.05",
)
def test_dynamic_norm_autofills_gpu_with_other_gres_in_conf(dynamic_slurmd):
    """A non-GPU --conf Gres entry does not suppress GPU autofill."""
    _write_gres_conf("")
    _write_fake_gpus("a100|2|(null)|(null)|/dev/tty0|(null)|nvidia_gpu_env\n")
    atf.restart_slurm(quiet=True)

    node = dynamic_slurmd(extra_conf="Gres=bandwidth:lustre:no_consume:4G")
    gres = atf.get_node_parameter(node, "gres") or ""
    assert re.search(
        r"gpu:a100:1\b", gres, re.IGNORECASE
    ), f"expected GPU autofill alongside non-GPU GRES, got Gres={gres!r}"
    assert re.search(
        r"bandwidth:lustre\b", gres, re.IGNORECASE
    ), f"expected non-GPU GRES from --conf to be preserved, got Gres={gres!r}"
