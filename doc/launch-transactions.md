# Transactional preemptive job launches

This change was developed and validated on Slurm 25.05.3 (`slurm-25-05-3-1`).
This branch applies the same source changes to upstream `slurm-25.05`.
It coordinates preemption and resource allocation for ordinary jobs,
heterogeneous jobs, and job arrays, without requiring a container-vendor patch
stack. The cluster validation applies to 25.05.3.

## Problem

A scheduler can choose nodes and signal lower-priority jobs, then wait across
several scheduling cycles for grace periods and resource cleanup. Without
controller-wide ownership, another job can acquire the newly released capacity
before the job that initiated preemption starts. Replanning can then disrupt
another group of victims without making forward progress.

Heterogeneous jobs add a second constraint: every component's exact allocation
must be ready before the first component starts. Array tasks need independent
plans so that waiting for one task's preemption does not serialize preparation
for the rest of the array.

## Behavior

1. Backfill commits one exact node bitmap and a sufficient victim list before
   initiating preemption. Ordinary submit-time and main-scheduler paths hand
   preemption decisions to backfill while transactions are enabled.
2. A controller-wide node fence prevents other jobs and new reservations from
   consuming committed nodes. The owner can retry its exact allocation.
3. Victims follow their configured preemption mode and grace period. Early
   voluntary exits may make resources available sooner; node or GRES cleanup
   may delay the allocation after grace expires.
4. Transient start failures retain the same plan. Invalid plans that have
   already signaled victims finish cleanup before releasing ownership and
   entering a bounded replan cooldown.
5. A heterogeneous job owns the union of its component plans. An exact run-now
   readiness test must succeed for every pending component before the first
   allocation. If a component has started, remaining components retain their
   plans; running work is not rolled back automatically.
6. Each prepared array task receives its own job record, node plan, and reserved
   concurrency slot. Running tasks plus preparation slots count against `%N`.
   Failure or cooldown of one task does not block the shared array record.

Victim selection uses the normal run-now select simulation on the pinned node
bitmap. For example, a four-CPU request on a node occupied by six-CPU jobs can
select one sufficient victim instead of preempting every resident job. Victims
remain indivisible: preempting a multi-node job may release additional capacity
outside the owner's plan.

## Configuration

The following options are members of `SchedulerParameters`:

| Parameter | Default | Meaning |
| --- | --- | --- |
| `bf_job_commit_timeout` | 1800 seconds | Safety deadline for ordinary launch plans. Zero disables new ordinary transactions. |
| `bf_hetjob_commit_timeout` | 1800 seconds | Safety deadline before a heterogeneous job has started any component. Zero disables new heterogeneous transactions. |
| `bf_launch_replan_delay` | 300 seconds | Cooldown after cleanup of a failed victim plan. |
| `bf_launch_max_replans` | 1 | Maximum replacement victim plans after a failed attempt. Zero permits no replacement plan. |
| `bf_max_job_array_launch` | 20 | Outstanding plans per array, including grace and cleanup; accepted range is 0–1000. |

The failure cooldown is separate from a victim's `GraceTime`. A successful plan
does not enter cooldown. A safety timeout after victim signaling enters cleanup;
it does not immediately release nodes or authorize another victim wave. A
partially started heterogeneous job has no automatic timeout.

Setting the array cap to zero stops new array preemption plans but still permits
idle-resource starts and retries of existing plans. Reducing a cap does not
discard existing commitments. Multiple arrays may collectively exceed the
per-array cap. Lowering `%N` prevents further allocations until running tasks
leave enough room; it does not terminate already-running tasks.

## Observability and recovery

`scontrol show job` exposes the committed bitmap in `SchedNodeList` and detailed
status under the `LaunchTxn:` prefix in `SystemComment`. Pending reasons include
`PreemptionPlanned`, `Preempting`, and `HetjobPartialLaunch`. Comments distinguish
victim grace, resource cleanup, failed-plan cleanup, cooldown, and exhausted
retries. Active blocker nodes are reported separately from the full node plan.

Disabling transactions prevents new commitments and lets existing signaled
plans drain through cleanup. An already partially launched heterogeneous job
remains committed until it completes, recovers, or is explicitly cancelled.
An exhausted replan budget leaves a job pending for operator recovery; cancel
and resubmit after addressing the cause to authorize a fresh attempt.

## Regression tests

Configure and build Slurm on Linux using the normal build prerequisites. Run
the focused harness from the source tree:

```bash
./configure --prefix=/tmp/slurm-launch-test
make -j4
bash testsuite/slurm_unit/backfill/test_launch_transactions.sh
```

For an out-of-tree build, set `SLURM_BUILD_ROOT` to the configured build
directory. The harness uses a C compiler, GNU `objcopy`, and the built
`libslurmfull`. It compiles the actual controller helpers and backfill code with
`NDEBUG` and strict declaration, pointer-type, and unused-variable checks.

The five regression groups cover:

- Parallel array plans, victim grace, cleanup, per-array caps, start and cancel.
- `%N` accounting, owner exemptions, limit reduction, the last task, and zero cap.
- Disjoint ownership, cleanup-slot retention, bounded retry isolation and teardown.
- Ordinary-job routing and preservation of the heterogeneous-job path.
- Shared victims signaled once, independent cancellation, and shutdown cleanup.

Time, job lookup, record splitting, and victim signaling are mocked. These tests
do not establish end-to-end heterogeneous launch correctness or real GPU cleanup
behavior.

### Optional local scale test

`testsuite/slurm_unit/backfill/array_preemption_scale.sh` runs one controller and
140 CPU-only `slurmd` processes in an isolated Docker container. Supply an image
containing the complete patched Slurm installation and Python 3, with Slurm's
executables on `PATH`:

```bash
SLURM_TEST_IMAGE=slurm-launch-test:local \
  bash testsuite/slurm_unit/backfill/array_preemption_scale.sh /tmp/slurm-scale-results
```

Use a fresh output directory and a Docker engine with at least 8 GiB available.
The container is limited to 10 CPUs, 6 GiB, and 32,768 processes/threads. It uses
an internal network, publishes no ports, mounts only the output directory, and
removes its container and network on exit. Setup can take three minutes; the
wave allows 15 minutes including a real 300-second victim grace period.

The test prepares up to 100 plans for one array and 25 for another. It checks
array concurrency, disjoint node ownership, sufficient victim selection,
grace enforcement, and starts on the originally committed nodes. It records
CLI latency and allocation timing, plus logs and scheduler diagnostics. This
is a synthetic CPU/partition-priority test, not a QOS/GPU performance benchmark.

## Limitations and further validation

- Ownership and array preparation counters are controller-local memory. Restart
  loses the plans; already-sent victim signals cannot be reversed.
- Ownership is whole-node even for smaller CPU or GPU requests. It does not
  persist a select plugin's full per-GRES proof or solve license-only conflicts.
- Heterogeneous component allocations remain sequential after the readiness
  barrier. A failure between allocations can leave a partial launch requiring
  operator recovery.
- Before deployment, exercise real QOS/GPU preemption, heterogeneous component
  cleanup and partial launch, cancellation, node failure, changed reservations,
  reconfiguration/failover, and RPC responsiveness under sustained load.
- Rebuild the controller and its plugins together: the internal `job_record_t`
  layout changes. Do not mix the new controller with old controller plugins.

This change has not been ported to later Slurm release series or the current
upstream development branch (`master`).
