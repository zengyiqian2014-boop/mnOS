#!/bin/bash
# Preserve useful failure details in the check annotation as well as the log.
# This remains readable when a client cannot reach Actions blob storage.
mnos_ci_failure() {
    local result=$1 detail
    trap - ERR
    # tee runs in a process substitution; let it drain the final short message.
    sleep 0.2
    detail=$(tail -c 6000 "$MNOS_CI_LOG")
    detail=${detail//%/%25}
    detail=${detail//$'\r'/%0D}
    detail=${detail//$'\n'/%0A}
    # Docker's progress renderer writes ANSI and CR controls. Keep annotations
    # short enough for the runner parser, on the original output descriptor.
    printf '::error title=Build or installation test failed::%s\n' "$detail" >&"$MNOS_CI_OUTPUT"
    exit "$result"
}
mnos_ci_diagnostics() {
    MNOS_CI_LOG=$1
    export MNOS_CI_LOG
    exec {MNOS_CI_OUTPUT}>&1
    exec > >(tee "$MNOS_CI_LOG") 2>&1
    set -Eeuo pipefail
    trap 'mnos_ci_failure "$?"' ERR
}
