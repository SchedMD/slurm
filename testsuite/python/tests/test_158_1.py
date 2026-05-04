############################################################################
# Copyright (C) SchedMD LLC.
############################################################################
import atf
import pytest

import csv
import io
import json
import re


def get_cluster_names(remote_license):
    """Find all cluster names"""

    cluster_names = [
        k
        for item in remote_license["actions"]
        if "clusters" in item
        for k in item["clusters"]
    ]

    return sorted(cluster_names)


# Setup
def setup(remote_license):
    cluster_names = get_cluster_names(remote_license)

    for cluster in cluster_names:
        atf.set_config_parameter("ClusterName", cluster)
        # TODO: Set cluster name for all clusters when multi-cluster ability is
        # available.
        break

    if "preserve_case" in remote_license and remote_license["preserve_case"]:
        atf.require_config_parameter(
            "Parameters", "PreserveCaseResource", source="slurmdbd"
        )
    atf.require_accounting(modify=True)
    atf.require_slurm_running()

    for cluster in cluster_names:
        print(f"cluster: {cluster}")
        atf.run_command(
            f"sacctmgr -i add cluster {cluster}",
            user=atf.properties["slurm-user"],
        )


@pytest.fixture
def do_licenses():
    remote_licenses = []

    def _processor(remote_license):
        setup(remote_license)
        remote_licenses.append(remote_license)
        _remote_licenses(remote_license)

        return "Added"

    yield _processor

    print(f"Cleaning up resources: {remote_licenses}")

    for remote_license in remote_licenses:
        atf.run_command(
            f"sacctmgr -i delete resource {remote_license['name']}",
            user=atf.properties["slurm-user"],
            fatal=True,
        )

        cluster_names = get_cluster_names(remote_license)
        for cluster in cluster_names:
            print(f"cluster: {cluster}")
            atf.run_command(
                f"sacctmgr -i delete cluster {cluster}",
                user=atf.properties["slurm-user"],
            )


def resources_to_list():
    output = atf.run_command_output(
        "sacctmgr -P show resource", user=atf.properties["slurm-user"], fatal=True
    )

    resources = []

    f = io.StringIO(output.strip())
    reader = csv.DictReader(f, delimiter="|")
    for row in reader:
        for key, value in row.items():
            if value.isdigit():
                row[key] = int(value)
        resources.append(row)
    return resources


def cluster_resources_to_dict():
    output = atf.run_command_output(
        "sacctmgr -P show resource withclusters",
        user=atf.properties["slurm-user"],
        fatal=True,
    )

    resources = {}

    f = io.StringIO(output.strip())
    reader = csv.DictReader(f, delimiter="|")
    for row in reader:
        for key, value in row.items():
            if value.isdigit():
                row[key] = int(value)
        cluster_name = row.pop("Cluster")
        resources[cluster_name] = row

    return resources


