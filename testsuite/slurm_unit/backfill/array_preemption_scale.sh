#!/usr/bin/env bash
set -euo pipefail

image=${SLURM_TEST_IMAGE:?Set SLURM_TEST_IMAGE to a local image containing the patched Slurm installation and Python 3}
output_dir=${1:?Usage: SLURM_TEST_IMAGE=IMAGE bash array_preemption_scale.sh OUTPUT_DIRECTORY}
mkdir -p "$output_dir"
output_dir=$(cd "$output_dir" && pwd)
if [[ -e "$output_dir/controller.log" ]]; then
    printf 'Use a fresh results directory; controller.log already exists.\n' >&2
    exit 1
fi
name="slurm-array-scale-$$"
cleanup() {
    docker rm -f "$name" >/dev/null 2>&1 || true
    docker network rm "$name" >/dev/null 2>&1 || true
}
trap cleanup EXIT
trap 'exit 130' INT
trap 'exit 143' TERM
printf 'Starting a local 140-node test (10 CPUs, 6 GiB, 32768 PID limit).\n'
printf 'Setup may take three minutes; the preemption wave includes real 300s grace.\n'
docker network create --internal "$name" >/dev/null
docker run --rm -i --platform linux/amd64 --hostname localhost \
    --name "$name" --network "$name" --cpus 10 --memory 6g --pids-limit 32768 \
    --mount "type=bind,source=$output_dir,target=/results" \
    --entrypoint python3 "$image" -u - <<'PY'
import datetime
import json
import os
import pathlib
import re
import statistics
import subprocess
import time

root = pathlib.Path('/tmp/array-scale')
results = pathlib.Path('/results')
(root / 'state').mkdir(parents=True)
(results / 'nodes').mkdir(exist_ok=True)
os.environ['SLURM_CONF'] = str(root / 'slurm.conf')
os.environ['TZ'] = 'UTC'
config = '''ClusterName=array-scale
SlurmctldHost=localhost
SlurmctldPort=26817
SlurmdPort=26818
SlurmUser=root
SlurmdUser=root
StateSaveLocation=/tmp/array-scale/state
SlurmdSpoolDir=/tmp/array-scale/spool-%n
SlurmctldPidFile=/tmp/array-scale/controller.pid
SlurmdPidFile=/tmp/array-scale/slurmd-%n.pid
SlurmctldLogFile=/results/controller.log
SlurmdLogFile=/results/nodes/%n.log
AuthType=auth/none
CredType=cred/none
SelectType=select/cons_tres
SelectTypeParameters=CR_Core
SchedulerType=sched/backfill
SchedulerParameters=nohold_on_prolog_fail,max_rpc_cnt=256,enable_user_top,bf_continue,bf_max_job_array_launch=100
SlurmctldParameters=conmgr_max_connections=512,conmgr_threads=32
SlurmdParameters=config_overrides
ProctrackType=proctrack/linuxproc
TaskPlugin=task/none
JobAcctGatherType=jobacct_gather/none
PreemptType=preempt/partition_prio
PreemptMode=REQUEUE
ReturnToService=2
SlurmctldDebug=info
MaxArraySize=10000
MaxJobCount=10000
NodeName=node[001-140] NodeAddr=127.0.0.1 NodeHostname=node[001-140] Port=27001-27140 CPUs=4 RealMemory=4096 State=UNKNOWN
PartitionName=low Nodes=ALL Default=YES MaxTime=INFINITE State=UP PriorityTier=1 GraceTime=300
PartitionName=high Nodes=ALL MaxTime=INFINITE State=UP PriorityTier=2
'''
(root / 'slurm.conf').write_text(config)
(results / 'slurm.conf').write_text(config)
(root / 'cgroup.conf').write_text('CgroupPlugin=disabled\n')


def command(*arguments):
    return subprocess.check_output(arguments, text=True, timeout=30).strip()


def jobs():
    output = command('scontrol', 'show', 'job', '-o')
    return [dict(re.findall(r'(\w+)=(\S*)', line)) for line in output.splitlines() if line.startswith('JobId=')]


def belongs(records, array_id):
    return [record for record in records if record.get('ArrayJobId') == array_id]


def running(records):
    return [record for record in records if record.get('JobState') in {'RUNNING', 'SIGNALING'}]


def preparing(records):
    return [record for record in records if record.get('JobState') == 'PENDING' and record.get('Reason') in {'Preempting', 'PreemptionPlanned', 'PreemptionCleanup'}]


def wait_for(predicate, seconds=180):
    deadline = time.monotonic() + seconds
    while True:
        try:
            records = jobs()
            if predicate(records):
                return records
        except subprocess.CalledProcessError:
            pass
        assert time.monotonic() < deadline, 'setup timeout'
        time.sleep(2)


def ping():
    started = time.monotonic()
    assert 'UP' in command('scontrol', 'ping')
    return time.monotonic() - started


