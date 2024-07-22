############################################################################
# Copyright (C) SchedMD LLC.
############################################################################
import atf
import getpass
import json
import logging
import math
import os
import pathlib
import pytest
import re
import requests
import socket
import subprocess
import sys
import time
from pprint import pprint


cluster_name = "test-cluster-taco"
cluster2_name = "test-cluster-taco2"
user_name = "test-user-taco"
local_user_name = getpass.getuser()
account_name = "test-account-taco"
account2_name = "test-account-taco2"
coord_name = "test-user-taco-commander"
wckey_name = "test-wckey-taco"
wckey2_name = "test-wckey-taco2"
qos_name = "test-qos-taco"
qos2_name = "test-qos-taco2"
resv_name = "test-reservation-taco"


slurmrestd = None


def run(cmd, fatal):
    return atf.run_command(cmd, fatal=fatal)


def run_fail(cmd, fatal):
    return atf.run_command(cmd, fatal=fatal, xfail=True)


@pytest.fixture(scope="module", autouse=True)
def setup():
    global slurmrestd_url, token, slurmrestd
    global local_cluster_name, partition_name

    atf.require_accounting(modify=True)
    atf.require_config_parameter("AllowNoDefAcct", "Yes", source="slurmdbd")
    atf.require_config_parameter("TrackWCKey", "Yes", source="slurmdbd")
    atf.require_config_parameter("TrackWCKey", "Yes")
    atf.require_config_parameter("AuthAltTypes", "auth/jwt")
    atf.require_config_parameter("AuthAltTypes", "auth/jwt", source="slurmdbd")
    atf.require_slurmrestd("v0.0.39,dbv0.0.39", None)
    atf.require_slurm_running()

    # Setup OpenAPI client with OpenAPI-Generator once Slurm(restd) is running
    atf.require_openapi_generator("7.2.0")

    # Conf reliant variables (put here to avert --auto-config errors)
    local_cluster_name = atf.get_config_parameter("ClusterName")

    # local_user_name needs to have an association due ticket 20394.
    # It also needs AdminLevel to be able to run commands like slurm.slurm_<ver>_diag()
    atf.run_command(
        f"sacctmgr -i add user {local_user_name} defaultaccount=root AdminLevel=Admin",
        user=atf.properties["slurm-user"],
    )

    partition_name = atf.default_partition()
    if not partition_name:
        partition_name = "debug"


@pytest.fixture(scope="function")
def slurm(setup):
    yield atf.openapi_slurm()


def test_loaded_versions():
    r = atf.request_slurmrestd("openapi/v3")
    assert r.status_code == 200

    spec = r.json()

    # verify older plugins are not loaded
    assert "/slurm/v0.0.41/jobs" not in spec["paths"].keys()
    assert "/slurm/v0.0.40/jobs" not in spec["paths"].keys()
    assert "/slurm/v0.0.38/jobs" not in spec["paths"].keys()
    assert "/slurmdb/v0.0.38/jobs" not in spec["paths"].keys()
    assert "/slurm/v0.0.37/jobs" not in spec["paths"].keys()
    assert "/slurmdb/v0.0.37/jobs" not in spec["paths"].keys()
    assert "/slurm/v0.0.36/jobs" not in spec["paths"].keys()
    assert "/slurmdb/v0.0.36/jobs" not in spec["paths"].keys()
    assert "/slurm/v0.0.35/jobs" not in spec["paths"].keys()

    # verify current plugins are loaded
    assert "/slurm/v0.0.39/jobs" in spec["paths"].keys()
    assert "/slurmdb/v0.0.39/jobs" in spec["paths"].keys()


def purge():
    atf.run_command("scontrol delete reservation {}".format(resv_name), fatal=False)
    atf.run_command("sacctmgr -i delete wckey {}".format(wckey_name), fatal=False)
    atf.run_command("sacctmgr -i delete wckey {}".format(wckey2_name), fatal=False)
    atf.run_command(
        "sacctmgr -i delete account {} cluster={}".format(account2_name, cluster_name),
        fatal=False,
    )
    atf.run_command("sacctmgr -i delete account {}".format(account2_name), fatal=False)
    atf.run_command(
        "sacctmgr -i delete account {} cluster={}".format(account_name, cluster_name),
        fatal=False,
    )
    atf.run_command("sacctmgr -i delete account {}".format(account_name), fatal=False)
    atf.run_command("sacctmgr -i delete qos {}".format(qos_name), fatal=False)
    atf.run_command("sacctmgr -i delete qos {}".format(qos2_name), fatal=False)
    atf.run_command(
        "sacctmgr -i delete user {} cluster={}".format(coord_name, cluster_name),
        fatal=False,
    )
    atf.run_command(
        "sacctmgr -i delete user {} cluster={}".format(user_name, cluster_name),
        fatal=False,
    )
    atf.run_command(
        "sacctmgr -i delete user {} cluster={}".format(coord_name, local_cluster_name),
        fatal=False,
    )
    atf.run_command(
        "sacctmgr -i delete user {} cluster={}".format(user_name, local_cluster_name),
        fatal=False,
    )
    atf.run_command("sacctmgr -i delete user {}".format(coord_name), fatal=False)
    atf.run_command("sacctmgr -i delete user {}".format(user_name), fatal=False)
    atf.run_command("sacctmgr -i delete cluster {}".format(cluster_name), fatal=False)
    atf.run_command("sacctmgr -i delete cluster {}".format(cluster2_name), fatal=False)


