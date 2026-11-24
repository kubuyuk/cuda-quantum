# Code Reuse Guidelines

The remote-QPU layer in `runtime/` has one small shared helper for each job
that every vendor integration needs. When a vendor helper needs one of these
jobs, use the shared helper instead of writing a local copy. Local copies drift
apart: they gain different error messages, different handling of empty values
and different edge cases, and bugs then have to be fixed in several places.

## Which helper to use

| You need to | Use | Where |
| --- | --- | --- |
| Read an environment variable, optionally required | `cudaq::getEnvVar` | `runtime/common/ConfigUtils.h` |
| Read a key from a backend config map with a default | `cudaq::getValueOrDefault` | `runtime/common/ConfigUtils.h` |
| Read an API key or token from a `~/.<vendor>_config` file | `cudaq::parseCredentialFile` | `runtime/common/CredentialFile.h` |
| Poll a job until it finishes, or retry a fetch | `pollUntilDone`, `retryWithDelay` | `runtime/common/PollingPolicy.h` |
| Parse `--key=value` options in a realtime bridge | `for_each_arg` and helpers | `realtime/include/cudaq/realtime/daemon/bridge/bridge_args.h` |

If the shared helper almost fits, extend it with a parameter, as
`getEnvVar` does with its optional error message, instead of copying it.

## What is checked

`scripts/pre-commit/check_shared_helpers.sh` runs as the `shared-helpers`
pre-commit hook. It flags these patterns:

| Rule | Flags | Fix |
| --- | --- | --- |
| `local-env-helper` | A new local definition of `getEnvVar` or `getValueOrDefault` | Use `ConfigUtils.h` |
| `raw-getenv` | `getenv(` in a REST server helper | Use `cudaq::getEnvVar` |
| `adhoc-poll-sleep` | `sleep_for(` in a REST server helper or `Future.cpp` | Use `PollingPolicy.h` |
| `executor-downcast` | `dynamic_cast<...ServerHelper *>` in an executor | Get a typed helper from the factory |

The check is a ratchet. Violations that already existed are recorded in
`.github/pre-commit/shared_helpers_baseline.txt`, and only an increase in a
file's count fails. When you remove a violation, lower or delete its line in
the baseline in the same change. The script prints a note when a baseline entry
can be lowered. To regenerate the whole baseline after a large cleanup:

```bash
bash scripts/pre-commit/check_shared_helpers.sh --update-baseline
```

To check the whole tree, not only changed files:

```bash
bash scripts/pre-commit/check_shared_helpers.sh --all
```

## When a local copy is justified

Some vendors really do differ. A helper that must treat an empty environment
variable as unset, or must search several locations in a fixed order, may need
its own logic. In that case, mark the line and say why:

```cpp
// cudaq-lint: allow(raw-getenv) HOME fallback needs the unset/empty distinction
const char *home = std::getenv("HOME");
```

The marker must be on the same line as the match. Reviewers should read the
reason. If several vendors need the same exception, that is a sign the shared
helper needs a new option.

## Changing a shared helper

The shared helpers replaced several near-identical copies, and the differences
between those copies were kept on purpose. Before changing a default, such as
an error message, a polling interval or how an empty value is treated, check
which vendors depend on the current behavior, and say in the change what
differs for each of them.