def _remote_licenses(remote_license):

    # Add license
    command = f"sacctmgr -i create resource {remote_license['name']} count={remote_license['count']}"
    if remote_license["flags"]:
        command += f" flags={remote_license["flags"]}"
    output = atf.run_command_output(
        command, user=atf.properties["slurm-user"], fatal=True
    )

    # Verify
    resource = resources_to_list()[0]

    assert resource["Name"] == remote_license["name"]
    assert resource["Server"] == "slurmdb"
    assert resource["Type"] == "License"
    assert resource["Count"] == remote_license["count"]
    assert resource["Flags"].lower() == remote_license["flags"].lower()

    # Add/modify cluster licenses
    for action in remote_license["actions"]:
        if action["type"] == "validate":
            pass  # no mods
        elif "clusters" in action:
            for cname, res in action["clusters"].items():
                mod_cmd = ""
                if action["type"] == "modify":
                    mod_cmd = " set"

                if "allowed" in res:
                    mod_cmd += f" allowed={res['allowed']}"
                if "flags" in res:
                    mod_cmd += f" flags{res['flags']}"

                proc = atf.run_command(
                    f"sacctmgr -i {action['type']} resource {remote_license['name']} cluster={cname} {mod_cmd}",
                    user=atf.properties["slurm-user"],
                )
                if "error" in action:
                    assert re.match(action["error"], proc["stderr"])
                    assert proc["exit_code"] == 1
                else:
                    assert proc["exit_code"] == 0
        else:
            mod_cmd = ""
            if action["type"] == "modify":
                mod_cmd = " set"

            if "flags" in action:
                mod_cmd += f" flags{action['flags']}"

            if "lastconsumed" in action:
                mod_cmd += f" lastconsumed={action['lastconsumed']}"

            proc = atf.run_command(
                f"sacctmgr -i {action['type']} resource {remote_license['name']} {mod_cmd}",
                user=atf.properties["slurm-user"],
            )
            if "error" in action:
                assert re.match(action["error"], proc["stderr"])
                assert proc["exit_code"] == 1
            else:
                assert proc["exit_code"] == 0

        # Verify
        resources = cluster_resources_to_dict()

        if "clusters" in action:
            for cname, res in action["clusters"].items():
                assert resources[cname]["Count"] == remote_license["count"]
                assert resources[cname]["Allocated"] == action["allocated"]
                if "expected_allowed" in res:
                    assert resources[cname]["Allowed"] == res["expected_allowed"]
                else:
                    assert resources[cname]["Allowed"] == res["allowed"]

            for cname, res in action["clusters"].items():
                output = atf.run_command_output(
                    "scontrol show lic --json",
                    user=atf.properties["slurm-user"],
                    fatal=True,
                )
                resources = json.loads(output)
                print(resources)

                resource = next(
                    (
                        item
                        for item in resources["licenses"]
                        if item["LicenseName"] == f"{remote_license['name']}@slurmdb"
                    ),
                    None,
                )
                print(resource)
                assert resource["Total"] == res["actual_total"]
                assert resource["Free"] == res["actual_free"]
                assert resource["Remote"]
                assert resource["Used"] == 0

                # TODO: Check other clusters
                break


@pytest.mark.parametrize(
    "preserve_case", [False, True], ids=["PreserveCase=No", "PreserveCase=yes"]
)
def test_remote_license_percent(request, do_licenses, preserve_case):
    """Remote licenses - Percent"""

    remote_license = {
        "name": "lic1",
        "count": 200,
        "flags": "",
        "preserve_case": preserve_case,
        "actions": [
            {
                "type": "add",
                "allocated": 100,
                "clusters": {
                    "c1": {"allowed": 25, "actual_total": 50, "actual_free": 50},
                    "c2": {"allowed": 75, "actual_total": 150, "actual_free": 150},
                },
            },
            {
                "type": "modify",
                "allocated": 60,
                "clusters": {
                    "c1": {"allowed": 10, "actual_total": 20, "actual_free": 20},
                    "c2": {"allowed": 50, "actual_total": 100, "actual_free": 100},
                },
            },
            {
                "type": "modify",
                "allocated": 100,
                "clusters": {
                    "c1": {"allowed": 50, "actual_total": 100, "actual_free": 100},
                    "c2": {"allowed": 50, "actual_total": 100, "actual_free": 100},
                },
            },
            {
                "type": "modify",
                "lastconsumed": 100,
            },
            {
                "type": "validate",
                "allocated": 100,
                "clusters": {
                    "c1": {"allowed": 50, "actual_total": 100, "actual_free": 100},
                    "c2": {"allowed": 50, "actual_total": 100, "actual_free": 100},
                },
            },
            {
                "type": "modify",
                "lastconsumed": 200,
            },
            {
                "type": "validate",
                "allocated": 100,
                "clusters": {
                    "c1": {"allowed": 50, "actual_total": 100, "actual_free": 0},
                    "c2": {"allowed": 50, "actual_total": 100, "actual_free": 0},
                },
            },
            {
                "type": "modify",
                "lastconsumed": 180,
            },
            {
                "type": "validate",
                "allocated": 100,
                "clusters": {
                    "c1": {"allowed": 50, "actual_total": 100, "actual_free": 20},
                    "c2": {"allowed": 50, "actual_total": 100, "actual_free": 20},
                },
            },
            {
                "type": "modify",
                "lastconsumed": 101,
            },
            {
                "type": "validate",
                "allocated": 100,
                "clusters": {
                    "c1": {"allowed": 50, "actual_total": 100, "actual_free": 99},
                    "c2": {"allowed": 50, "actual_total": 100, "actual_free": 99},
                },
            },
        ],
    }

    if preserve_case:
        if atf.get_version("bin/sacctmgr") < (26, 5):
            request.applymarker(
                pytest.mark.xfail(
                    reason="Issue 50782: PreserveCaseResource added in 26.05"
                )
            )
        remote_license["name"] = "LiC1"

    do_licenses(remote_license)