@pytest.fixture(scope="function", autouse=True)
def cleanup():
    purge()
    yield
    purge()


def test_db_accounts(slurm):
    from openapi_client import ApiClient as Client
    from openapi_client import Configuration as Config
    from openapi_client.models.dbv0039_account_info import Dbv0039AccountInfo
    from openapi_client.models.v0039_account import V0039Account
    from openapi_client.models.v0039_assoc_short import V0039AssocShort
    from openapi_client.models.v0039_coord import V0039Coord

    atf.run_command(
        "sacctmgr -i create user {} cluster={}".format(user_name, local_cluster_name),
        fatal=False,
    )
    atf.run_command(
        "sacctmgr -i create user {} cluster={}".format(coord_name, local_cluster_name),
        fatal=False,
    )

    # make sure account doesnt already exist
    resp = slurm.slurmdb_v0039_get_account(account2_name)
    assert len(resp.warnings) > 0
    assert not len(resp.errors)
    assert not len(resp.accounts)

    # create account
    accounts = Dbv0039AccountInfo(
        accounts=[
            V0039Account(
                description="test description",
                name=account_name,
                organization="test organization",
            ),
            V0039Account(
                coordinators=[
                    V0039Coord(
                        name=coord_name,
                    )
                ],
                description="test description",
                name=account2_name,
                organization="test organization",
            ),
        ]
    )
    resp = slurm.slurmdb_v0039_update_accounts(accounts)
    assert not resp.warnings
    assert not resp.errors

    # test setting DELETE flag is warned about
    accounts = Dbv0039AccountInfo(
        accounts=[
            V0039Account(
                name=account2_name,
                description="fail description",
                organization="fail organization",
                flags=["DELETED"],
            )
        ]
    )
    resp = slurm.slurmdb_v0039_update_accounts(accounts)
    assert len(resp.warnings) > 0
    assert len(resp.errors) == 0

    # verify account matches modifiy request
    resp = slurm.slurmdb_v0039_get_account(account2_name)
    assert resp.accounts
    for account in resp.accounts:
        assert account.name == account2_name
        assert account.description == accounts.accounts[0].description
        assert account.organization == accounts.accounts[0].organization
        assert not account.flags

        # change account desc and org
    accounts = Dbv0039AccountInfo(
        accounts=[
            V0039Account(
                coordinators=[],
                description="test description modified",
                name=account2_name,
                organization="test organization modified",
            )
        ]
    )
    resp = slurm.slurmdb_v0039_update_accounts(accounts)
    assert not resp.warnings
    assert len(resp.errors) == 0
    resp = slurm.slurmdb_v0039_get_account(account2_name)
    assert resp.accounts
    for account in resp.accounts:
        assert account.name == account2_name
        assert account.description == accounts.accounts[0].description
        assert account.organization == accounts.accounts[0].organization
        assert not account.coordinators

    resp = slurm.slurmdb_v0039_get_account(account_name)
    assert resp.accounts
    for account in resp.accounts:
        assert account.name == account_name

        # check full listing works
    resp = slurm.slurmdb_v0039_get_accounts(with_deleted="true")
    assert resp.accounts
    resp = slurm.slurmdb_v0039_get_accounts()
    assert resp.accounts

    accounts = Dbv0039AccountInfo(
        accounts=[
            V0039Account(
                coordinators=[],
                description="test description modified",
                name=account2_name,
                organization="test organization modified",
            )
        ]
    )

    resp = slurm.slurmdb_v0039_delete_account(account_name)
    assert not resp.warnings
    assert len(resp.errors) == 0

    resp = slurm.slurmdb_v0039_delete_account(account2_name)
    assert not resp.warnings
    assert len(resp.errors) == 0

    resp = slurm.slurmdb_v0039_get_account(account_name)
    assert len(resp.warnings) > 0
    assert len(resp.errors) == 0
    assert not resp.accounts

    resp = slurm.slurmdb_v0039_get_account(account2_name)
    assert len(resp.warnings) > 0
    assert len(resp.errors) == 0
    assert not resp.accounts


def test_db_diag(slurm):
    resp = slurm.slurmdb_v0039_diag()
    assert not resp.warnings
    assert len(resp.errors) == 0
    assert resp.statistics


