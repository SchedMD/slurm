############################################################################
# Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
############################################################################
"""Verify srun --bcast with hetjobs."""

import os
import re

import pytest

import atf

# srun.1 documents this name for a broadcast file whose destination is a
# directory rather than a file: slurm_bcast_<job_id>.<step_id>_<nodename>.
BCAST_NAME_PATTERN = r"slurm_bcast_\d+\.\d+_\S+"

# Hetjobs only start on a backfill cycle, so allow more than the default
# command timeout. A timeout otherwise reports exit code 110 and no stderr.
SRUN_TIMEOUT = 120

# What detects the regression these tests cover is the srun exit code, not any
# field parsed out of a broadcast name. slurmd names the file it writes from
# the sbcast credential the controller issued, while slurmstepd rebuilds the
# same name from the step it is about to run, so a credential carrying the
# wrong step id sends the file somewhere the task never looks, and a
# credential carrying the wrong het component sends it to the other
# component's node. Either way the exec fails with ENOENT.
#
# The controller side of that was broken in 25.11 and is fixed in 26.11 and in
# 26.05.5, so every test below that depends on the credential is expected to
# fail against a controller older than that. tox.ini sets xfail_strict, so a
# controller in this range that does have the fix is a hard failure rather than
# an XPASS signal: the range has to shrink in step with every backport.
xfail_bcast_hetjob = pytest.mark.xfail(
    (25, 11) <= atf.get_version("sbin/slurmctld") < (26, 5, 5),
    reason="Issue 51136: srun --bcast in a hetjob is broken before 26.05.5",
)

# --bcast-exclude has a separate gate. Its defect is in srun rather than the
# controller, so an upgraded controller in front of an old srun still shows it,
# and it predates 25.11 rather than being a regression in it, so there is no
# lower bound. srun dropped a non-final component's list, leaving that
# component to take its successor's value.
xfail_bcast_exclude_hetjob = pytest.mark.xfail(
    atf.get_version("bin/srun") < (26, 5, 5),
    reason="Issue 51136: srun --bcast-exclude is lost for every hetjob"
    " component but the last before 26.05.5",
)


@pytest.fixture(scope="module", autouse=True)
def setup():
    # Two nodes because each component asks for -N1. Nothing here forces the
    # components onto different nodes: require_nodes() treats CPUs as a
    # minimum rather than an exact count, and heterogeneous_jobs.shtml says
    # components may end up sharing a node. What the tests rely on is that
    # backfill normally has to place each component separately to start the
    # allocation at all.
    atf.require_nodes(2, [("CPUs", 1)])
    # Hetjobs are only ever started by the backfill scheduler.
    atf.require_config_parameter_includes("SchedulerParameters", ("bf_interval", 1))
    # With no dest_path srun.1 prefers BcastParameters DestDir over --chdir.
    # The tests below assert the --chdir form, so require the whole parameter
    # unset rather than report a site setting as if --bcast were broken.
    # SbcastParameters is still accepted as a deprecated alias for the same
    # value (read_config.c only logs a rename error), so it has to be unset
    # too or a site using the old spelling slips a DestDir past this.
    atf.require_config_parameter("BcastParameters", None)
    atf.require_config_parameter("SbcastParameters", None)
    # --bcast-exclude overrides slurm.conf BcastExclude, so the exclude test
    # can only tell an honoured override from an ignored one if BcastExclude is
    # known. Unset is the documented default, which is non-empty.
    atf.require_config_parameter("BcastExclude", None)
    atf.require_slurm_running()


@pytest.fixture(scope="module")
def component_scripts():
    """Return one distinct executable per hetjob component.

    Each script prints its own marker, its own argv[0] and the node it ran
    on. srun rewrites argv[0] to the broadcast destination, so the output
    says which file the component ran, where that file landed, and which
    node ran it.
    """

    scripts = []
    for component in range(2):
        name = f"comp{component}.sh"
        atf.make_bash_script(name, f'echo "COMP{component} $0 $SLURMD_NODENAME"')
        scripts.append(f"{os.getcwd()}/{name}")
    return scripts


