#!/system/bin/sh
set -u
base=/data/local/tmp/s26-qprof-2.26.10.6
mount -o rprivate none / || exit 80
mount --bind "$base/libs" /vendor/qprof/libs || exit 81
export LD_LIBRARY_PATH="$base/libs"
export QMONITOR_BACKEND_LIB_PATH="$base/libs/backends"
export QPROF_META_FB_PATH="$base/ExtQProfiler.fb"
export PATH="$base/bins:$PATH"
cd "$base" || exit 82
timeout -s KILL 20 "$base/bins/qprof" --capabilities
result=$?
umount /vendor/qprof/libs
printf '\nTOOL_EXIT=%s\n' "$result"
exit "$result"
