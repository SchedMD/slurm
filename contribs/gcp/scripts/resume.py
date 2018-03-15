#!/usr/bin/python

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
REGION       = '@REGION@'
MACHINE_TYPE = '@MACHINE_TYPE@'
PREEMPTIBLE  = @PREEMPTIBLE@
EXTERNAL_IP  = @EXTERNAL_COMPUTE_IPS@

DISK_SIZE_GB = '@DISK_SIZE_GB@'
DISK_TYPE    = '@DISK_TYPE@'

NETWORK_TYPE = 'subnetwork'
NETWORK      = "projects/%s/regions/%s/subnetworks/slurm-subnet" % (PROJECT, REGION)

SCONTROL     = '/apps/slurm/current/bin/scontrol'
LOGFILE      = '/apps/slurm/log/resume.log'

# [START create_instance]
def create_instance(compute, project, zone, instance_type, instance_name):
  # Get the latest CentOS 7image.
  image_response = compute.images().getFromFamily(
    project='centos-cloud', family='centos-7').execute()
  source_disk_image = image_response['selfLink']

  # Configure the machine
  machine_type = "zones/%s/machineTypes/%s" % (zone, instance_type)
  disk_type = "projects/%s/zones/%s/diskTypes/%s" % (PROJECT, ZONE, DISK_TYPE)
  startup_script = open('/apps/slurm/scripts/startup-script.py', 'r').read()

  config = {
    'name': instance_name,
    'machineType': machine_type,

    # Specify the boot disk and the image to use as a source.
    'disks': [
      {
        'boot': True,
        'autoDelete': True,
        'initializeParams': {
          'sourceImage': source_disk_image,
          'diskType': disk_type,
          'diskSizeGb': DISK_SIZE_GB
        }
      }
    ],

    # Specify a network interface
    'networkInterfaces': [{
      NETWORK_TYPE : NETWORK,
    }],

    # Allow the instance to access cloud storage and logging.
    'serviceAccounts': [{
      'email': 'default',
      'scopes': [
        'https://www.googleapis.com/auth/logging.write',
        'https://www.googleapis.com/auth/compute'
      ]
    }],

    'tags': {'items': ['compute'] },

    # Metadata is readable from the instance and allows you to
    # pass configuration from deployment scripts to instances.
    'metadata': {
      'items': [{
        # Startup script is automatically executed by the
        # instance upon startup.
        'key': 'startup-script',
        'value': startup_script
      }]
    }
  }

  if PREEMPTIBLE:
      config['scheduling'] = {
              "preemptible": True,
              "onHostMaintenance": "TERMINATE",
              "automaticRestart": False
              },

  if EXTERNAL_IP:
      config['networkInterfaces'][0]['accessConfigs'] = [
                {'type': 'ONE_TO_ONE_NAT', 'name': 'External NAT'}
              ]


  return compute.instances().insert(
    project=project,
    zone=zone,
    body=config).execute()
# [END create_instance]

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
  logging.info("Bursting out:" + short_node_list)
  compute = googleapiclient.discovery.build('compute', 'v1',
                                            cache_discovery=False)

  # Get node list
  show_hostname_cmd = "%s show hostname %s" % (SCONTROL, short_node_list)
  node_list = subprocess.check_output(shlex.split(show_hostname_cmd))

  operations = {}
  for node_name in node_list.splitlines():
    try:
      instance = compute.instances().get(
                project=PROJECT, zone=ZONE, instance=node_name,
                fields='name,status').execute()
      logging.info("node %s already exists in state %s" % (node_name,
          instance['status']))
      operations[node_name] = compute.instances().start(project=PROJECT,
              zone=ZONE, instance=node_name).execute()
      logging.info("Sent start instance for " + node_name)
    except:
      try:
        operations[node_name] = create_instance(compute, PROJECT, ZONE,
                                                MACHINE_TYPE, node_name)
        logging.info("Sent create instance for " + node_name)
      except Exception, e:
        logging.exception("Error in creation of %s (%s)" % (node_name, str(e)))
        cmd = "%s update node=%s state=down reason='%s'" % \
              (SCONTROL, node_name, str(e))
        subprocess.call(shlex.split(cmd))

  for node_name in operations:
    try:
      operation = operations[node_name]
      # Do this after the instances have been initialized and then wait for all
      # operations to finish. Then updates their addrs.
      wait_for_operation(compute, PROJECT, ZONE, operation['name'])

      instance_networks = compute.instances().get(
                project=PROJECT, zone=ZONE, instance=node_name,
                fields='networkInterfaces(name,network,networkIP,subnetwork)'
                ).execute()
      instance_ip = instance_networks['networkInterfaces'][0]['networkIP']

      node_update_cmd = "%s update node=%s nodeaddr=%s" % \
              (SCONTROL, node_name, instance_ip)
      subprocess.call(shlex.split(node_update_cmd))

      logging.info("Instance " + node_name + " is now up")
    except Exception, e:
      logging.exception("Error in adding %s to slurm (%s)" % (node_name, str(e)))

# [END main]


if __name__ == '__main__':
  parser = argparse.ArgumentParser(
    description=__doc__,
    formatter_class=argparse.RawDescriptionHelpFormatter)
  parser.add_argument('nodes', help='Nodes to burst')

  args = parser.parse_args()
  logging.basicConfig(filename=LOGFILE,
                      format='%(asctime)s %(name)s %(levelname)s: %(message)s',
                      level=logging.DEBUG)

  main(args.nodes)
