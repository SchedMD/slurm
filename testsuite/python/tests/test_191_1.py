############################################################################
# Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
############################################################################
"""scontrol hash and hash_file against independent reference digests.

scontrol computes these hashes locally with the plugins named by HashPlugin in
its own slurm.conf: the first one listed is the default, and hash/k12 is always
loaded as well. A client-only slurm.conf that includes the real one and sets
HashPlugin=hash/sha3 therefore exercises hash/sha3 without reconfiguring any
daemon. The same client is rejected by K12-only daemons, which is what shows
that HashPlugin selects the hash used for network communication.
"""

import hashlib
import os
import shlex

import pytest

import atf

# KangarooTwelve (RFC 9861) with an empty customization string, 32 bytes.
K12_DIGESTS = {
    "": "1ac2d450fc3b4205d19da7bfca1b37513c0803577ac7167f06fe2ce1f0ef39e5",
    "test": "333bd5d6c148a56b3eb4ecd489d07cd13f563a5a2593ece9155ca68772671628",
    "Hello, World!": "58650c76f081423ae01f3aea4b301b5682550129be77aac1bd9333d8f1c97563",
    "Your text here": "23e9976374d3ce4314a21b28eb780172f0e04188380ebd37eb541a1163e47e9a",
}

DIGESTS = {
    "k12": K12_DIGESTS,
    "sha3": {s: hashlib.sha3_256(s.encode()).hexdigest() for s in K12_DIGESTS},
}

# RFC 9861 KT128 vector for M=ptn(17**4), 83521 bytes: more than one 8192-byte
# chunk, so K12 hashes it as a tree, and SHA3-256 absorbs it over many blocks.
LONG_INPUT = bytes(i % 251 for i in range(17**4))
LONG_DIGESTS = {
    "k12": "8701045e22205345ff4dda05555cbb5c3af1a771c2b89baef37db43d9998b9fe",
    "sha3": hashlib.sha3_256(LONG_INPUT).hexdigest(),
}

# (client HashPlugin, hash_type argument, expected algorithm)
HASH_CASES = [
    ("default", "", "k12"),
    ("default", "2", "k12"),
    ("sha3", "", "sha3"),
    ("sha3", "4", "sha3"),
    ("sha3", "2", "k12"),
]


@pytest.fixture(scope="module", autouse=True)
def setup():
    atf.require_config_parameter("HashPlugin", [None, "hash/k12"])
    atf.require_config_parameter("HashPlugin", [None, "hash/k12"], source="slurmdbd")
    atf.require_accounting()
    atf.require_slurm_running()


@pytest.fixture(scope="module")
def client_env(module_setup):
    """SLURM_CONF settings selecting each HashPlugin for the client only."""
    real_conf = f"{atf.properties['slurm-config-dir']}/slurm.conf"
    env = {"default": None}
    for plugin in ["k12", "sha3"]:
        path = f"{plugin}_slurm.conf"
        atf.run_command(
            f"printf 'include {real_conf}\\nHashPlugin=hash/{plugin}\\n' > {path}",
            fatal=True,
        )
        env[plugin] = f"SLURM_CONF={os.path.abspath(path)}"
    return env


@pytest.fixture(scope="module")
def hash_files(module_setup):
    """A file per non-empty input holding exactly its bytes.

    hash_file mmap()s the file, which cannot map an empty one.
    """
    files = {}
    for i, text in enumerate(s for s in K12_DIGESTS if s):
        path = f"input_{i}"
        atf.run_command(f"printf '%s' {shlex.quote(text)} > {path}", fatal=True)
        files[text] = os.path.abspath(path)
    return files


@pytest.fixture(scope="module")
def long_file(module_setup):
    """A file holding LONG_INPUT."""
    path = "input_long"
    atf.run_command(
        "python3 -c 'import sys; "
        f"sys.stdout.buffer.write(bytes(i % 251 for i in range(17**4)))' > {path}",
        fatal=True,
    )
    return os.path.abspath(path)


@pytest.mark.parametrize("plugin,hash_type,algorithm", HASH_CASES)
def test_hash(client_env, plugin, hash_type, algorithm):
    """scontrol hash matches the reference digest of the selected algorithm"""

    got = {
        text: atf.run_command_output(
            f"scontrol hash {shlex.quote(text)} {hash_type}",
            env_vars=client_env[plugin],
            fatal=True,
        ).strip()
        for text in K12_DIGESTS
    }
    assert got == DIGESTS[algorithm], f"scontrol hash should give {algorithm} digests"


@pytest.mark.parametrize("plugin,hash_type,algorithm", HASH_CASES)
def test_hash_file(client_env, hash_files, plugin, hash_type, algorithm):
    """scontrol hash_file matches the reference digest of the file's bytes"""

    got = {
        text: atf.run_command_output(
            f"scontrol hash_file {path} {hash_type}",
            env_vars=client_env[plugin],
            fatal=True,
        ).strip()
        for text, path in hash_files.items()
    }
    assert got == {
        text: DIGESTS[algorithm][text] for text in hash_files
    }, f"scontrol hash_file should give {algorithm} digests"


@pytest.mark.parametrize("plugin,hash_type,algorithm", HASH_CASES)
def test_hash_file_long(client_env, long_file, plugin, hash_type, algorithm):
    """scontrol hash_file matches the reference digest of a multi-block file"""

    got = atf.run_command_output(
        f"scontrol hash_file {long_file} {hash_type}",
        env_vars=client_env[plugin],
        fatal=True,
    ).strip()
    assert (
        got == LONG_DIGESTS[algorithm]
    ), f"scontrol hash_file should give {algorithm} digest"


@pytest.mark.parametrize("command", ["sacctmgr -n show cluster", "sinfo"])
def test_sha3_client_rejected(client_env, command):
    """A hash/sha3 client is rejected by K12-only slurmdbd and slurmctld"""

    result = atf.run_command(
        command, env_vars=client_env["sha3"], xfail=True, fatal=True
    )
    assert (
        result["exit_code"] != 0
    ), f"'{command}' should fail when its client hashes with SHA3"

    atf.run_command(command, env_vars=client_env["k12"], fatal=True)
