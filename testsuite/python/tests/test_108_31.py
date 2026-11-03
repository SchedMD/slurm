############################################################################
# Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
############################################################################
"""Test scontrol write config keeps values with spaces or parentheses whole.

scontrol write config strips annotations such as the " sec" in "60 sec" from
the values reported by scontrol show config. That must not cut a value that
legitimately contains a space, like the arguments of ResumeFailProgram or the
default of InteractiveStepOptions, or a "(", like the "name(address)" form of
SlurmctldHost.
"""

import re
import socket

import pytest

import atf

# (name written to the file, name in scontrol show config)
PARAMETERS = [
    ("InteractiveStepOptions", "InteractiveStepOptions"),
    ("ResumeFailProgram", "ResumeFailProgram"),
    ("SlurmctldHost", "SlurmctldHost[0]"),
]


@pytest.fixture(scope="module", autouse=True)
def setup():
    atf.require_version(
        (26, 11),
        "bin/scontrol",
        reason="write config truncated these values before 26.11",
    )
    # InteractiveStepOptions needs no setting: its default has spaces
    atf.require_config_parameter("ResumeFailProgram", '"/bin/true --fail"')

    # A controller address that differs from its name is written as
    # "name(address)". The host's own address keeps the cluster working.
    host = atf.get_config_parameter("SlurmctldHost", live=False).split("(")[0]
    atf.require_config_parameter(
        "SlurmctldHost", f"{host}({socket.gethostbyname(host)})"
    )
    atf.require_slurm_running()


@pytest.fixture(scope="module")
def shown():
    return atf.get_config()


@pytest.fixture(scope="module")
def written():
    output = atf.run_command_output("scontrol write config slurm.conf.out", fatal=True)
    match = re.search(r"Slurm config saved to (\S+)", output)
    assert match, f"scontrol write config did not report a file: {output}"

    content = atf.run_command_output(f"cat {match.group(1)}", fatal=True)
    # SlurmctldHost is repeated for backup controllers: keep the first, which
    # is the primary
    lines = {}
    for name, value in re.findall(r"^([A-Za-z]+)=(.*)$", content, re.MULTILINE):
        lines.setdefault(name, value)
    return lines


@pytest.mark.parametrize("written_name,shown_name", PARAMETERS)
def test_value_written_whole(shown, written, written_name, shown_name):
    """The value is written exactly as scontrol show config reports it"""

    value = shown[shown_name]
    assert re.search(
        r"[ (]", value
    ), f"{shown_name} ({value!r}) has no space or parenthesis to be cut at"
    assert written_name in written, f"{written_name} was not written"

    # Values containing spaces must be written inside double quotes
    expected = f'"{value}"' if " " in value else value
    assert (
        written[written_name] == expected
    ), f"write config wrote {written[written_name]!r}, show config reports {value!r}"