def executed_files(output):
    """Map each component marker to its (argv[0], nodename)."""

    executed = {}
    for line in output.splitlines():
        match = re.fullmatch(r"(COMP\d) (\S+) (\S+)", line.strip())
        if match:
            executed[match.group(1)] = (match.group(2), match.group(3))
    return executed


def assert_documented_bcast_name(marker, argv0, directory):
    """Assert argv[0] is the name srun.1 documents, under directory.

    srun.1 says a broadcast file whose destination is a directory is named
    slurm_bcast_<job_id>.<step_id>_<nodename>. The directory half carries the
    weight here: it comes from srun's --chdir, so matching it is what proves
    the documented no-dest_path resolution was used.

    The fields inside the name are deliberately not asserted. slurmstepd
    rebuilds them from the step it is running, so they look correct whatever
    the sbcast credential said; the credential only shows up in whether the
    exec succeeded.
    """

    assert re.fullmatch(f"{re.escape(directory)}/{BCAST_NAME_PATTERN}", argv0), (
        f"{marker} should have run the documented broadcast name under"
        f" {directory}, but ran {argv0}"
    )


@xfail_bcast_hetjob
def test_bcast_hetjob_default_destination(component_scripts):
    """Verify --bcast with no path runs each component's own broadcast file.

    With no path and no BcastParameters DestDir, srun.1 puts the file in
    --chdir under the documented name. slurmd builds that name from the
    sbcast credential while slurmstepd rebuilds it from the step it runs,
    so the two have to agree or the task dies with ENOENT.

    The exit code assertion is therefore the regression detector, not the
    name checks that follow it. Do not weaken it.

    Note: assumes the same file system is visible to us and to the slurmd.
    """

    result = atf.run_command(
        f"srun -t1 --bcast -N1 -n1 {component_scripts[0]}"
        f" : --bcast -N1 -n1 {component_scripts[1]}",
        timeout=SRUN_TIMEOUT,
    )

    assert result["exit_code"] == 0, (
        f"srun --bcast in a hetjob should succeed, but got rc="
        f"{result['exit_code']}: {result['stderr']}"
    )

    executed = executed_files(result["stdout"])
    assert set(executed) == {"COMP0", "COMP1"}, (
        "Each hetjob component should have run its own broadcast file, but got"
        f" {result['stdout']!r}"
    )

    cwd = os.getcwd()
    for marker, (argv0, _) in executed.items():
        assert_documented_bcast_name(marker, argv0, cwd)


@pytest.fixture(scope="module")
def shared_script():
    """Return one executable for both hetjob components to share."""

    atf.make_bash_script("shared.sh", 'echo "SHARED $0 $SLURMD_NODENAME"')
    return f"{os.getcwd()}/shared.sh"


@xfail_bcast_hetjob
def test_bcast_hetjob_shared_application(shared_script):
    """Verify --bcast with one application shared by both components.

    heterogeneous_jobs.shtml says a component that lacks an application
    specification uses the next one provided, so this single command line
    broadcasts one source file on behalf of two components. That is the
    hardest shape to get right and the easiest to get wrong quietly: there is
    only one source path to reason about, so a credential naming the wrong
    component has nothing else to disagree with.

    As above, the exit code is the detector.

    Note: assumes the same file system is visible to us and to the slurmd.
    """

    result = atf.run_command(
        f"srun -t1 -n1 --bcast : -n1 --bcast {shared_script}",
        timeout=SRUN_TIMEOUT,
    )

    assert result["exit_code"] == 0, (
        "srun --bcast with an application shared by both hetjob components"
        f" should succeed, but got rc={result['exit_code']}: {result['stderr']}"
    )

    ran = [
        match.groups()
        for match in (
            re.fullmatch(r"SHARED (\S+) (\S+)", line.strip())
            for line in result["stdout"].splitlines()
        )
        if match
    ]
    assert len(ran) == 2, (
        "Both hetjob components should have run the shared application, but"
        f" got {result['stdout']!r}"
    )

    cwd = os.getcwd()
    for argv0, node in ran:
        assert_documented_bcast_name(f"The component on {node}", argv0, cwd)

    assert len({argv0 for argv0, _ in ran}) == 2, (
        "Each component should have broadcast the shared application to its"
        f" own destination, but both ran {ran[0][0]}"
    )