def test_remote_license_absolute(do_licenses):
    """Remote licenses - Absolute"""

    remote_license = {
        "name": "lic1",
        "count": 200,
        "flags": "absolute",
        "actions": [
            {
                "type": "add",
                "allocated": 100,
                "clusters": {
                    "c1": {"allowed": 25, "actual_total": 25, "actual_free": 25},
                    "c2": {"allowed": 75, "actual_total": 75, "actual_free": 75},
                },
            },
            {
                "type": "modify",
                "allocated": 60,
                "clusters": {
                    "c1": {"allowed": 10, "actual_total": 10, "actual_free": 10},
                    "c2": {"allowed": 50, "actual_total": 50, "actual_free": 50},
                },
            },
            {
                "type": "modify",
                "allocated": 200,
                "clusters": {
                    "c1": {"allowed": 100, "actual_total": 100, "actual_free": 100},
                    "c2": {"allowed": 100, "actual_total": 100, "actual_free": 100},
                },
            },
            {
                "type": "modify",
                "lastconsumed": 100,
            },
            {
                "type": "validate",
                "allocated": 200,
                "clusters": {
                    "c1": {"allowed": 100, "actual_total": 100, "actual_free": 100},
                    "c2": {"allowed": 100, "actual_total": 100, "actual_free": 100},
                },
            },
            {
                "type": "modify",
                "lastconsumed": 200,
            },
            {
                "type": "validate",
                "allocated": 200,
                "clusters": {
                    "c1": {"allowed": 100, "actual_total": 100, "actual_free": 0},
                    "c2": {"allowed": 100, "actual_total": 100, "actual_free": 0},
                },
            },
            {
                "type": "modify",
                "lastconsumed": 180,
            },
            {
                "type": "validate",
                "allocated": 200,
                "clusters": {
                    "c1": {"allowed": 100, "actual_total": 100, "actual_free": 20},
                    "c2": {"allowed": 100, "actual_total": 100, "actual_free": 20},
                },
            },
            {
                "type": "modify",
                "lastconsumed": 101,
            },
            {
                "type": "validate",
                "allocated": 200,
                "clusters": {
                    "c1": {"allowed": 100, "actual_total": 100, "actual_free": 99},
                    "c2": {"allowed": 100, "actual_total": 100, "actual_free": 99},
                },
            },
        ],
    }

    do_licenses(remote_license)


@pytest.mark.skipif(
    atf.get_version("bin/sacctmgr") < (26, 5),
    reason="Issue 50784: SharedPool added in 26.05.",
)
def test_remote_license_sharedpool(do_licenses):
    """Remote licenses - Percent,SharedPool"""

    remote_license = {
        "name": "lic1",
        "count": 200,
        "flags": "sharedpool",
        "actions": [
            {
                "type": "add",
                "allocated": 200,
                "clusters": {
                    "c1": {"allowed": 100, "actual_total": 200, "actual_free": 200},
                    "c2": {"allowed": 100, "actual_total": 200, "actual_free": 200},
                },
            },
            {"type": "modify", "lastconsumed": 150},
            {
                "type": "validate",
                "allocated": 200,
                "clusters": {
                    "c1": {"allowed": 100, "actual_total": 200, "actual_free": 50},
                    "c2": {"allowed": 100, "actual_total": 200, "actual_free": 50},
                },
            },
            {"type": "modify", "lastconsumed": 0},
            # Remove flag. Should only let you do it once the license usage is less
            # than 100%.
            {
                "type": "modify",
                "flags": "-=sharedpool",
                "error": r"sacctmgr: error: You can not allocate more than 100% of a resource.*",
            },
            {
                "type": "modify",
                "allocated": 200,
                "clusters": {
                    "c1": {
                        "allowed": 5,
                        "actual_total": 200,
                        "actual_free": 200,
                        "expected_allowed": 100,
                    },
                },
                "error": r"sacctmgr: error: Invalid value for Allowed field when SharedPool enabled, must be 0 or 100% \(without Absolute\) or Count \(with Absolute\)\..*",
            },
            {
                "type": "modify",
                "allocated": 0,
                "clusters": {
                    "c1": {"allowed": 0, "actual_total": 0, "actual_free": 0},
                    "c2": {"allowed": 0, "actual_total": 0, "actual_free": 0},
                },
            },
            {
                "type": "modify",
                "flags": "-=sharedpool",
            },
        ],
    }

    do_licenses(remote_license)