def test_db_wckeys(slurm):
    from openapi_client.models.v0039_wckey import V0039Wckey
    from openapi_client.models.dbv0039_wckey_info import Dbv0039WckeyInfo

    atf.run_command(
        "sacctmgr -i create user {} cluster={}".format(user_name, local_cluster_name),
        fatal=False,
    )
    atf.run_command(
        "sacctmgr -i create user {} cluster={}".format(coord_name, local_cluster_name),
        fatal=False,
    )

    wckeys = [
        V0039Wckey(
            cluster=local_cluster_name,
            name=wckey_name,
            user=user_name,
        ),
        V0039Wckey(
            cluster=local_cluster_name,
            name=wckey2_name,
            user=user_name,
        ),
        V0039Wckey(
            cluster=local_cluster_name,
            name=wckey2_name,
            user=coord_name,
        ),
    ]

    resp = slurm.slurmdb_v0039_add_wckeys(Dbv0039WckeyInfo(wckeys=wckeys))
    assert not resp.warnings
    assert len(resp.errors) == 0

    resp = slurm.slurmdb_v0039_get_wckeys()
    # not present in v39: assert not resp.warnings
    assert len(resp.errors) == 0
    assert len(resp.wckeys) >= 1

    resp = slurm.slurmdb_v0039_get_wckey(wckey_name)
    # not present in v39: assert not resp.warnings
    assert len(resp.errors) == 0
    assert resp.wckeys
    for wckey in resp.wckeys:
        assert wckey.name == wckey_name or wckey.name == wckey2_name
        assert wckey.user == user_name or wckey.user == coord_name

    resp = slurm.slurmdb_v0039_get_wckey(wckey2_name)
    # not present in v39: assert not resp.warnings
    assert len(resp.errors) == 0
    assert resp.wckeys
    for wckey in resp.wckeys:
        assert wckey.name == wckey2_name
        assert wckey.user == user_name or wckey.user == coord_name

    resp = slurm.slurmdb_v0039_delete_wckey(wckey_name)
    # not present in v39: assert not resp.warnings
    assert len(resp.errors) == 0

    resp = slurm.slurmdb_v0039_delete_wckey(wckey2_name)
    # not present in v39: assert not resp.warnings
    assert len(resp.errors) == 0

    resp = slurm.slurmdb_v0039_get_wckey(wckey_name)
    # not present in v39: assert not resp.warnings
    assert len(resp.errors) == 0
    # FIXME: bug#18939 assert len(resp.wckeys) == 0

    resp = slurm.slurmdb_v0039_get_wckey(wckey2_name)
    # not present in v39: assert not resp.warnings
    assert len(resp.errors) == 0
    # FIXME: bug#18939 assert len(resp.wckeys) == 0


def test_db_clusters(slurm):
    from openapi_client.models.status import Status
    from openapi_client.models.dbv0039_clusters_info import Dbv0039ClustersInfo
    from openapi_client.models.v0039_cluster_rec import V0039ClusterRec

    clusters = [
        V0039ClusterRec(
            name=cluster_name,
        ),
        V0039ClusterRec(
            name=cluster2_name,
        ),
    ]

    resp = slurm.slurmdb_v0039_add_clusters(Dbv0039ClustersInfo(clusters=clusters))
    assert not resp.warnings
    assert len(resp.errors) == 0

    resp = slurm.slurmdb_v0039_get_clusters()
    assert not resp.warnings
    assert len(resp.errors) == 0
    assert resp.clusters

    resp = slurm.slurmdb_v0039_get_cluster(cluster_name)
    assert not resp.warnings
    assert len(resp.errors) == 0
    assert resp.clusters
    for cluster in resp.clusters:
        assert cluster.name == cluster_name
        assert not cluster.nodes

    resp = slurm.slurmdb_v0039_get_cluster(cluster2_name)
    assert not resp.warnings
    assert len(resp.errors) == 0
    assert resp.clusters
    for cluster in resp.clusters:
        assert cluster.name == cluster2_name
        assert not cluster.nodes

    resp = slurm.slurmdb_v0039_delete_cluster(cluster_name)
    assert len(resp.errors) == 0

    resp = slurm.slurmdb_v0039_delete_cluster(cluster2_name)
    assert len(resp.errors) == 0

    resp = slurm.slurmdb_v0039_get_cluster(cluster_name)
    assert len(resp.warnings) > 0
    assert len(resp.errors) == 0
    assert not resp.clusters

    resp = slurm.slurmdb_v0039_get_cluster(cluster2_name)
    assert len(resp.warnings) > 0
    assert len(resp.errors) == 0
    assert not resp.clusters