def test_bcast_hetjob_destination_file(component_scripts):
    """Verify --bcast=<absolute file> is used verbatim for each component.

    srun.1 says a path that does not end in '/' is the destination file name.
    No name is derived from the job or step in that case, so this covers the
    other half of the documented path resolution from the pattern tests.

    Note: assumes the same file system is visible to us and to the slurmd.
    """

    cwd = os.getcwd()
    destinations = [f"{cwd}/bcast_comp0", f"{cwd}/bcast_comp1"]

    result = atf.run_command(
        f"srun -t1 --bcast={destinations[0]} -N1 -n1 {component_scripts[0]}"
        f" : --bcast={destinations[1]} -N1 -n1 {component_scripts[1]}",
        timeout=SRUN_TIMEOUT,
    )

    assert result["exit_code"] == 0, (
        f"srun --bcast=<file> in a hetjob should succeed, but got rc="
        f"{result['exit_code']}: {result['stderr']}"
    )

    executed = executed_files(result["stdout"])
    assert {marker: argv0 for marker, (argv0, _) in executed.items()} == {
        "COMP0": destinations[0],
        "COMP1": destinations[1],
    }, f"Each component should have run its own --bcast file, but got {executed}"


@xfail_bcast_hetjob
def test_bcast_hetjob_destination_dir(component_scripts):
    """Verify --bcast=<dir>/ names the file per the documented pattern.

    srun.1 says a path ending in '/' is a target directory, and that the
    destination file in it is named slurm_bcast_<job_id>.<step_id>_<nodename>.

    Note: assumes the same file system is visible to us and to the slurmd.
    """

    cwd = os.getcwd()
    directories = [f"{cwd}/bcast_dir0", f"{cwd}/bcast_dir1"]
    for directory in directories:
        atf.run_command(f"mkdir -p {directory}", fatal=True)

    result = atf.run_command(
        f"srun -t1 --bcast={directories[0]}/ -N1 -n1 {component_scripts[0]}"
        f" : --bcast={directories[1]}/ -N1 -n1 {component_scripts[1]}",
        timeout=SRUN_TIMEOUT,
    )

    assert result["exit_code"] == 0, (
        f"srun --bcast=<dir>/ in a hetjob should succeed, but got rc="
        f"{result['exit_code']}: {result['stderr']}"
    )

    executed = executed_files(result["stdout"])
    assert set(executed) == {"COMP0", "COMP1"}, (
        "Each hetjob component should have run its own broadcast file, but got"
        f" {result['stdout']!r}"
    )
    for component, marker in enumerate(["COMP0", "COMP1"]):
        argv0, _ = executed[marker]
        assert_documented_bcast_name(marker, argv0, directories[component])


@xfail_bcast_hetjob
def test_bcast_hetjob_chdir_per_component(component_scripts):
    """Verify --bcast with no path resolves each component's own --chdir.

    srun.1 falls back to --chdir when no dest_path is given and no
    BcastParameters DestDir is configured. Each component parses its own
    --chdir, so each broadcast file has to land under that component's
    directory rather than under one the components share.

    The scripts are named by absolute path so --chdir moves the destination
    without also moving the source.

    Note: assumes the same file system is visible to us and to the slurmd.
    """

    cwd = os.getcwd()
    directories = [f"{cwd}/chdir_comp0", f"{cwd}/chdir_comp1"]
    for directory in directories:
        atf.run_command(f"mkdir -p {directory}", fatal=True)

    result = atf.run_command(
        f"srun -t1 --chdir={directories[0]} --bcast -N1 -n1 {component_scripts[0]}"
        f" : --chdir={directories[1]} --bcast -N1 -n1 {component_scripts[1]}",
        timeout=SRUN_TIMEOUT,
    )

    assert result["exit_code"] == 0, (
        f"srun --bcast with a per-component --chdir should succeed, but got rc="
        f"{result['exit_code']}: {result['stderr']}"
    )

    executed = executed_files(result["stdout"])
    assert set(executed) == {"COMP0", "COMP1"}, (
        "Each hetjob component should have run its own broadcast file, but got"
        f" {result['stdout']!r}"
    )
    for component, marker in enumerate(["COMP0", "COMP1"]):
        argv0, _ = executed[marker]
        assert_documented_bcast_name(marker, argv0, directories[component])


