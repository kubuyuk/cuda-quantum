#!/bin/bash

# ============================================================================ #
# Copyright (c) 2022 - 2026 NVIDIA Corporation & Affiliates.                   #
# All rights reserved.                                                         #
#                                                                              #
# This source code and the accompanying materials are made available under     #
# the terms of the Apache License 2.0 which accompanies this distribution.     #
# ============================================================================ #

# Self-test for check_shared_helpers.sh: each rule must fail on a new
# violation, pass on a baselined one, honour the allow marker, and ignore
# comments.

SCRIPT="$(cd "$(dirname "$0")" && pwd)/check_shared_helpers.sh"
WORK=$(mktemp -d)
trap 'rm -rf "$WORK"' EXIT
cd "$WORK" || exit 1
mkdir -p .github/pre-commit runtime/common runtime/cudaq/platform/orca \
         runtime/cudaq/platform/default/rest/helpers/v

H=runtime/cudaq/platform/default/rest/helpers/v/VServerHelper.cpp
E=runtime/cudaq/platform/orca/OrcaExecutor.cpp
F=runtime/common/Future.cpp
: > .github/pre-commit/shared_helpers_baseline.txt

FAILED=0
expect() { # description expected_exit file...
  local what=$1 want=$2; shift 2
  bash "$SCRIPT" "$@" >/dev/null 2>&1
  local got=$?
  if [ "$got" -ne "$want" ]; then
    echo "FAIL: $what (exit $got, wanted $want)" >&2; FAILED=1
  fi
}

echo 'int x;' > $H; echo 'int x;' > $E; echo 'int x;' > $F
expect "clean files pass" 0 $H $E $F

echo 'std::string getEnvVar(const std::string &k) { return ""; }' > $H
expect "local-env-helper fails" 1 $H
echo 'static inline std::string getValueOrDefault(int a) { return ""; }' > $H
expect "local-env-helper (static inline) fails" 1 $H
echo 'std::string getEnvVar(int) { return ""; } // cudaq-lint: allow(local-env-helper) x' > $H
expect "allow marker passes" 0 $H
echo '// std::string getEnvVar(int k)' > $H
expect "commented code passes" 0 $H
echo 'std::string v = cudaq::getEnvVar("A", "", true);' > $H
expect "calling the shared helper passes" 0 $H

echo 'auto *p = std::getenv("HOME");' > $H
expect "raw-getenv fails" 1 $H
echo 'auto *p = std::getenv("HOME"); // cudaq-lint: allow(raw-getenv) x' > $H
expect "raw-getenv allow marker passes" 0 $H
echo 'auto *p = std::getenv("HOME"); // cudaq-lint: allow(adhoc-poll-sleep) x' > $H
expect "allow marker for another rule does not mask" 1 $H

echo 'std::this_thread::sleep_for(std::chrono::seconds(1));' > $H
expect "adhoc-poll-sleep fails in helper" 1 $H
echo 'std::this_thread::sleep_for(std::chrono::seconds(1));' > $F
expect "adhoc-poll-sleep fails in Future.cpp" 1 $F
echo 'std::this_thread::sleep_for(std::chrono::seconds(1));' > runtime/common/Other.cpp
expect "adhoc-poll-sleep ignored outside scope" 0 runtime/common/Other.cpp
echo 'int x;' > $F

echo 'auto *h = dynamic_cast<OrcaServerHelper *>(serverHelper);' > $E
expect "executor-downcast fails" 1 $E
printf 'rule\tfile\tcount\n' >/dev/null
printf 'executor-downcast\t%s\t1\n' "$E" > .github/pre-commit/shared_helpers_baseline.txt
expect "baselined violation passes" 0 $E
printf 'auto *a = dynamic_cast<OrcaServerHelper *>(s);\nauto *b = dynamic_cast<OrcaServerHelper *>(s);\n' > $E
expect "count above baseline fails" 1 $E

[ "$FAILED" -eq 0 ] && echo "check_shared_helpers.sh self-test: all passed"
exit $FAILED
