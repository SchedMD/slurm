############################################################################
# Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
############################################################################
"""Test sacct --expand-patterns for the StdIn, StdOut and StdErr fields.

Every filename pattern specifier documented in the FILENAME PATTERN section of
sbatch(1) is exercised through a single job per shape, and each reader (plain
text and --json) is compared against the same table of expected expansions.
"""

import json
import os

import pytest

import atf

JOB_NAME = "expandpat"
ACCOUNT = "expandpat"

# Specifiers whose expansion does not depend on the job id, the node or the
# array index, so the expanded stdin filename is known before any job exists.
STDIN_SPECS = ["%%", "%n", "%s", "%t", "%u", "%x"]

# Specifiers valid for any batch job.  %r and %S were added in 25.11, and
# before 25.11 the width modifier was ignored for %t, which was hardcoded to
# "0" rather than being padded.
BASE_SPECS = [
    "%%",
    "%J",
    "%j",
    "%4j",
    "%20j",
    "%N",
    "%4N",
    "%n",
    "%s",
    "%t",
    "%u",
    "%x",
]
if atf.get_version("bin/sacct") >= (25, 11):
    BASE_SPECS = BASE_SPECS + ["%4t", "%r", "%S"]

# %A, %a and %b are only documented for job arrays, so they are only ever
# exercised on one.
ARRAY_SPECS = BASE_SPECS + ["%A", "%a", "%b"]

# Before 25.11 slurmctld did not record the documented default StdOut for a job
# submitted without --output, so the reported pattern already carried the work
# directory instead of being "slurm-%j.out".
default_stdout_recorded = pytest.mark.skipif(
    atf.get_version("sbin/slurmctld") < (25, 11),
    reason="Ticket 23357: slurmctld only sets the default stdout string during "
    "_job_create() from 25.11 on",
)

SHAPES = [
    "nonarray",
    pytest.param("nonarray_default", marks=default_stdout_recorded),
    "array",
    pytest.param("array_default", marks=default_stdout_recorded),
    "wide",
    "escape",
]

# ARRAY_TASK is not the last task, so it is split off from the array and gets a
# job id of its own instead of keeping the array job id.
ARRAY_TASK = 12
ARRAY_TASKS = f"{ARRAY_TASK},15"


@pytest.fixture(scope="module", autouse=True)
def setup():
    atf.require_accounting(modify=True)
    atf.require_config_parameter_excludes("AccountingStoreFlags", "no_stdio")
    atf.require_nodes(2, [("CPUs", 2)])
    atf.require_slurm_running()

    # slurmdbd only records the user name of a job that has an association,
    # and %u expands from that recorded name.
    user = atf.get_user_name()
    atf.run_command(
        f"sacctmgr -i add account {ACCOUNT}",
        user=atf.properties["slurm-user"],
        fatal=True,
    )
    atf.run_command(
        f"sacctmgr -i add user {user} account={ACCOUNT}",
        user=atf.properties["slurm-user"],
        fatal=True,
    )

    yield

    atf.run_command(
        f"sacctmgr -i delete user {user} account={ACCOUNT}",
        user=atf.properties["slurm-user"],
        fatal=True,
    )
    atf.run_command(
        f"sacctmgr -i delete account {ACCOUNT}",
        user=atf.properties["slurm-user"],
        fatal=True,
    )


# --- expand-patterns helpers ------------------------------------------------
# NOTE: this block is duplicated byte-for-byte in:
#   testsuite/python/tests/test_101_14.py
#   testsuite/python/tests/test_114_7.py
# Keep the copies identical.

PATTERN_SEP = ","


def expand_patterns_expected(
    specifiers,
    job_id,
    job_name,
    user_name,
    first_node=None,
    sluid=None,
    array_job_id=None,
    array_task_id=None,
):
    """Return [(specifier, expected expansion)] for a job's batch step.

    Mirrors the FILENAME PATTERN table of sbatch(1) as it applies to the batch
    step of a batch job: %s is "batch", %J carries no ".stepid", and %n/%t are
    the first value of their range.  Width zero-pads, is clamped to 10, and is
    ignored for non-numeric data.
    """
    jid = int(job_id)
    ajid = int(array_job_id) if array_job_id is not None else jid
    atid = int(array_task_id) if array_task_id is not None else None
    table = {
        "%%": "%",
        "%J": str(jid),
        "%j": str(jid),
        "%4j": "%04d" % jid,
        "%20j": "%010d" % jid,
        "%N": first_node,
        "%4N": first_node,
        "%n": "0",
        "%s": "batch",
        "%t": "0",
        "%4t": "0000",
        "%u": user_name,
        "%x": job_name,
        "%r": "0",
        "%S": sluid,
        "%A": str(ajid),
        "%a": str(atid) if atid is not None else None,
        "%b": str(atid % 10) if atid is not None else None,
    }
    items = []
    for spec in specifiers:
        value = table[spec]
        assert value is not None, f"No expected expansion is available for {spec}"
        assert (
            PATTERN_SEP not in value
        ), f"{spec} expands to {value!r}, which contains the {PATTERN_SEP!r} separator"
        items.append((spec, value))
    return items