def test_db_users(slurm):
    from openapi_client.models.status import Status
    from openapi_client.models.dbv0039_update_users import Dbv0039UpdateUsers
    from openapi_client.models.v0039_assoc_short import V0039AssocShort
    from openapi_client.models.v0039_coord import V0039Coord
    from openapi_client.models.v0039_user import V0039User
    from openapi_client.models.v0039_user_default import V0039UserDefault
    from openapi_client.models.v0039_wckey import V0039Wckey

    atf.run_command("sacctmgr -i create wckey {}".format(wckey_name), fatal=False)
    atf.run_command("sacctmgr -i create wckey {}".format(wckey2_name), fatal=False)

    users = [
        V0039User(
            administrator_level=["None"],
            default=dict(
                wckey=wckey_name,
            ),
            name=user_name,
        ),
        V0039User(
            administrator_level=["Operator"],
            wckeys=[
                V0039Wckey(
                    cluster=local_cluster_name, name=wckey_name, user=coord_name
                ),
                V0039Wckey(
                    cluster=local_cluster_name,
                    name=wckey2_name,
                    user=coord_name,
                ),
            ],
            default=V0039UserDefault(
                wckey=wckey2_name,
            ),
            name=coord_name,
        ),
    ]

    resp = slurm.slurmdb_v0039_update_users(Dbv0039UpdateUsers(users=users))
    resp = slurm.slurmdb_v0039_update_users(Dbv0039UpdateUsers(users=users))
    assert not resp.warnings
    assert len(resp.errors) == 0

    resp = slurm.slurmdb_v0039_get_users()
    assert not resp.warnings
    assert len(resp.errors) == 0
    assert resp.users

    resp = slurm.slurmdb_v0039_get_user(user_name)
    assert not resp.warnings
    assert len(resp.errors) == 0
    assert resp.users
    for user in resp.users:
        assert user.name == user_name
        assert user.default.wckey == wckey_name

    resp = slurm.slurmdb_v0039_get_user(coord_name)
    assert not resp.warnings
    assert len(resp.errors) == 0
    assert resp.users
    for user in resp.users:
        assert user.name == coord_name
        assert user.default.wckey == wckey2_name
        for wckey in user.wckeys:
            assert wckey.name == wckey_name or wckey.name == wckey2_name
            assert wckey.user == coord_name
            assert wckey.cluster == local_cluster_name

    resp = slurm.slurmdb_v0039_delete_user(coord_name)
    assert len(resp.errors) == 0

    user_exists = False
    resp = slurm.slurmdb_v0039_get_user(coord_name, with_deleted="true")
    assert len(resp.errors) == 0
    for user in resp.users:
        assert user.name == coord_name
        assert user.flags[0] == "DELETED"
        user_exists = True

    if not user_exists:
        users = [
            V0039User(
                administrator_level=["Administrator"],
                default=dict(
                    wckey=wckey_name,
                ),
                old_name=user_name,
                name=coord_name,
            )
        ]

        resp = slurm.slurmdb_v0039_update_users(Dbv0039UpdateUsers(users=users))
        assert not resp.warnings
        assert len(resp.errors) == 0

        resp = slurm.slurmdb_v0039_get_user(coord_name)
        assert not resp.warnings
        assert len(resp.errors) == 0
        assert resp.users
        for user in resp.users:
            assert user.name == coord_name
            assert not user.old_name
            assert user.default.wckey == wckey_name
            for wckey in user.wckeys:
                assert wckey.name == wckey_name
                assert wckey.user == coord_name
                assert wckey.cluster == local_cluster_name

        resp = slurm.slurmdb_v0039_delete_user(coord_name)
        assert len(resp.errors) == 0

        resp = slurm.slurmdb_v0039_get_user(coord_name)
        assert len(resp.warnings) > 0
        assert len(resp.errors) == 0
        assert not resp.users


