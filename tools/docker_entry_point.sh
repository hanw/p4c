#!/bin/bash

set -x
cat /proc/cpuinfo

cat /proc/meminfo

# setup hugepage
/bfn/hugepage_setup.sh >&2

# execute docker command
exec "$@"
