############################################################################
# Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
############################################################################
"""sbcast must transfer files into the job's namespace, not onto the node.

slurmd serves REQUEST_FILE_BCAST from _open_as_other(), which joins the job
namespace passing step_create=false. A 24.11 regression (Ticket 21634) turned
that join into a no-op for every slurmd caller, so sbcast created the file in
the node's real filesystem instead of the job's private mount.

These tests pre-create a decoy at the destination on the host, so that
regression fails immediately and names itself instead of surfacing as a job
that never finds its file:

  - without --force slurmd opens the destination O_EXCL, so a slurmd that
    skipped the join trips over the decoy and reports the ticket's own error,
    "File exists";
  - with --force it opens O_TRUNC instead, so a slurmd that skipped the join
    fails silently, and the only evidence is the decoy being overwritten.

Both are checked before the test waits on anything.
"""

import pytest

import atf

# Bind-mount target for the job's private directory. The plugin mounts over an
# absolute path on the node, so this cannot be a relative path in the test's
# cwd. It must also not be plain /tmp: pytest puts every test's tmp_path (and
# therefore each job's working directory) under /tmp, and privatizing that
# would hide the job's own cwd.
ns_dir = "/tmp/test_106_2_private"

bcast_name = "bcast_file"
bcast_path = f"{ns_dir}/{bcast_name}"

host_content = "written on the host, must never be replaced"
bcast_content = "written by sbcast, must only exist inside the job"


@pytest.fixture(scope="module", autouse=True)
def setup():
    # JobContainerType was renamed to NamespaceType in 25.11, but the old
    # spelling is still accepted on every release after that (slurmctld only
    # warns), and it is the sole spelling 25.05 understands. Writing it
    # unconditionally therefore works everywhere, and avoids gating on a
    # version: slurm.conf is parsed by the clients and slurmd too, and those
    # upgrade last, so the controller's version would be the wrong thing to
    # ask about. NamespaceType takes precedence when both are set, so clear any
    # inherited one.
    atf.require_config_parameter("NamespaceType", None)
    atf.require_config_parameter("JobContainerType", "job_container/tmpfs")

    atf.require_config_parameter_includes("PrologFlags", "Contain")
    atf.require_config_parameter("AutoBasePath", "true", source="job_container")
    atf.require_config_parameter(
        "BasePath", "/tmp/%h_%n_base_path", source="job_container"
    )
    atf.require_config_parameter("Dirs", ns_dir, source="job_container")
    # sbcast only takes the slurmd join path under test when this is off.
    atf.require_config_parameter("EntireStepInNS", "false", source="job_container")

    atf.require_slurm_running()

    if atf.get_config_parameter("SlurmdUser") != "root(0)":
        pytest.skip("Issue 50971: sbcast requires SlurmdUser to be root")

    # The plugin bind-mounts over this path and mount(2) needs its target to
    # already exist, so it has to be created before any job creates its
    # namespace.
    # Note: assume the same file system is visible for us and the slurmd.
    atf.run_command(f"mkdir -p {ns_dir}", fatal=True)

    yield

    # Nothing else removes this: the module teardown restores the config dir
    # but leaves behind the directories the plugin was pointed at.
    atf.run_command(f"rm -rf {ns_dir}", fatal=True, quiet=True)


@pytest.fixture(scope="function")
def host_decoy():
    """Place a file on the host at the destination sbcast will be given.

    Inside the job that path is a freshly created, empty private mount, so a
    correctly joined sbcast never sees this file. One that stayed in the host
    namespace does, which is what makes both tests fail fast.
    """

    atf.run_command(f"rm -rf {ns_dir}/*", fatal=True)
    atf.run_command(f"echo '{host_content}' > {bcast_path}", fatal=True)

    yield

    atf.run_command(f"rm -rf {ns_dir}/*", fatal=True, quiet=True)


@pytest.fixture(scope="function")
def running_job():
    """A job whose namespace is known to already exist.

    The job touches a barrier file from inside its own step, so waiting for
    that file proves the prolog has finished creating the namespace. Waiting
    for the job state would not.
    """

    barrier = "job_ready"
    atf.make_bash_script("job.sh", f"touch {barrier}\nsleep infinity\n")

    job_id = atf.submit_job_sbatch("-o /dev/null job.sh", fatal=True)
    # The barrier carries no content, only existence, so wait_for_file is the
    # right call here rather than assert_file_contents.
    atf.wait_for_file(barrier, fatal=True)

    yield job_id