def test_db_assoc(slurm):
    from openapi_client.models.status import Status
    from openapi_client.models.dbv0039_update_users import Dbv0039UpdateUsers
    from openapi_client.models.dbv0039_associations_info import Dbv0039AssociationsInfo
    from openapi_client.models.v0039_assoc import V0039Assoc
    from openapi_client.models.v0039_assoc_short import V0039AssocShort
    from openapi_client.models.v0039_coord import V0039Coord
    from openapi_client.models.v0039_user import V0039User
    from openapi_client.models.v0039_wckey import V0039Wckey
    from openapi_client.models.v0039_uint32_no_val import V0039Uint32NoVal

    atf.run_command("sacctmgr -i create account {}".format(account_name), fatal=False)
    atf.run_command("sacctmgr -i create account {}".format(account2_name), fatal=False)
    atf.run_command(
        "sacctmgr -i create user {} cluster={}".format(user_name, local_cluster_name),
        fatal=False,
    )
    atf.run_command(
        "sacctmgr -i create user {} cluster={}".format(coord_name, local_cluster_name),
        fatal=False,
    )
    atf.run_command("sacctmgr -i create wckey {}".format(wckey_name), fatal=False)
    atf.run_command("sacctmgr -i create wckey {}".format(wckey2_name), fatal=False)
    atf.run_command("sacctmgr -i create qos {}".format(qos_name), fatal=False)
    atf.run_command("sacctmgr -i create qos {}".format(qos2_name), fatal=False)

    associations = [
        V0039Assoc(
            account=account_name,
            cluster=local_cluster_name,
            default=dict(
                qos=qos_name,
            ),
            flags=[],
            max=dict(
                jobs=dict(
                    per=dict(
                        wall_clock=V0039Uint32NoVal(
                            set=True,
                            number=150,
                        )
                    ),
                ),
            ),
            min=dict(
                priority_threshold=V0039Uint32NoVal(
                    set=True,
                    number=10,
                )
            ),
            partition=partition_name,
            priority=V0039Uint32NoVal(number=9, set=True),
            qos=[qos_name, qos2_name],
            shares_raw=23,
            user=user_name,
        ),
        V0039Assoc(
            account=account_name,
            cluster=local_cluster_name,
            default=dict(
                qos=qos_name,
            ),
            flags=[],
            max=dict(
                jobs=dict(
                    per=dict(
                        wall_clock=V0039Uint32NoVal(
                            set=True,
                            number=150,
                        )
                    ),
                ),
            ),
            min=dict(
                priority_threshold=V0039Uint32NoVal(
                    set=True,
                    number=10,
                )
            ),
            priority=V0039Uint32NoVal(number=9, set=True),
            qos=[qos_name, qos2_name],
            shares_raw=23,
            user=user_name,
        ),
        V0039Assoc(
            account=account2_name,
            cluster=local_cluster_name,
            default=dict(
                qos=qos2_name,
            ),
            flags=[],
            max=dict(
                jobs=dict(
                    per=dict(
                        wall_clock=V0039Uint32NoVal(
                            set=True,
                            number=50,
                        )
                    ),
                ),
            ),
            min=dict(
                priority_threshold=V0039Uint32NoVal(
                    set=True,
                    number=4,
                )
            ),
            partition=partition_name,
            priority=V0039Uint32NoVal(number=90, set=True),
            qos=[qos2_name],
            shares_raw=1012,
            user=user_name,
        ),
    ]

    resp = slurm.slurmdb_v0039_update_associations(
        Dbv0039AssociationsInfo(associations=associations)
    )
    assert not resp.warnings
    assert len(resp.errors) == 0

    resp = slurm.slurmdb_v0039_get_associations()
    assert not resp.warnings
    assert len(resp.errors) == 0
    assert resp.associations

    resp = slurm.slurmdb_v0039_get_association(
        cluster=local_cluster_name,
        account=account_name,
        user=user_name,
        partition=partition_name,
    )
    assert not resp.warnings
    assert len(resp.errors) == 0
    assert resp.associations
    for assoc in resp.associations:
        assert assoc.cluster == local_cluster_name
        assert assoc.account == account_name
        assert assoc.user == user_name
        assert assoc.partition == partition_name
        assert assoc.default.qos == qos_name
        assert not assoc.flags
        assert assoc.max.jobs.per.wall_clock.set
        assert assoc.max.jobs.per.wall_clock.number == 150
        assert assoc.min.priority_threshold.set
        assert assoc.min.priority_threshold.number == 10
        assert assoc.priority.set
        assert assoc.priority.number == 9
        for qos in assoc.qos:
            assert qos == qos_name or qos == qos2_name
        assert assoc.shares_raw == 23

    associations = [
        V0039Assoc(
            account=account_name,
            cluster=local_cluster_name,
            partition=partition_name,
            user=user_name,
            default=dict(
                qos=qos2_name,
            ),
            qos=[qos2_name],
            max=dict(
                jobs=dict(
                    per=dict(wall_clock=V0039Uint32NoVal(set=True, number=250)),
                ),
            ),
            min=dict(
                priority_threshold=V0039Uint32NoVal(set=True, number=100),
            ),
            priority=V0039Uint32NoVal(number=848, set=True),
            shares_raw=230,
        )
    ]

    resp = slurm.slurmdb_v0039_update_associations(
        Dbv0039AssociationsInfo(associations=associations)
    )
    assert not resp.warnings
    assert len(resp.errors) == 0

    resp = slurm.slurmdb_v0039_get_association(
        cluster=local_cluster_name,
        account=account_name,
        user=user_name,
        partition=partition_name,
    )
    assert not resp.warnings
    assert len(resp.errors) == 0
    assert resp.associations
    for assoc in resp.associations:
        assert assoc.cluster == local_cluster_name
        assert assoc.account == account_name
        assert assoc.user == user_name
        assert assoc.partition == partition_name
        assert assoc.default.qos == qos2_name
        assert not assoc.flags
        assert assoc.max.jobs.per.wall_clock.set
        assert assoc.max.jobs.per.wall_clock.number == 250
        assert assoc.min.priority_threshold.set
        assert assoc.min.priority_threshold.number == 100
        assert assoc.priority.set
        assert assoc.priority.number == 848
        for qos in assoc.qos:
            assert qos == qos2_name
        assert assoc.shares_raw == 230

    resp = slurm.slurmdb_v0039_delete_association(
        cluster=local_cluster_name,
        account=account_name,
        user=user_name,
        partition=partition_name,
    )
    assert len(resp.errors) == 0

    resp = slurm.slurmdb_v0039_get_association(
        cluster=local_cluster_name,
        account=account_name,
        user=user_name,
        partition=partition_name,
    )
    assert len(resp.warnings) > 0
    assert len(resp.errors) == 0
    assert not resp.associations

    resp = slurm.slurmdb_v0039_delete_associations(
        cluster=local_cluster_name,
        account=account_name,
    )
    assert len(resp.errors) == 0

    resp = slurm.slurmdb_v0039_get_association(
        cluster=local_cluster_name,
        account=account_name,
    )
    assert len(resp.warnings) > 0
    assert len(resp.errors) == 0
    assert not resp.associations

    resp = slurm.slurmdb_v0039_delete_associations(
        cluster=local_cluster_name,
        account=account2_name,
    )
    assert len(resp.errors) == 0

    resp = slurm.slurmdb_v0039_get_association(
        cluster=local_cluster_name,
        account=account2_name,
    )
    assert len(resp.warnings) > 0
    assert len(resp.errors) == 0
    assert not resp.associations