def split_expansion(items, value, prefix, suffix):
    """Return {specifier: actual expansion} parsed out of an expanded path."""
    assert value.startswith(
        prefix
    ), f"Expanded value {value!r} does not start with {prefix!r}"
    assert value.endswith(
        suffix
    ), f"Expanded value {value!r} does not end with {suffix!r}"
    parts = value[len(prefix) : len(value) - len(suffix)].split(PATTERN_SEP)
    assert len(parts) == len(
        items
    ), f"Expected {len(items)} fields in {value!r}, got {len(parts)}"
    return dict(zip([spec for spec, _ in items], parts))


# --- fixtures ---------------------------------------------------------------


@pytest.fixture(scope="module")
def paths():
    """Create the stdin file in the PWD and return the paths built from it."""
    user_name = atf.get_user_name()
    stdin_items = expand_patterns_expected(
        STDIN_SPECS, job_id=0, job_name=JOB_NAME, user_name=user_name
    )
    stdin_pattern = "i_" + PATTERN_SEP.join(s for s, _ in stdin_items) + ".in"
    stdin_expanded = "i_" + PATTERN_SEP.join(v for _, v in stdin_items) + ".in"

    atf.run_command(f"touch '{stdin_expanded}'", fatal=True)

    directory = os.getcwd()
    return {
        "dir": directory,
        "user_name": user_name,
        "stdin_pattern": f"{directory}/{stdin_pattern}",
        "stdin_items": stdin_items,
    }


def job_facts(query, want_sluid):
    """Return one job's (raw id, work dir, first node, sluid) from sacct.

    query addresses a single job: either a plain job id, or "<array>_<task>"
    for one task of a job array.  A bare array job id would make sacct emit one
    row per task, so array shapes must always use the underscore form.
    """
    fields = "JobIDRaw,WorkDir,NodeList"
    if want_sluid:
        fields += ",SLUID"
    rows = atf.run_command_output(
        f"sacct -X -n -P -j {query} -o {fields}", fatal=True
    ).splitlines()
    assert len(rows) == 1, f"Expected one sacct row for job {query}, got {rows}"
    parts = rows[0].split("|")
    first_node = atf.run_command_output(
        f"scontrol show hostnames {parts[2]}", fatal=True
    ).splitlines()[0]
    return int(parts[0]), parts[1], first_node, parts[3] if want_sluid else None


def _submit(paths, specs, extra_args):
    """Submit one job using the mega-pattern built from specs, return its id."""
    directory = paths["dir"]
    pattern = PATTERN_SEP.join(specs)
    args = (
        f"-J {JOB_NAME}"
        f" --output='{directory}/o_{pattern}.out'"
        f" --error='{directory}/e_{pattern}.err'"
        f" --input='{paths['stdin_pattern']}'"
        f" {extra_args} --wrap 'true'"
    )
    return atf.submit_job_sbatch(args, fatal=True)


