############################################################################
# Copyright (C) SchedMD LLC.
############################################################################
import atf
import pytest

import csv
import io
import json


def get_cluster_names(remote_license):
    """Find all cluster names"""

    cluster_names = {
        k
        for item in remote_license["actions"]
        if "clusters" in item
        for k in item["clusters"]
    }

    return cluster_names


# Setup
def setup(remote_license):
    cluster_names = get_cluster_names(remote_license)

    for cluster in cluster_names:
        atf.set_config_parameter("ClusterName", cluster)
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
        output = ""
        if action["type"] == "validate":
            pass  # no mods
        elif "clusters" in action:
            for cname, res in action["clusters"].items():
                mod_cmd = ""
                if action["type"] == "modify":
                    mod_cmd = " set"

                if res["allowed"]:
                    mod_cmd += f" allowed={res['allowed']}"
                if "flags" in res:
                    mod_cmd += f" flags{res['flags']}"

                output = atf.run_command_output(
                    f"sacctmgr -i {action['type']} resource {remote_license['name']} cluster={cname} {mod_cmd}",
                    user=atf.properties["slurm-user"],
                    fatal=True,
                )
        else:
            mod_cmd = ""
            if action["type"] == "modify":
                mod_cmd = " set"

            if "flags" in action:
                mod_cmd += f" flags{action['flags']}"

            output = atf.run_command_output(
                f"sacctmgr -i {action['type']} resource {remote_license['name']} {mod_cmd}",
                user=atf.properties["slurm-user"],
                fatal=True,
            )

        if "error" in action:
            assert action["error"] == output

        # Verify
        resources = cluster_resources_to_dict()

        if "clusters" in action:
            for cname, res in action["clusters"].items():
                assert resources[cname]["Count"] == remote_license["count"]
                assert resources[cname]["Allocated"] == action["allocated"]
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
                assert resource["Total"] == res["actual"]
                assert resource["Free"] == res["actual"]
                assert resource["Remote"]
                assert resource["Used"] == 0

                # TODO: Check other clusters
                break


def test_remote_license_percent(do_licenses):
    """Remote licenses - Percent"""

    remote_license = {
        "name": "lic1",
        "count": 200,
        "flags": "",
        "actions": [
            {
                "type": "add",
                "allocated": 100,
                "clusters": {
                    "c1": {"allowed": 25, "actual": 50},
                    "c2": {"allowed": 75, "actual": 150},
                },
            },
            {
                "type": "modify",
                "allocated": 60,
                "clusters": {
                    "c1": {"allowed": 10, "actual": 20},
                    "c2": {"allowed": 50, "actual": 100},
                },
            },
        ],
    }

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
                    "c1": {"allowed": 25, "actual": 25},
                    "c2": {"allowed": 75, "actual": 75},
                },
            },
            {
                "type": "modify",
                "allocated": 60,
                "clusters": {
                    "c1": {"allowed": 10, "actual": 10},
                    "c2": {"allowed": 50, "actual": 50},
                },
            },
        ],
    }

    do_licenses(remote_license)
