#!/bin/bash

set -x
cat /proc/cpuinfo

cat /proc/meminfo

echo 1024 > /sys/devices/system/node/node0/hugepages/hugepages-2048kB/nr_hugepages

# setup hugepage
/bfn/hugepage_setup.sh >&2

# execute docker command
exec "$@"
