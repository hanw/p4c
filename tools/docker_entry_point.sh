#!/bin/bash

set -x
# setup hugepage
/bfn/hugepage_setup.sh >&2

# execute docker command
exec "$@"
