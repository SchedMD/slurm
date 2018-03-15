#!/usr/bin/env python

# Copyright 2017 SchedMD LLC.
# Modified for use with the Slurm Resource Manager.
#
# Copyright 2015 Google Inc. All rights reserved.
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

import argparse
import logging
import shlex
import subprocess
import time

import googleapiclient.discovery

PROJECT      = '@PROJECT@'
ZONE         = '@ZONE@'
SCONTROL     = '/apps/slurm/current/bin/scontrol'
LOGFILE      = '/apps/slurm/log/suspend.log'

# [START delete_instance]
def delete_instance(compute, project, zone, node_name):
  return compute.instances().delete(
    project=project,
    zone=zone,
    instance=node_name).execute()
# [END delete_instance]

# [START wait_for_operation]
def wait_for_operation(compute, project, zone, operation):
    print('Waiting for operation to finish...')
    while True:
        result = compute.zoneOperations().get(
            project=project,
            zone=zone,
            operation=operation).execute()

        if result['status'] == 'DONE':
            print("done.")
            if 'error' in result:
                raise Exception(result['error'])
            return result

        time.sleep(1)
# [END wait_for_operation]

# [START main]
def main(short_node_list):
  logging.info("Releasing nodes:" + short_node_list)
  compute = googleapiclient.discovery.build('compute', 'v1',
                                            cache_discovery=False)

  # Get node list
  show_hostname_cmd = "%s show hostname %s" % (SCONTROL, short_node_list)
  node_list = subprocess.check_output(shlex.split(show_hostname_cmd))

  operations = {}
  for node_name in node_list.splitlines():
    try:
      operations[node_name] = delete_instance(compute, PROJECT, ZONE, node_name)
    except Exception, e:
      logging.exception("error during release of %s (%s)" % (node_name, str(e)))

  for node_name in operations:
    operation = operations[node_name]
    try:
      # Do we care if they have completely deleted? Waiting will cause it to
      # wait for each to be completely deleted befotre the next delete is made.
      # Could issue all deletes and then wait for the deletes to finish..
      wait_for_operation(compute, PROJECT, ZONE, operation['name'])
      logging.info("deleted instance " + node_name)
    except Exception, e:
      logging.exception("error deleting %s (%s)" % (node_name, str(e)))

  logging.info("done deleting instances")

# [END main]


if __name__ == '__main__':
  parser = argparse.ArgumentParser(
    description=__doc__,
    formatter_class=argparse.RawDescriptionHelpFormatter)
  parser.add_argument('nodes', help='Nodes to release')

  args = parser.parse_args()
  logging.basicConfig(filename=LOGFILE,
                      format='%(asctime)s %(name)s %(levelname)s: %(message)s',
                      level=logging.DEBUG)

  main(args.nodes)
