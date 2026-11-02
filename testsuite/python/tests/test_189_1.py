############################################################################
# Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
############################################################################
"""Per-node config_overrides applies on the registration that reports it.

slurmd always registers physical memory. The controller compares that with
the configured RealMemory and marks the node INVAL when the host is short,
unless config_overrides applies to that node. The flag may arrive only in
this registration (slurmd --parameters), and the memory check has to see it
then. A global SlurmdParameters flag is not set here, and slurmctld is not
given the flag in slurm.conf, so the previous registration cannot already
have it.

The node is first brought up with a tiny RealMemory so startup can see it
IDLE. The test then raises RealMemory far above the host and reconfigures.
One slurmd restart carries config_overrides. A second restart would store
the flag on the first pass and then pass for the wrong reason.
"""

import re

import pytest

import atf

pytestmark = pytest.mark.slow

NODE = "node1"  # require_nodes(1, ...) yields node1

# Far above any host this suite runs on, so reported physical memory is
# below 100% of configured RealMemory.
HUGE_MEMORY = 100000000


@pytest.fixture(scope="module", autouse=True)
def setup():
    atf.require_config_parameter("SelectType", "select/cons_tres")
    atf.require_config_parameter("SelectTypeParameters", "CR_CPU")
    # ATF enables config_overrides by default. Drop it so the global flag
    # cannot satisfy the memory check. The test supplies it per node.
    atf.require_config_parameter_excludes("SlurmdParameters", "config_overrides")
    # Small enough that the host's physical memory passes the first registration.
    atf.require_nodes(1, [("CPUs", 2), ("RealMemory", 1)])
    atf.require_version(
        (26, 11),
        component="sbin/slurmctld",
        reason="Ticket 25180: per-node config_overrides is applied at registration in 26.11",
    )
    atf.require_version(
        (25, 11),
        component="bin/scontrol",
        reason="Ticket 22862: per-node Parameters= in slurm.conf and scontrol show node added in 25.11",
    )
    atf.require_slurm_running()


@pytest.fixture
def restore_node_line():
    """Put RealMemory and Parameters back on the node line after the test."""
    yield
    atf.set_node_parameter(NODE, "RealMemory", 1)
    atf.set_node_parameter(NODE, "Parameters", None)


def _restart_slurmd(parameters):
    """Restart this node's slurmd once with ``--parameters``.

    Returns when the controller has the registration of the new slurmd.
    """
    atf.stop_slurmd(NODE)
    atf.start_slurmd(NODE, args=f"--parameters={parameters}", fatal=True)


def _set_real_memory(memory):
    """Write RealMemory on this node's slurm.conf line. Does not reconfigure."""
    path = f"{atf.properties['slurm-config-dir']}/slurm.conf"
    text = atf.run_command_output(
        f"cat {path}", user=atf.properties["slurm-user"], quiet=True
    )
    found = False
    lines = []
    for line in text.splitlines():
        if re.match(rf"NodeName={NODE}(\s|$)", line):
            if re.search(r"(^|\s)RealMemory=\S+", line):
                line = re.sub(
                    r"(^|\s)RealMemory=\S+",
                    lambda m: f"{m.group(1)}RealMemory={memory}",
                    line,
                )
            else:
                line += f" RealMemory={memory}"
            found = True
        lines.append(line)
    if not found:
        pytest.fail(f"NodeName={NODE} was not found in {path}")
    atf.run_command(
        f"cat > {path}",
        input="\n".join(lines) + "\n",
        user=atf.properties["slurm-user"],
        fatal=True,
    )


def test_config_overrides_applies_on_that_registration():
    """Raising RealMemory above the host does not leave the node INVAL when
    the same registration reports config_overrides."""
    for _ in atf.timer(timeout=60, fatal=True):
        state = atf.get_node_field(NODE, "State")
        if state is not None and "DOWN" not in state and "INVAL" not in state:
            break
    parameters = atf.get_node_field(NODE, "Parameters") or ""
    assert "config_override" not in parameters, atf.get_node_detail(NODE)

    _set_real_memory(HUGE_MEMORY)
    atf.run_command(
        "scontrol reconfigure", user=atf.properties["slurm-user"], fatal=True
    )
    for _ in atf.timer(timeout=60, fatal=True):
        if atf.get_node_field(NODE, "RealMemory") == str(HUGE_MEMORY):
            break

    # Keep whatever this node already reports and add the flag.
    # --parameters replaces the daemon's global list.
    reported = parameters
    if reported and not reported.endswith(","):
        reported += ","
    reported += "config_overrides"

    _restart_slurmd(reported)

    # The registration that reported config_overrides must not leave the
    # node invalid.
    for _ in atf.timer(timeout=60, fatal=True):
        state = atf.get_node_field(NODE, "State")
        if state is not None and "INVAL" not in state and "DOWN" not in state:
            break
    memory = atf.get_node_field(NODE, "RealMemory")
    assert memory == str(HUGE_MEMORY), atf.get_node_detail(NODE)
    parameters = atf.get_node_field(NODE, "Parameters") or ""
    assert "config_override" in parameters, atf.get_node_detail(NODE)


def test_node_line_config_overrides_applies(restore_node_line):
    """config_overrides on the node's slurm.conf line also applies.

    A node's Parameters= are added to SlurmdParameters for that node, so with
    config_overrides there a RealMemory above the host does not make the node
    INVAL either.
    """
    atf.set_node_parameter(NODE, "Parameters", "config_overrides")
    atf.set_node_parameter(NODE, "RealMemory", HUGE_MEMORY)
    for _ in atf.timer(timeout=60, fatal=True):
        state = atf.get_node_field(NODE, "State")
        if state is not None and "INVAL" not in state and "DOWN" not in state:
            break
    memory = atf.get_node_field(NODE, "RealMemory")
    assert memory == str(HUGE_MEMORY), atf.get_node_detail(NODE)
    parameters = atf.get_node_field(NODE, "Parameters") or ""
    assert "config_override" in parameters, atf.get_node_detail(NODE)