@pytest.mark.skipif(
    atf.get_version("bin/sacctmgr") < (26, 5),
    reason="Issue 50784: SharedPool added in 26.05.",
)
def test_remote_license_add_sharedpool(do_licenses):
    """Test adding SharedPool flag to existing external license"""

    remote_license = {
        "name": "lic1",
        "count": 200,
        "flags": "",
        "actions": [
            {
                "type": "add",
                "allocated": 100,
                "clusters": {
                    "c1": {"allowed": 50, "actual_total": 100, "actual_free": 100},
                    "c2": {"allowed": 50, "actual_total": 100, "actual_free": 100},
                },
            },
            {
                "type": "modify",
                "flags": "=sharedpool",
                "allocated": 100,
                "error": r"sacctmgr: error: Invalid value for Allowed field when SharedPool enabled, must be 0 or 100% \(without Absolute\) or Count \(with Absolute\)\..*",
            },
            {
                "type": "modify",
                "allocated": 0,
                "clusters": {
                    "c1": {"allowed": 0, "actual_total": 0, "actual_free": 0},
                    "c2": {"allowed": 0, "actual_total": 0, "actual_free": 0},
                },
            },
            {
                "type": "modify",
                "allocated": 0,
                "flags": "=sharedpool",
            },
            {
                "type": "modify",
                "allocated": 200,
                "clusters": {
                    "c1": {"allowed": 100, "actual_total": 200, "actual_free": 200},
                    "c2": {"allowed": 100, "actual_total": 200, "actual_free": 200},
                },
            },
        ],
    }

    do_licenses(remote_license)


@pytest.mark.skipif(
    atf.get_version("bin/sacctmgr") < (26, 5),
    reason="Issue 50784: SharedPool added in 26.05.",
)
def test_remote_license_abs_sharedpool(do_licenses):
    """Remote licenses - Absolute,SharedPool"""

    remote_license = {
        "name": "lic1",
        "count": 200,
        "flags": "absolute,sharedpool",
        "actions": [
            {
                "type": "add",
                "allocated": 400,
                "clusters": {
                    "c1": {"allowed": 200, "actual_total": 200, "actual_free": 200},
                    "c2": {"allowed": 200, "actual_total": 200, "actual_free": 200},
                },
            },
            # Remove flag. Should only let you do it once the license usage is less
            # than absolute value.
            {
                "type": "modify",
                "flags": "-=sharedpool",
                "error": r"sacctmgr: error: You can not allocate more than 100% of a resource.*",
            },
            # Setting flags. Should only let you do it once the license usage
            # is less than absolute value.
            {
                "type": "modify",
                "flags": "=absolute",
                "error": r"sacctmgr: error: You can not allocate more than 100% of a resource.*",
            },
            {
                "type": "modify",
                "allocated": 200,
                "clusters": {
                    "c1": {"allowed": 0, "actual_total": 0, "actual_free": 0},
                    "c2": {"allowed": 200, "actual_total": 200, "actual_free": 200},
                },
            },
            {
                "type": "modify",
                "flags": "-=sharedpool",
            },
        ],
    }

    do_licenses(remote_license)
