# setup hugepage
/p4c/tools/ptf_hugepage_setup.sh >&2

# execute docker command
exec "$@"