def test_db_qos(slurm):
    from openapi_client.models.status import Status
    from openapi_client.models.v0039_qos import V0039Qos
    from openapi_client.models.v0039_tres import V0039Tres
    from openapi_client.models.dbv0039_update_qos import Dbv0039UpdateQos
    from openapi_client.models.v0039_float64_no_val import V0039Float64NoVal
    from openapi_client.models.v0039_uint32_no_val import V0039Uint32NoVal

    atf.run_command("sacctmgr -i create account {}".format(account_name), fatal=False)
    atf.run_command("sacctmgr -i create account {}".format(account2_name), fatal=False)
    atf.run_command(
        "sacctmgr -i create user {} cluster={} acccount={}".format(
            user_name, local_cluster_name, account_name
        ),
        fatal=False,
    )
    atf.run_command(
        "sacctmgr -i create user {} cluster={} account={}".format(
            coord_name, local_cluster_name, account2_name
        ),
        fatal=False,
    )
    atf.run_command(
        "sacctmgr -i create wckey {} account={}".format(wckey_name, account_name),
        fatal=False,
    )
    atf.run_command(
        "sacctmgr -i create wckey {} account={}".format(wckey2_name, account2_name),
        fatal=False,
    )

    qos = [
        V0039Qos(
            description="test QOS",
            flags=[
                "PARTITION_MAXIMUM_NODE",
                "PARTITION_TIME_LIMIT",
                "ENFORCE_USAGE_THRESHOLD",
                "NO_RESERVE",
                "DENY_LIMIT",
                "OVERRIDE_PARTITION_QOS",
                "NO_DECAY",
            ],
            limits=dict(
                min=dict(
                    tres=dict(
                        per=dict(
                            job=[
                                V0039Tres(
                                    type="cpu",
                                    count=100,
                                ),
                                V0039Tres(
                                    type="memory",
                                    count=100000,
                                ),
                            ],
                        ),
                    ),
                ),
            ),
            name=qos_name,
            preempt=dict(
                exempt_time=V0039Uint32NoVal(set=True, number=199),
            ),
            priority=V0039Uint32NoVal(number=180, set=True),
            usage_factor=V0039Float64NoVal(
                set=True,
                number=82382.23823,
            ),
            usage_threshold=V0039Float64NoVal(
                set=True,
                number=929392.33,
            ),
        ),
        V0039Qos(
            description="test QOS 2",
            name=qos2_name,
        ),
    ]

    resp = slurm.slurmdb_v0039_update_qos(Dbv0039UpdateQos(qos=qos))
    assert not resp.warnings
    assert len(resp.errors) == 0

    resp = slurm.slurmdb_v0039_get_qos()
    assert not resp.warnings
    assert len(resp.errors) == 0
    assert resp.qos

    resp = slurm.slurmdb_v0039_get_single_qos(qos_name)
    assert not resp.warnings
    assert len(resp.errors) == 0
    assert resp.qos
    for qos in resp.qos:
        assert qos.description == "test QOS"
        assert qos.flags
        for flag in qos.flags:
            assert flag in [
                "PARTITION_MAXIMUM_NODE",
                "PARTITION_TIME_LIMIT",
                "ENFORCE_USAGE_THRESHOLD",
                "NO_RESERVE",
                "DENY_LIMIT",
                "OVERRIDE_PARTITION_QOS",
                "NO_DECAY",
            ]
        assert qos.limits.min.tres.per.job
        for tres in qos.limits.min.tres.per.job:
            assert tres.type == "cpu" or tres.type == "memory"
            if tres.type == "cpu":
                assert tres.count == 100
            if tres.type == "memory":
                assert tres.count == 100000
        assert qos.name == qos_name
        assert qos.preempt.exempt_time.set
        assert qos.preempt.exempt_time.number == 199
        assert qos.priority.set
        assert qos.priority.number == 180
        assert qos.usage_factor.set
        assert qos.usage_factor.number == 82382.23823
        assert qos.usage_threshold.set
        assert qos.usage_threshold.number == 929392.33

    resp = slurm.slurmdb_v0039_get_single_qos(qos2_name)
    assert len(resp.warnings) == 0
    assert len(resp.errors) == 0
    assert resp.qos
    for qos in resp.qos:
        assert qos.description == "test QOS 2"
        assert not qos.flags
        assert not qos.limits.min.tres.per.job
        assert qos.name == qos2_name
        assert not qos.preempt.exempt_time.set
        assert qos.priority.set
        assert qos.priority.number == 0
        assert qos.usage_factor.set
        assert qos.usage_factor.number == 1
        assert not qos.usage_threshold.set

    resp = slurm.slurmdb_v0039_delete_qos(qos_name)
    assert len(resp.warnings) == 0
    assert len(resp.errors) == 0

    resp = slurm.slurmdb_v0039_get_single_qos(qos_name)
    assert len(resp.warnings) == 0
    assert len(resp.errors) == 0
    assert not resp.qos

    resp = slurm.slurmdb_v0039_delete_qos(qos2_name)
    assert len(resp.warnings) == 0
    assert len(resp.errors) == 0

    resp = slurm.slurmdb_v0039_get_single_qos(qos2_name)
    assert len(resp.warnings) == 0
    assert len(resp.errors) == 0
    assert not resp.qos


