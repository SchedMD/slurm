############################################################################
# Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
############################################################################
"""Shared readers for the TresDecayHalfLife modules.

Ticket 50920: every module that measures per-TRES decay reads the same numbers
out of 'scontrol show assoc_mgr' - the GrpTRESMins usage for one TRES, and the
fairshare RawUsage - and has to pick the right block out of the output before it
can. That parsing lives here so the modules do not each carry a copy.

What stays in the modules is everything they legitimately disagree about: how
accounts are named, what the accrual looks like, and when a window closes.
"""

import re

import pytest

import atf


def account_block(acct):
    """The account's own association record, as scontrol prints it.

    'account=' filters on the account only, so the output also carries an
    association per user beneath it. Those inherit the same half-life, so any
    block would answer today, but the account's own record is the one these
    tests set, and it is the one with no user on it.
    """
    out = atf.run_command_output(
        f"scontrol show assoc_mgr account={acct} flags=assoc",
        user=atf.properties["slurm-user"],
        fatal=True,
    )
    for block in out.split("ClusterName=")[1:]:
        if re.search(r"UserName=\s*(?:Partition=|$)", block, re.M):
            return block
    pytest.fail(f"no account-level association for {acct} in: {out!r}")


def qos_block(qos):
    """The QOS record, as scontrol prints it.

    A QOS has no hierarchy under it, so unlike an account there is only ever
    the one block to read.
    """
    return atf.run_command_output(
        f"scontrol show assoc_mgr qos={qos} flags=qos",
        user=atf.properties["slurm-user"],
        fatal=True,
    )


def tres_usage(block, tres, label):
    """TRES-minutes of GrpTRESMins usage for one TRES within a block.

    GrpTRESMins=cpu=N(used),mem=M(used),... - the parenthesised value is the
    usage. Anchored on the TRES name so mem cannot be read off the cpu entry.
    """
    match = re.search(
        r"GrpTRESMins=[^\n]*?\b" + re.escape(tres) + r"=[^(,\n]*\((\d+)\)",
        block,
    )
    if not match:
        pytest.fail(f"no GrpTRESMins {tres} usage for {label} in: {block!r}")
    return int(match.group(1))


def account_usage(acct):
    """What the controller holds for acct, both usages in one sample.

    The two are read from the same command so nothing can decay between them,
    and they are the two accumulators the feature has to keep apart:
    GrpTRESMins usage per TRES, and the fairshare RawUsage.
    """
    block = account_block(acct)
    # UsageRaw/Norm/Efctv=1234.00/...
    raw = re.search(r"UsageRaw/Norm/Efctv=([\d.]+)/", block)
    if not raw:
        pytest.fail(f"no RawUsage for {acct} in: {block!r}")
    return tres_usage(block, "cpu", acct), float(raw.group(1))


def cpu_usage(acct):
    """TRES-minutes of GrpTRESMins cpu usage the controller holds for acct."""
    return tres_usage(account_block(acct), "cpu", acct)


def mem_usage(acct):
    """TRES-minutes of GrpTRESMins mem usage the controller holds for acct."""
    return tres_usage(account_block(acct), "mem", acct)


def qos_cpu_usage(qos):
    """TRES-minutes of GrpTRESMins cpu usage the controller holds for a QOS."""
    return tres_usage(qos_block(qos), "cpu", f"QOS {qos}")


def reported_half_life(acct):
    """The half-life the controller reports for acct, as sacctmgr wrote it.

    Blank for an account with none of its own, which is what one that has never
    been modified reports.
    """
    block = account_block(acct)
    match = re.search(r"TresDecayHalfLife=(\S*)", block)
    if not match:
        pytest.fail(f"no TresDecayHalfLife line for {acct} in: {block!r}")
    return match.group(1)
