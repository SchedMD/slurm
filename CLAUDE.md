# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Build System

Slurm uses autotools (autoconf/automake). The standard build workflow:

```bash
./configure [options]   # Configure for your system
make                    # Build all components
make check              # Run self-tests
make install            # Install
make distclean          # Remove all generated files for a clean rebuild
```

Common configure flags:
- `--enable-debug` - Enable debug symbols and assertions
- `--enable-developer` - Enable developer checks
- `--disable-optimizations` - Disable compiler optimizations (useful with debug)
- `--with-mysql_config` - Enable MySQL/MariaDB support (required for slurmdbd)
- `--with-pam`, `--with-lua`, `--with-hwloc`, `--with-nvml`, `--with-pmix` - Optional feature support
- `--disable-sview`, `--disable-slurmrestd` - Disable optional components

**Important**: Edit `Makefile.am` files (not `Makefile.in`, which is auto-generated).

## Running Tests

The unified test runner is `testsuite/run-tests`. Slurm must be built, installed, and running before tests execute.

```bash
# Run unit tests only (Check framework, no running cluster required)
./testsuite/run-tests -i unit

# Run expect/regression tests (Tcl/Expect, requires running Slurm)
./testsuite/run-tests -i expect

# Run Python tests (pytest-based, requires running Slurm)
./testsuite/run-tests -i python

# Run a specific test by regex
./testsuite/run-tests -i 'expect/test1.1$'
```

Copy `testsuite/testsuite.conf.sample` to `testsuite/testsuite.conf` before running tests.

Unit tests live in `testsuite/slurm_unit/` (subdirs: `common/`, `backfill/`, `topology/`).

## Code Style

- **C code**: clang-format with the `.clang-format` config (80-char column limit, 8-space indentation, K&R brace style)
- **Python**: Black (max line length 89) + Flake8
- **Shell**: shfmt + shellcheck (severity=warning)

Pre-commit hooks enforce all formatting:
```bash
pre-commit install          # Install hooks
pre-commit run --all-files  # Run manually
```

## Commit Requirements

- All commits must be signed off: `git commit -s` (DCO requirement)
- Commit title: max 72 characters
- Commit body lines: max 76 characters
- Include a `Changelog:` trailer line describing the change
- Each patch must compile independently (to support `git bisect`)
- Submit formatting-only changes separately from functional changes

## Architecture Overview

Slurm has three core daemons and a REST API server:

- **`slurmctld`** (`src/slurmctld/`) — Central controller/scheduler. Manages jobs, nodes, partitions, reservations. The authoritative state for the cluster.
- **`slurmd`** (`src/slurmd/`) — Compute node agent. Launches and manages job steps on worker nodes.
- **`slurmdbd`** (`src/slurmdbd/`) — Accounting database daemon. Interfaces with MySQL/MariaDB via `src/database/`.
- **`slurmrestd`** (`src/slurmrestd/`) — REST API server. Uses `src/plugins/data_parser/` for serialization.

### Key Source Directories

- `src/common/` — ~70 shared utility modules: data structures (`list.c`, `xhash.c`), networking, logging, configuration parsing, RPC protocol, string utilities.
- `src/api/` — Public C API used by CLI tools to communicate with `slurmctld`/`slurmdbd`.
- `src/plugins/` — 42 plugin categories. Each category has a defined API that slurmctld/slurmd calls at runtime via dlopen. Key categories:
  - `select/` — Node selection algorithms (cons_tres, linear)
  - `sched/` — Job scheduling (backfill, hold, etc.)
  - `accounting_storage/` — Storage backends (MySQL)
  - `gres/` — Generic resource management (GPUs, FPGAs)
  - `auth/` — Authentication backends
  - `task/` — Task binding/affinity (cgroup, affinity)
  - `topology/` — Network topology-aware scheduling
  - `data_parser/` — Serialization for REST API (JSON, YAML, URL)
- `src/conmgr/` — Asynchronous connection manager used by slurmrestd and other components.
- `src/interfaces/` — Common interfaces wrapping plugin dispatch (e.g., `select.h`, `auth.h`).
- `slurm/` — Public header files installed with the package.
- `contribs/` — Optional contributed components (PAM modules, Perl API, Lua scripts, PMI).

### Version and API

Version is defined in `META`. The API version (field `API`) is independent of the release version and governs RPC and state file compatibility.

### Plugin Architecture

Plugins implement a defined function table (e.g., `slurm_select_ops_t`). The interface layer in `src/interfaces/` loads the configured plugin at startup and dispatches calls through function pointers. When adding a new plugin type, define the ops struct and dispatch wrappers in `src/interfaces/`.

### Inter-daemon Communication

All daemons communicate via Slurm's internal RPC protocol defined in `src/common/slurm_protocol_*.c`. RPCs are defined in `slurm/slurm_protocol_defs.h`. The `src/common/pack.c` module handles serialization (pack/unpack).