def test_db_tres(slurm):
    resp = slurm.slurmdb_v0039_get_tres()
    assert len(resp.warnings) == 0
    assert len(resp.errors) == 0


def test_db_config(slurm):
    resp = slurm.slurmdb_v0039_get_config()
    assert len(resp.warnings) == 0
    assert len(resp.errors) == 0


def test_jobs(slurm):
    from openapi_client.models.status import Status
    from openapi_client.models.v0039_job_submission import V0039JobSubmission
    from openapi_client.models.v0039_job_submission_response import (
        V0039JobSubmissionResponse,
    )
    from openapi_client.models.v0039_job_desc_msg import V0039JobDescMsg
    from openapi_client.models.v0039_job_info import V0039JobInfo
    from openapi_client.models.v0039_uint32_no_val import V0039Uint32NoVal

    script = "#!/bin/bash\n/bin/true"
    env = ["PATH=/bin/:/sbin/:/usr/bin/:/usr/sbin/"]

    job = V0039JobSubmission(
        script=script,
        job=V0039JobDescMsg(
            partition=partition_name,
            name="test job",
            environment=env,
            current_working_directory="/tmp/",
        ),
    )

    resp = slurm.slurm_v0039_submit_job(job)
    assert len(resp.warnings) == 0
    assert len(resp.errors) == 0
    assert resp.job_id
    assert resp.step_id
    jobid = int(resp.job_id)

    resp = slurm.slurm_v0039_get_jobs()
    assert len(resp.warnings) == 0
    assert len(resp.errors) == 0

    resp = slurm.slurm_v0039_get_job(str(jobid))
    assert len(resp.warnings) == 0
    assert len(resp.errors) == 0
    for job in resp.jobs:
        assert job.job_id == jobid
        assert job.name == "test job"
        assert job.partition == partition_name

        # submit a HELD job to be able to update it
    job = V0039JobSubmission(
        script=script,
        job=V0039JobDescMsg(
            partition=partition_name,
            name="test job",
            environment=env,
            priority=V0039Uint32NoVal(number=0, set=True),
            current_working_directory="/tmp/",
        ),
    )

    resp = slurm.slurm_v0039_submit_job(job)
    assert len(resp.warnings) > 0
    assert len(resp.errors) == 0
    assert resp.job_id
    assert resp.step_id
    jobid = int(resp.job_id)

    # Disabled until v0.0.40 due double $refs not being supported
    #    job = V0039JobSubmission(
    #            job=V0039JobDescMsg(
    #                environment=env,
    #                partition=partition_name,
    #                name="updated test job",
    #                priority=V0039Uint32NoVal(number=0, set=True),
    #            )
    #    )
    #
    #    resp = slurm.slurm_v0039_update_job(str(jobid))
    #    assert not resp.warnings
    #    assert not resp.errors

    resp = slurm.slurm_v0039_get_job(str(jobid))
    assert len(resp.warnings) == 0
    assert len(resp.errors) == 0
    for job in resp.jobs:
        assert job.job_id == jobid
        assert job.name == "test job"
        assert job.partition == partition_name
        assert job.priority.set
        assert job.priority.number == 0
        assert job.user_name == local_user_name

    resp = slurm.slurm_v0039_cancel_job(str(jobid))
    assert len(resp.warnings) == 0
    assert len(resp.errors) == 0

    resp = slurm.slurm_v0039_get_job(str(jobid))
    assert len(resp.warnings) == 0
    assert len(resp.errors) == 0
    for job in resp.jobs:
        assert job.job_id == jobid
        assert job.name == "test job"
        assert job.partition == partition_name
        assert job.user_name == local_user_name
        assert job.job_state == "CANCELLED"

    # Wait until jobs are in the DB
    atf.repeat_command_until(
        f"sacct -Pnu {local_user_name} --format=user",
        lambda results: re.search(rf"{local_user_name}", results["stdout"]),
    )

    resp = slurm.slurmdb_v0039_get_jobs(local_user_name)
    assert len(resp.warnings) == 0
    assert len(resp.errors) == 0
    assert resp.jobs
    for job in resp.jobs:
        assert job.user == local_user_name

    resp = slurm.slurmdb_v0039_get_jobs()
    assert len(resp.warnings) == 0
    assert len(resp.errors) == 0

    requery = True
    while requery:
        resp = slurm.slurmdb_v0039_get_job(str(jobid))
        assert len(resp.warnings) == 0
        assert len(resp.errors) == 0
        assert resp.jobs
        for job in resp.jobs:
            if job.name == "allocation":
                # job hasn't settled at slurmdbd yet
                requery = True
            else:
                requery = False
                assert job.job_id == jobid
                assert job.name == "test job"
                assert job.partition == partition_name


