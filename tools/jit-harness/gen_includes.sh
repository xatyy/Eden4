#!/bin/sh
HARNESS="$(cd "$(dirname "$0")" && pwd)"
REPO="$(cd "$HARNESS/../.." && pwd)"
# Prints -I flags for boost's modular headers.
for d in $REPO/eden/.cache/cpm/boost/1.92.0/libs/*/include; do printf -- "-I%s " "$d"; done