@pytest.fixture(scope="module")
def library_probe():
    """Return a dynamically linked executable, its library dirs and a probe.

    --send-libs only has something to send for a dynamically linked ELF, so a
    bash script cannot be the broadcast executable. Broadcasting bash itself
    and handing it a probe script makes the task its own oracle: it reports
    the LD_LIBRARY_PATH it was launched with and the contents of the cache
    directory srun.1 promises to add to it, so nothing here has to know how
    slurmd spells that directory.
    """

    atf.require_tool("bash")
    # srun runs LDD_PATH unconditionally rather than searching PATH
    # (src/common/file_bcast.c), so gate on the path srun uses.
    atf.require_tool("/usr/bin/ldd")

    binary = atf.run_command_output("command -v bash", quiet=True, fatal=True).strip()
    # Not fatal: ldd exits non-zero on a non-dynamic binary, which the skip
    # below is there to handle. srun treats the same failure as non-fatal.
    output = atf.run_command_output(f"/usr/bin/ldd {binary}", quiet=True)

    # Take every absolute path ldd prints: the first '/' in the line up to the
    # next space. Matching on "=>" alone would miss the dynamic linker line,
    # which has no "=>" in it, and leave that directory unexcluded.
    directories = set()
    for line in output.splitlines():
        start = line.find("/")
        if start < 0:
            continue
        directories.add(os.path.dirname(line[start:].split(" ")[0]))
    if not directories:
        # A bare skip rather than an atf.require_*() or skipif: whether the
        # shell is statically linked is only knowable from ldd's output, and
        # with nothing to send there is no exclusion to observe.
        pytest.skip(f"{binary} is not dynamically linked, so nothing is sent")

    # $1 is the component's marker, $2 the directory it broadcast into.
    # srun.1 promises the libraries are placed in a directory alongside the
    # executable and that LD_LIBRARY_PATH is updated to include that cache
    # directory, so report every LD_LIBRARY_PATH entry that is in the
    # component's own directory along with what is in it. The DONE line makes
    # "the task ran and was given no cache directory" distinguishable from
    # "the task never ran".
    atf.make_bash_script(
        "libs_probe.sh",
        """
IFS=:
for entry in $LD_LIBRARY_PATH; do
    case "$entry" in
        "$2"/*) echo "$1 CACHE $entry $(ls -A "$entry" | tr '\\n' ',')" ;;
    esac
done
echo "$1 DONE"
""",
    )

    return binary, sorted(directories), f"{os.getcwd()}/libs_probe.sh"


def probe_caches(output):
    """Map each marker to the (directory, contents) pairs the probe reported.

    A marker present with an empty list means the task ran and was given no
    library cache directory at all.
    """

    caches = {}
    for line in output.splitlines():
        line = line.strip()
        done = re.fullmatch(r"(COMP\d) DONE", line)
        if done:
            caches.setdefault(done.group(1), [])
            continue
        cache = re.fullmatch(r"(COMP\d) CACHE (\S+) ?(.*)", line)
        if cache:
            contents = [name for name in cache.group(3).split(",") if name]
            caches.setdefault(cache.group(1), []).append((cache.group(2), contents))
    return caches