@pytest.fixture(scope="function")
def reservation(setup):
    atf.run_command(
        f"scontrol create reservation starttime=now duration=120 user=root nodes=ALL ReservationName={resv_name}",
        fatal=True,
    )

    yield

    atf.run_command(
        f"scontrol delete ReservationName={resv_name}",
        fatal=False,
    )


def test_resv(slurm, reservation):
    from openapi_client.models.status import Status

    resp = slurm.slurm_v0039_get_reservation(resv_name)
    assert len(resp.warnings) == 0
    assert len(resp.errors) == 0
    assert resp.reservations
    for resv in resp.reservations:
        assert resv.name == resv_name

    resp = slurm.slurm_v0039_get_reservations()
    assert len(resp.warnings) == 0
    assert len(resp.errors) == 0
    assert resp.reservations


def test_partitions(slurm):
    from openapi_client.models.status import Status

    resp = slurm.slurm_v0039_get_partition(partition_name)
    assert len(resp.warnings) == 0
    assert len(resp.errors) == 0
    assert resp.partitions
    for part in resp.partitions:
        assert part.name == partition_name

    resp = slurm.slurm_v0039_get_partitions()
    assert len(resp.warnings) == 0
    assert len(resp.errors) == 0
    assert resp.partitions


def test_nodes(slurm):
    from openapi_client.models.status import Status
    from openapi_client.models.v0039_update_node_msg import V0039UpdateNodeMsg

    node_name = None
    resp = slurm.slurm_v0039_get_nodes()
    assert len(resp.warnings) == 0
    assert len(resp.errors) == 0
    assert resp.nodes
    for node in resp.nodes:
        if "IDLE" in node.state:
            node_name = node.name
            comment = node.comment
            extra = node.extra
            feat = node.features
            actfeat = node.active_features
            state = node.state
            reason = node.reason
            reasonuid = node.reason_set_by_user
            break

        # skip if no idle nodes are found
    if node_name is None:
        return

    node = V0039UpdateNodeMsg(
        comment="test node comment",
        extra="test node extra",
        features=[
            "taco1",
            "taco2",
            "taco3",
        ],
        features_act=[
            "taco1",
            "taco3",
        ],
        state=["DRAIN"],
        reason="testing and tacos are the reason",
        reason_uid=local_user_name,
    )

    resp = slurm.slurm_v0039_update_node(node_name, node)
    assert len(resp.warnings) == 0
    assert len(resp.errors) == 0

    resp = slurm.slurm_v0039_get_node(node_name)
    assert len(resp.warnings) == 0
    assert len(resp.errors) == 0
    assert resp.nodes
    for node in resp.nodes:
        assert node.name == node_name
        assert node.comment == "test node comment"
        assert node.extra == "test node extra"
        assert "DRAIN" in node.state
        assert node.reason == "testing and tacos are the reason"
        assert node.reason_set_by_user == local_user_name

    ncomment = "test comment comment 2"
    node = V0039UpdateNodeMsg(
        comment=ncomment,
        extra=extra,
        features=feat,
        features_act=actfeat,
        state=["RESUME"],
        reason=reason,
        reason_uid=reasonuid,
    )

    resp = slurm.slurm_v0039_update_node(node_name, node)
    assert len(resp.warnings) == 0
    assert len(resp.errors) == 0

    resp = slurm.slurm_v0039_get_node(node_name)
    assert len(resp.warnings) == 0
    assert len(resp.errors) == 0
    assert resp.nodes
    for node in resp.nodes:
        assert node.name == node_name
        # assert node.comment == ncomment
        # assert node.extra == extra


def test_ping(slurm):
    from openapi_client.models.status import Status

    resp = slurm.slurm_v0039_ping()
    assert len(resp.warnings) == 0
    assert len(resp.errors) == 0


def test_diag(slurm):
    from openapi_client.models.status import Status

    resp = slurm.slurm_v0039_diag()
    assert len(resp.warnings) == 0
    assert len(resp.errors) == 0
    assert resp.statistics


def test_licenses(slurm):
    from openapi_client.models.status import Status

    resp = slurm.slurm_v0039_slurmctld_get_licenses()
    assert len(resp.errors) == 0
