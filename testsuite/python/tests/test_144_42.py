############################################################################
# Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
############################################################################
"""A gres.conf Cores= list must cover whole sockets of cores the node has.

A Cores= list that covers a whole socket is kept. One that splits a socket
is refused, and one naming a core past the node's last is an invalid core
specification.

The node topology matches this host (one socket, eight cores). A core spec is
checked against the topology slurmd reports, which is the hardware.
"""

import pytest

import atf

pytestmark = pytest.mark.slow

NODE = "node1"


@pytest.fixture(scope="module", autouse=True)
def setup():
    atf.require_config_parameter("SelectType", "select/cons_tres")
    atf.require_config_parameter("SelectTypeParameters", "CR_CPU")
    atf.require_config_parameter_includes("GresTypes", "gpu")
    atf.require_config_parameter_excludes("SlurmdParameters", "config_overrides")
    atf.require_tty(0)
    atf.require_tty(1)
    atf.require_config_file("gres.conf", _cores_conf("0-7"))
    atf.require_nodes(
        1,
        [
            ("Gres", "gpu:a100:2"),
            ("CPUs", 8),
            ("Sockets", 1),
            ("CoresPerSocket", 8),
        ],
    )
    atf.require_slurm_running()


def _cores_conf(cores, count=2):
    lines = "AutoDetect=off\n"
    lines += f"NodeName={NODE} Name=gpu File=/dev/tty0 Type=a100 Cores={cores}\n"
    if count > 1:
        lines += f"NodeName={NODE} Name=gpu File=/dev/tty1 Type=a100\n"
    return lines


def _register(gres_conf, count):
    """Make this gres.conf the node's registration at this configured count."""
    atf.stop_slurmd(NODE, quiet=True)
    # A detected device has no core spec, and slurmd would discard the
    # gres.conf line for disagreeing with it.
    atf.require_config_file("fake_gpus.conf", "")
    atf.require_config_file("gres.conf", gres_conf)
    atf.stop_slurmctld(quiet=True)
    atf.set_node_parameter(NODE, "Gres", f"gpu:a100:{count}")
    atf.restart_slurmctld(clean=True, quiet=True)
    atf.start_slurmd(NODE)


def test_full_socket_core_spec_is_kept():
    """A Cores= list that covers the whole socket binds the GPU to it."""

    _register(_cores_conf("0-7"), count=2)
    for _ in atf.timer(fatal=True):
        if "(S:0)" in (atf.get_node_detail(NODE) or ""):
            break


def test_core_spec_splitting_a_socket_is_refused():
    """A Cores= list that covers part of a socket is refused."""

    _register(_cores_conf("0-1", count=1), count=1)
    for _ in atf.timer(fatal=True):
        if "doesn't match socket boundaries" in (atf.get_node_detail(NODE) or ""):
            break


def test_core_past_the_last_is_invalid():
    """A Cores= list naming a core the node does not have is invalid.

    bit_unfmt() reads a word like "zzz" as core 0, so the case the parser
    rejects is a core past the end of this node's core bitmap.
    """

    _register(_cores_conf("8", count=1), count=1)
    log = atf.get_config_parameter("SlurmctldLogFile", live=False, quiet=True)
    for _ in atf.timer(fatal=True):
        text = atf.run_command_output(
            f"cat {log}", user=atf.properties["slurm-user"], quiet=True
        )
        if "invalid GRES core specification (8)" in text:
            break