@xfail_bcast_exclude_hetjob
def test_bcast_exclude_applies_per_hetjob_component(library_probe):
    """Verify each hetjob component honours its own --bcast-exclude list.

    srun.1 says --send-libs places the executable's shared objects in a
    directory alongside it and updates LD_LIBRARY_PATH to include that cache
    directory, and that --bcast-exclude (defaulting to the slurm.conf
    BcastExclude value) keeps listed paths out of it. Each component parses
    its own command line, so each must keep its own list rather than share
    one.

    Both components ask for --send-libs, so both must be given a cache
    directory on LD_LIBRARY_PATH; the only difference between them should be
    what is in it.

    The excluding component is deliberately the first one. srun copies each
    component's options while parsing the next, so only a non-final component
    can lose its own value.

    The dangling read left by srun's per-component copy defect does not
    return the original bytes here. Component 0 picks up component 1's NONE
    and is broadcast the shared objects it excluded, on every unpatched srun
    the mixed-version matrix runs. This test is the regression detector for
    that, not just a statement of the functional contract. Do not weaken it.

    Note: assumes the same file system is visible to us and to the slurmd.
    """

    binary, library_dirs, probe = library_probe
    cwd = os.getcwd()
    directories = [f"{cwd}/libs_comp0", f"{cwd}/libs_comp1"]
    for directory in directories:
        atf.run_command(f"mkdir -p {directory}", fatal=True)
    excluded = ",".join(library_dirs)

    result = atf.run_command(
        f"srun -t1 --send-libs --bcast={directories[0]}/probe"
        f" --bcast-exclude={excluded} -N1 -n1"
        f" {binary} {probe} COMP0 {directories[0]}"
        f" : --send-libs --bcast={directories[1]}/probe"
        f" --bcast-exclude=NONE -N1 -n1"
        f" {binary} {probe} COMP1 {directories[1]}",
        timeout=SRUN_TIMEOUT,
    )

    assert result["exit_code"] == 0, (
        f"srun --bcast --send-libs in a hetjob should succeed, but got rc="
        f"{result['exit_code']}: {result['stderr']}"
    )

    caches = probe_caches(result["stdout"])
    assert set(caches) == {"COMP0", "COMP1"}, (
        "Each hetjob component should have run the probe, but got"
        f" {result['stdout']!r}"
    )

    for marker, directory in zip(["COMP0", "COMP1"], directories):
        assert len(caches[marker]) == 1, (
            f"--send-libs should have given {marker} exactly one library cache"
            f" directory under {directory} on its LD_LIBRARY_PATH, but it"
            f" reported {caches[marker]}"
        )

    cache_dir, included = caches["COMP1"][0]
    assert included, (
        "--bcast-exclude=NONE should have broadcast the shared objects of"
        f" component 1, but {cache_dir} is empty"
    )

    cache_dir, withheld = caches["COMP0"][0]
    assert not withheld, (
        f"Component 0 excluded {excluded}, which is every directory ldd"
        f" reported for {binary}, so nothing should have been broadcast, but"
        f" {cache_dir} holds {sorted(withheld)}"
    )


@xfail_bcast_hetjob
def test_bcast_het_group_in_existing_allocation():
    """Verify --het-group picks the component --bcast transfers to.

    heterogeneous_jobs.shtml describes --bcast as transferring files to the
    nodes of the application to be launched, as selected by --het-group, and
    srun.1 says --het-group applies only inside a salloc allocation or an
    sbatch script. That is a different path than srun creating the hetjob.

    This uses the default destination on purpose. An explicit path carries no
    node identity, so a transfer credentialed for the wrong component still
    execs from the shared file system and the test cannot tell. The
    documented name embeds the node, so the wrong component gives ENOENT and
    the srun in the batch script fails.

    Note: assumes the same file system is visible to us and to the slurmd.
    """

    cwd = os.getcwd()
    output = "het_group_bcast.out"
    error = "het_group_bcast.err"
    batch_script = "het_group_bcast.sh"

    for component in range(2):
        atf.make_bash_script(
            f"group{component}.sh", f'echo "COMP{component} $0 $SLURMD_NODENAME"'
        )

    atf.make_bash_script(
        batch_script,
        f"""
#SBATCH -N1 -t1
#SBATCH hetjob
#SBATCH -N1 -t1

set -e

srun --het-group=0 --bcast -n1 {cwd}/group0.sh
srun --het-group=1 --bcast -n1 {cwd}/group1.sh
""",
    )

    job_id = atf.submit_job_sbatch(
        f"-t1 -o {output} -e {error} {batch_script}", fatal=True
    )
    # Same backfill-cycle wait as SRUN_TIMEOUT above: the 45s default polling
    # timeout is not enough for a hetjob to be scheduled and complete.
    atf.wait_for_job_state(job_id, "DONE", timeout=SRUN_TIMEOUT, fatal=True)

    # Check the job state before polling the output. A failed srun leaves the
    # output empty for good, so waiting on it first only burns the timeout and
    # hides the real error, which srun wrote to the error file.
    state = atf.get_job_parameter(job_id, "JobState", fatal=True)
    assert state == "COMPLETED", (
        f"Expected COMPLETED but got {state!r}:"
        f" {atf.run_command_output(f'cat {error}', quiet=True)}"
    )

    # Waits for the file and re-polls until the second component's line lands.
    # The wait includes the start of the name, not just the marker, so a read
    # that catches the line half written cannot be mistaken for a component
    # that never ran.
    atf.assert_file_contents(output, f"COMP1 {cwd}/slurm_bcast_", contains=True)
    contents = atf.run_command_output(f"cat {output}", fatal=True)

    executed = executed_files(contents)
    assert set(executed) == {"COMP0", "COMP1"}, (
        "Each --het-group should have run its own broadcast file, but got"
        f" {contents!r}"
    )

    for marker, (argv0, _) in executed.items():
        assert_documented_bcast_name(marker, argv0, cwd)

    # Not a regression check by itself, and nothing above forces the split:
    # hetjob components are observed to be placed on separate nodes. That
    # separation is what makes the ENOENT above possible, so if it ever stops
    # holding, the test above has quietly stopped testing anything.
    nodes = {node for _, node in executed.values()}
    assert len(nodes) == 2, (
        "The two --het-group components should have run on different nodes,"
        f" each with a broadcast file named for its own node, but got {nodes}"
    )