@pytest.fixture(scope="module")
def elf_binary():
    """A small dynamically linked binary to broadcast with --send-libs."""

    # sbcast runs LDD_PATH unconditionally rather than searching PATH
    # (src/common/file_bcast.c), so gate on the path sbcast uses.
    atf.require_tool("/usr/bin/ldd")

    # Resolve a real file, not a shell builtin: "command -v true" would answer
    # "true" rather than a path, and ldd would then have nothing to read.
    path = atf.run_command_output("command -v ls", quiet=True, fatal=True).strip()
    if not path.startswith("/"):
        pytest.skip(f"Could not resolve a binary to broadcast, got '{path}'")

    # Whenever ldd reports no shared objects, sbcast quietly drops --send-libs
    # and still exits 0, so the library directory is never created on either
    # side and this test would be measuring nothing. Not fatal: ldd exits
    # non-zero on a static binary, which is what the skip is here to handle.
    # Match on the first '/' in each line rather than on "=>", which the
    # dynamic linker's own line does not have.
    ldd_output = atf.run_command_output(f"/usr/bin/ldd {path}", quiet=True)
    if not any("/" in line for line in ldd_output.splitlines()):
        pytest.skip(f"{path} is not dynamically linked, so no libraries are sent")

    return path


@pytest.mark.parametrize("force", [False, True], ids=["exclusive", "force"])
def test_sbcast_writes_into_the_job_namespace(force, host_decoy, running_job):
    """sbcast must create the file in the job's namespace, not on the node."""

    atf.run_command(f"echo '{bcast_content}' > {bcast_name}", fatal=True)

    # Deliberately not fatal: this sbcast is the test's own subject, and its
    # exit code is the first thing asserted below.
    force_opt = "--force " if force else ""
    result = atf.run_command(
        f"sbcast {force_opt}--jobid={running_job} {bcast_name} {bcast_path}"
    )

    # Without --force the destination is opened O_EXCL, so an sbcast that never
    # entered the namespace collides with the decoy instead of writing it.
    assert result["exit_code"] == 0, (
        f"sbcast must create {bcast_path} inside the namespace of job"
        f" {running_job}, where nothing exists yet, but it failed. A"
        f" 'File exists' error means slurmd stayed in the host namespace and"
        f" hit the host's copy of the file instead."
        f"\nstderr:\n{result['stderr']}"
    )

    # With --force the destination is opened O_TRUNC instead, so the same
    # failure is silent and only the state of the host's copy reveals it.
    # sbcast has already returned, so there is nothing left to wait for: read
    # once rather than with assert_file_contents, whose polling would turn the
    # failure this test exists to catch into a timeout.
    host_after = atf.run_command_output(f"cat {bcast_path}", fatal=True).strip()
    assert host_after == host_content, (
        f"The host's {bcast_path} must still hold '{host_content}' because"
        f" sbcast should have written inside the namespace of job"
        f" {running_job}, but it now holds '{host_after}'"
    )

    # Having established sbcast did not write to the host, confirm it wrote
    # somewhere the job can actually read.
    in_job = atf.run_command_output(
        f"srun --overlap --jobid={running_job} cat {bcast_path}", fatal=True
    ).strip()
    assert in_job == bcast_content, (
        f"Job {running_job} must read '{bcast_content}' from {bcast_path}"
        f" inside its namespace, but it read '{in_job}'"
    )


def test_sbcast_send_libs_creates_its_directory_in_the_job_namespace(
    host_decoy, running_job, elf_binary
):
    """The <dest>_libs directory sbcast creates must also land in the namespace."""

    libs_path = f"{bcast_path}_libs"

    # The libraries of a typical binary all sit under the default BcastExclude
    # (/lib,/usr/lib,/lib64,/usr/lib64), which would leave the directory empty
    # and the assertion below unable to tell an empty transfer from a missing
    # one, so ask for everything.
    # Deliberately not fatal: this sbcast is the test's own subject.
    result = atf.run_command(
        f"sbcast --send-libs --exclude=none --jobid={running_job}"
        f" {elf_binary} {bcast_path}"
    )
    assert result["exit_code"] == 0, (
        f"sbcast --send-libs must create {bcast_path} and {libs_path} inside"
        f" the namespace of job {running_job}, but it failed."
        f"\nstderr:\n{result['stderr']}"
    )

    # slurmd creates the directory and the file through the same namespace
    # join, so this cannot fail on its own -- if the join were skipped, the
    # file open above would already have collided with the decoy. Keep it as a
    # cheap statement of what "in the namespace" means for the directory too.
    # Read it the same way as every other host-side check here, through
    # run_command as the test user, so a permission error cannot read as
    # absence.
    host_libs = atf.run_command(f"ls -d {libs_path}", quiet=True)
    assert host_libs["exit_code"] != 0, (
        f"sbcast --send-libs must create {libs_path} inside the namespace of"
        f" job {running_job}, so it must not appear on the host"
    )

    in_job = atf.run_command_output(
        f"srun --overlap --jobid={running_job} ls {libs_path}", fatal=True
    ).strip()
    assert in_job, (
        f"Job {running_job} must see the libraries sbcast --send-libs placed"
        f" in {libs_path}, but that directory is empty inside the namespace"
    )