def distribution(values):
    ordered = sorted(values)
    if not ordered:
        return {'samples': 0}
    return {'samples': len(values), 'median_ms': round(statistics.median(values) * 1000, 2), 'p95_ms': round(ordered[int((len(ordered) - 1) * .95)] * 1000, 2), 'max_ms': round(max(values) * 1000, 2)}


subprocess.run(['slurmctld', '-f', os.environ['SLURM_CONF']], check=True)
with (results / 'slurmd-startup.log').open('w') as startup:
    for index in range(1, 141):
        subprocess.run(['slurmd', '-N', f'node{index:03}', '-f', os.environ['SLURM_CONF']], check=True, stdout=startup, stderr=startup)

deadline = time.monotonic() + 90
while True:
    nodes = command('sinfo', '-N', '-h', '-p', 'low', '-o', '%N|%T').splitlines()
    if len(nodes) == 140 and all(node.endswith('|idle') for node in nodes):
        break
    assert time.monotonic() < deadline, nodes
    time.sleep(2)

array_a = command('sbatch', '--parsable', '-p', 'high', '--cpus-per-task=2', '--array=0-120%20', '--output=/tmp/scale-a-%a.out', '--wrap', 'if [ "$SLURM_ARRAY_TASK_ID" -lt 20 ]; then sleep 1200; else sleep 420; fi')
wait_for(lambda records: len(running(belongs(records, array_a))) == 20)
victims = command('sbatch', '--parsable', '-p', 'low', '--cpus-per-task=2', '--array=0-259', '--output=/tmp/scale-v-%a.out', '--wrap', 'trap "" TERM; sleep 1800 & wait')
initial = wait_for(lambda records: len(running(belongs(records, victims))) == 260)
victim_nodes = {record['JobId']: record['NodeList'] for record in belongs(initial, victims)}
assert len(victim_nodes) == 260
baseline = [ping() for _ in range(25)]
(results / 'sdiag-before.txt').write_text(command('sdiag') + '\n')
command('scontrol', 'update', f'JobId={array_a}', 'ArrayTaskThrottle=121')
array_b = command('sbatch', '--parsable', '-p', 'high', '--cpus-per-task=2', '--array=0-29%25', '--output=/tmp/scale-b-%a.out', '--wrap', 'sleep 420')
print(json.dumps({'event': 'wave submitted', 'array_a': array_a, 'array_b': array_b, 'victims': victims, 'baseline_ping': distribution(baseline)}), flush=True)

started = time.monotonic()
wave_pings = []
high_pings = []
cleanup_pings = []
plans = {}
stopped = {}
starts_a = {}
max_plans_a = max_plans_b = max_combined = 0
cap_reached_at = None
cap_marker_seen = False
cleanup_seen = False
last_print = -30
samples = []
open_pattern = re.compile(r'\[([^]]+)\].*JobId=(\d+)_([0-9]+)\((\d+)\) launch transaction opened: (\d+) nodes pinned, (\d+) planned preemptions')