@pytest.fixture(scope="module", params=SHAPES)
def job(request, paths):
    """Submit one job per shape and return everything needed to check it."""
    shape = request.param
    directory = paths["dir"]
    user_name = paths["user_name"]
    specs = ARRAY_SPECS if shape.startswith("array") else BASE_SPECS
    array_job_id = None
    array_task_id = None

    if shape == "nonarray":
        job_id = _submit(paths, specs, "")
    elif shape == "nonarray_default":
        job_id = atf.submit_job_sbatch(f"-J {JOB_NAME} --wrap 'true'", fatal=True)
    elif shape == "array":
        job_id = _submit(paths, specs, f"--array={ARRAY_TASKS}")
    elif shape == "array_default":
        job_id = atf.submit_job_sbatch(
            f"-J {JOB_NAME} --array={ARRAY_TASKS} --wrap 'true'", fatal=True
        )
    elif shape == "wide":
        job_id = _submit(paths, specs, "-N2 -n4")
    else:
        job_id = atf.submit_job_sbatch(
            f"-J {JOB_NAME} --output='{directory}/esc_\\%j-%x.out' --wrap 'true'",
            fatal=True,
        )

    query = job_id
    if shape.startswith("array"):
        array_job_id = job_id
        array_task_id = ARRAY_TASK
        query = f"{job_id}_{ARRAY_TASK}"

    atf.wait_for_job_accounted(query, field="State", value="COMPLETED", fatal=True)
    ref, work_dir, first_node, sluid = job_facts(query, "%S" in specs)
    if array_job_id is not None:
        assert (
            ref != array_job_id
        ), f"Task {query} kept the array job id, so %A and %j are indistinguishable"

    fields = {}
    if shape == "nonarray_default":
        fields["stdout"] = _exact("slurm-%j.out", f"{work_dir}/slurm-{ref}.out")
        fields["stderr"] = _exact("", "")
        fields["stdin"] = _exact("/dev/null", "/dev/null")
    elif shape == "array_default":
        fields["stdout"] = _exact(
            "slurm-%A_%a.out", f"{work_dir}/slurm-{array_job_id}_{ARRAY_TASK}.out"
        )
        fields["stderr"] = _exact("", "")
        fields["stdin"] = _exact("/dev/null", "/dev/null")
    elif shape == "escape":
        fields["stdout"] = _exact(
            f"{directory}/esc_\\%j-%x.out", f"{directory}/esc_%j-%x.out"
        )
        fields["stderr"] = _exact("", "")
        fields["stdin"] = _exact("/dev/null", "/dev/null")
    else:
        items = expand_patterns_expected(
            specs,
            job_id=ref,
            job_name=JOB_NAME,
            user_name=user_name,
            first_node=first_node,
            sluid=sluid,
            array_job_id=array_job_id,
            array_task_id=array_task_id,
        )
        pattern = PATTERN_SEP.join(specs)
        fields["stdout"] = _table(
            f"{directory}/o_{pattern}.out", items, f"{directory}/o_", ".out"
        )
        fields["stderr"] = _table(
            f"{directory}/e_{pattern}.err", items, f"{directory}/e_", ".err"
        )
        fields["stdin"] = _table(
            paths["stdin_pattern"],
            paths["stdin_items"],
            f"{directory}/i_",
            ".in",
        )

    return {"query": query, "ref": ref, "fields": fields}


def _exact(raw, expanded):
    return {"raw": raw, "expanded": expanded, "items": None}


def _table(raw, items, prefix, suffix):
    return {"raw": raw, "items": items, "prefix": prefix, "suffix": suffix}


# --- readers ----------------------------------------------------------------


def _read_text(query, ref):
    """Return (raw, expanded) field dicts from sacct's text output."""
    fields = "StdOut,StdErr,StdIn"
    out = {}
    for key, flag in (("raw", ""), ("expanded", "--expand-patterns")):
        line = atf.run_command_output(
            f"sacct -X -n -P -j {query} -o {fields} {flag}", fatal=True
        ).splitlines()
        assert len(line) == 1, f"Expected one sacct row for job {query}, got {line}"
        parts = line[0].split("|")
        assert len(parts) == 3, f"Expected 3 sacct fields for job {query}, got {parts}"
        out[key] = dict(zip(["stdout", "stderr", "stdin"], parts))
    return out["raw"], out["expanded"]


def _read_json(query, ref):
    """Return (raw, expanded) field dicts from sacct --json output."""
    jobs = json.loads(atf.run_command_output(f"sacct -j {query} --json", fatal=True))[
        "jobs"
    ]
    jobs = [j for j in jobs if j["job_id"] == ref]
    assert len(jobs) == 1, f"Expected one job {ref} in sacct --json, got {len(jobs)}"
    rec = jobs[0]
    raw = {k: rec[k] for k in ("stdout", "stderr", "stdin")}
    expanded = {k: rec[f"{k}_expanded"] for k in ("stdout", "stderr", "stdin")}
    return raw, expanded


# --- tests ------------------------------------------------------------------


@pytest.mark.parametrize("reader", [_read_text, _read_json], ids=["text", "json"])
def test_sacct_stdio_fields(job, reader):
    """Verify sacct reports the raw and the expanded stdio filename patterns."""
    raw, expanded = reader(job["query"], job["ref"])

    for name, expect in job["fields"].items():
        assert (
            raw[name] == expect["raw"]
        ), f"{name} should be reported unexpanded as {expect['raw']!r}"

        if expect["items"] is None:
            assert (
                expanded[name] == expect["expanded"]
            ), f"{name} should expand to {expect['expanded']!r}"
        else:
            actual = split_expansion(
                expect["items"], expanded[name], expect["prefix"], expect["suffix"]
            )
            assert actual == dict(
                expect["items"]
            ), f"{name} did not expand every specifier as documented"
