#!/usr/bin/env bash
set -euo pipefail

source_root=$(cd "$(dirname "$0")/../../.." && pwd)
build_root=${SLURM_BUILD_ROOT:-$source_root}
test_build=$(mktemp -d)
trap 'rm -rf "$test_build"' EXIT
compiler=${CC:-cc}
flags=(-I"$build_root" -I"$source_root" -include "$build_root/config.h"
    -O0 -g -DNDEBUG -ffunction-sections -fdata-sections
    -Werror=implicit-function-declaration -Werror=incompatible-pointer-types
    -Werror=unused-variable)

"$compiler" "${flags[@]}" -c "$source_root/src/slurmctld/job_mgr.c" -o "$test_build/job_mgr.o"
objcopy --weaken-symbol=find_job_record "$test_build/job_mgr.o"
"$compiler" "${flags[@]}" -c "$source_root/src/slurmctld/node_scheduler.c" -o "$test_build/node_scheduler.o"
"$compiler" "${flags[@]}" -c "$source_root/testsuite/slurm_unit/backfill/launch_transaction_test.c" -o "$test_build/test.o"

wraps=()
for symbol in time job_array_split schedule_job_save bitmap2node_name \
    find_part_record job_overlap_and_running slurm_job_preempt_mode slurm_job_preempt; do
    wraps+=("-Wl,--wrap=$symbol")
done
"$compiler" -Wl,--gc-sections "${wraps[@]}" "$test_build/test.o" \
    "$test_build/job_mgr.o" "$test_build/node_scheduler.o" \
    -L"$build_root/src/api/.libs" -lslurmfull -lpthread -ldl -lm \
    -Wl,-rpath,"$build_root/src/api/.libs" -o "$test_build/test"
"$test_build/test"
