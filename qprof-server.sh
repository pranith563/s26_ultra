#!/system/bin/sh
# Keep the matching profiler libraries in this server's private mount namespace.
exec unshare -m sh -c '
set -eu
base=/data/local/tmp/s26-qprof-2.26.10.6
mount -o rprivate none /
mount --bind "$base/libs" /vendor/qprof/libs
export LD_LIBRARY_PATH="$base/libs"
export QMONITOR_BACKEND_LIB_PATH="$base/libs/backends"
export QPROF_META_FB_PATH="$base/ExtQProfiler.fb"
export PATH="$base/bins:$PATH"
cd "$base"
echo $$ > "$base/server.pid"
exec "$base/bins/qmonitor-grpc-server" -l -n 127.0.0.1 -p 62472
'