@xfail_bcast_hetjob
def test_bcast_het_group_spanning_components():
    """Verify --het-group=0,1 broadcasts to every component it names.

    srun.1 gives --het-group a set expression, so one step can span several
    components. That is the only shape where a single sbcast credential has to
    resolve to more than one component's nodes, which is what the het step
    component field selects; with a single component there is only one answer
    it could give.

    The default destination is used for the same reason as the test above: the
    documented name embeds the node, so a component the file never reached
    gives ENOENT rather than silently execing from the shared file system.

    Note: assumes the same file system is visible to us and to the slurmd.
    """

    cwd = os.getcwd()
    output = "het_group_span.out"
    error = "het_group_span.err"
    batch_script = "het_group_span.sh"

    atf.make_bash_script("span.sh", 'echo "SPAN $0 $SLURMD_NODENAME"')
    atf.make_bash_script(
        batch_script,
        f"""
#SBATCH -N1 -t1
#SBATCH hetjob
#SBATCH -N1 -t1

srun --het-group=0,1 --bcast {cwd}/span.sh
""",
    )

    job_id = atf.submit_job_sbatch(
        f"-t1 -o {output} -e {error} {batch_script}", fatal=True
    )
    atf.wait_for_job_state(job_id, "DONE", timeout=SRUN_TIMEOUT, fatal=True)

    state = atf.get_job_parameter(job_id, "JobState", fatal=True)
    assert state == "COMPLETED", (
        f"Expected COMPLETED but got {state!r}:"
        f" {atf.run_command_output(f'cat {error}', quiet=True)}"
    )

    contents = ""
    for _ in atf.timer(fatal=True):
        contents = atf.run_command_output(f"cat {output}", quiet=True, fatal=True)
        if contents.count(f"SPAN {cwd}/slurm_bcast_") == 2:
            break

    ran = [
        match.groups()
        for match in (
            re.fullmatch(r"SPAN (\S+) (\S+)", line.strip())
            for line in contents.splitlines()
        )
        if match
    ]
    assert len(ran) == 2, (
        "--het-group=0,1 should have run a task in each component it names,"
        f" but got {contents!r}"
    )

    for argv0, node in ran:
        assert_documented_bcast_name(f"The component on {node}", argv0, cwd)

    assert len({argv0 for argv0, _ in ran}) == 2, (
        "Each component named by --het-group=0,1 should have been broadcast to"
        f" separately, but both ran {ran[0][0]}"
    )

    # heterogeneous_jobs.shtml says all components of a job step share the step
    # ID, while each component carries its own job id. This is the documented
    # cross-component invariant, not a credential detector.
    ids = [
        re.fullmatch(r"slurm_bcast_(\d+)\.(\d+)_\S+", os.path.basename(argv0)).groups()
        for argv0, _ in ran
    ]
    assert ids[0][1] == ids[1][1], (
        "Components of one --het-group=0,1 step should share a step id, but got"
        f" {ids[0][1]} and {ids[1][1]}"
    )
    assert (
        ids[0][0] != ids[1][0]
    ), f"Each component should carry its own job id, but both used {ids[0][0]}"