while True:
    elapsed = time.monotonic() - started
    records = jobs()
    (results / 'latest-jobs.json').write_text(json.dumps(records, indent=2) + '\n')
    group_a = belongs(records, array_a)
    group_b = belongs(records, array_b)
    prepared_a, prepared_b = preparing(group_a), preparing(group_b)
    running_a, running_b = running(group_a), running(group_b)
    active = prepared_a + prepared_b
    assert len(prepared_a) <= 100 and len(prepared_b) <= 100
    assert len(prepared_a) + len(running_a) <= 121
    assert len(prepared_b) + len(running_b) <= 25
    node_lists = [record.get('SchedNodeList', '') for record in active]
    assert all(re.fullmatch(r'node\d{3}', node) for node in node_lists), node_lists
    assert len(set(node_lists)) == len(node_lists), 'overlapping plan nodes'
    max_plans_a = max(max_plans_a, len(prepared_a))
    max_plans_b = max(max_plans_b, len(prepared_b))
    max_combined = max(max_combined, len(active))
    cap_marker_seen |= any(record.get('Reason') == 'ArrayPreemptionLimit' for record in group_a)
    if cap_reached_at is None and len(prepared_a) == 100 and len(prepared_b) == 25:
        cap_reached_at = elapsed
        assert len(running_a) == 20 and not running_b

    text = (results / 'controller.log').read_text()
    for stamp, array_id, task_id, job_id, nodes, blockers in open_pattern.findall(text):
        if array_id not in {array_a, array_b}:
            continue
        assert nodes == '1' and blockers == '1', 'non-minimal victim plan'
        epoch = datetime.datetime.fromisoformat(stamp).replace(tzinfo=datetime.timezone.utc).timestamp()
        plans.setdefault(job_id, {'array': array_id, 'task': task_id, 'epoch': epoch})
    for record in active:
        if record['JobId'] in plans:
            plans[record['JobId']]['node'] = record['SchedNodeList']

    now = time.time()
    victim_group = belongs(records, victims)
    completing = [record for record in victim_group if record.get('JobState') == 'COMPLETING']
    cleanup_seen |= bool(completing)
    for record in victim_group:
        job_id = record['JobId']
        if job_id in stopped or running([record]):
            continue
        relevant = [plan for plan in plans.values() if plan.get('node') == victim_nodes.get(job_id)]
        assert relevant, f'victim {job_id} stopped outside a committed plan'
        earliest = min(plan['epoch'] for plan in relevant)
        assert int(now) - int(earliest) >= 300, f'victim {job_id} on {victim_nodes.get(job_id)} stopped before grace: state={record.get("JobState")}, observed={now}, plan={earliest}'
        stopped[job_id] = now
    force_pattern = re.compile(r'\[([^]]+)\] preempted JobId=\d+_\d+\((\d+)\) has been requeued')
    for stamp, job_id in force_pattern.findall(text):
        relevant = [plan for plan in plans.values() if plan.get('node') == victim_nodes.get(job_id)]
        assert relevant, f'forced victim {job_id} has no plan'
        enforced = datetime.datetime.fromisoformat(stamp).replace(tzinfo=datetime.timezone.utc).timestamp()
        earliest = min(plan['epoch'] for plan in relevant)
        assert int(enforced) - int(earliest) >= 300, f'forced victim {job_id} before grace: enforcement={enforced}, plan={earliest}'
    assert len(stopped) <= len(plans), 'more victims stopped than planned'
    for record in running_a:
        if int(record['ArrayTaskId']) >= 20:
            starts_a.setdefault(record['JobId'], record.get('StartTime'))
            if record['JobId'] in plans and 'node' in plans[record['JobId']]:
                assert record['NodeList'] == plans[record['JobId']]['node'], 'owner lost its original node'
    for record in running_b:
        if record['JobId'] in plans and 'node' in plans[record['JobId']]:
            assert record['NodeList'] == plans[record['JobId']]['node'], 'owner lost its original node'

    latency = ping()
    wave_pings.append(latency)
    if len(active) >= 100:
        high_pings.append(latency)
    if completing:
        cleanup_pings.append(latency)
    sample = {'elapsed_s': round(elapsed, 2), 'a_running': len(running_a), 'a_plans': len(prepared_a), 'b_running': len(running_b), 'b_plans': len(prepared_b), 'victims_stopped': len(stopped), 'victims_completing': len(completing), 'ping_ms': round(latency * 1000, 2)}
    samples.append(sample)
    if elapsed - last_print >= 15:
        (results / 'samples.json').write_text(json.dumps(samples, indent=2) + '\n')
        (results / 'plans.json').write_text(json.dumps(plans, indent=2) + '\n')
        print(json.dumps(sample), flush=True)
        last_print = elapsed
    if len(running_a) >= 120 and len(running_b) == 25:
        break
    assert elapsed < 900, 'allocation throughput test timed out'
    time.sleep(2)

assert cap_reached_at is not None and max_combined > 100
assert max_plans_a == 100 and max_plans_b == 25
assert cap_marker_seen and cleanup_seen
assert len(starts_a) >= 100
assert len(stopped) == len(starts_a) + 25
assert len(running(victim_group)) == 260 - len(stopped)
start_epochs = [datetime.datetime.fromisoformat(stamp).replace(tzinfo=datetime.timezone.utc).timestamp() for stamp in starts_a.values()]
first_plan = min(plan['epoch'] for plan in plans.values() if plan['array'] == array_a)
start_window = max(start_epochs) - min(start_epochs)
summary = {
    'status': 'PASS', 'nodes': 140, 'array_cap': 100,
    'array_a_length': 121, 'array_a_limit': 121, 'array_b_length': 30, 'array_b_limit': 25,
    'max_preparing_a': max_plans_a, 'max_preparing_b': max_plans_b, 'max_combined_preparing': max_combined,
    'running_a_at_finish': len(running_a), 'running_b_at_finish': len(running_b),
    'cap_reached_seconds_after_submission': round(cap_reached_at, 2),
    'new_a_allocations': len(starts_a), 'first_plan_to_100_allocations_seconds': round(max(start_epochs) - first_plan, 2),
    'allocation_window_seconds': start_window, 'allocations_per_minute_in_window': round(len(starts_a) * 60 / max(start_window, 1), 2),
    'victim_plans': len(plans), 'victims_stopped': len(stopped), 'victims_still_running': len(running(victim_group)),
    'baseline_ping': distribution(baseline), 'wave_ping': distribution(wave_pings),
    'at_least_100_plans_ping': distribution(high_pings), 'cleanup_ping': distribution(cleanup_pings),
    'peak_memory_bytes': int(pathlib.Path('/sys/fs/cgroup/memory.peak').read_text()),
    'limitations': 'Local sleeping CPU jobs, partition-priority preemption, synthetic nodes sharing one host; not a production QOS/GPU or 20-versus-100 benchmark.'
}
(results / 'summary.json').write_text(json.dumps(summary, indent=2) + '\n')
(results / 'samples.json').write_text(json.dumps(samples, indent=2) + '\n')
(results / 'sdiag-after.txt').write_text(command('sdiag') + '\n')
print(json.dumps(summary), flush=True)
PY
