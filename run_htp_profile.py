"""Repeat the staged Conv/ReLU HTP test with qprof and Perfetto captures."""
import datetime
import hashlib
import json
import os
from pathlib import Path
import subprocess
import time
import winreg

PROJECT = Path(__file__).resolve().parent
ADB = ["adb", "-s", "RZGL21DRL6B"]
TARGET = "/data/local/tmp/s26-qnn-2.44"
SERVER = "/data/local/tmp/s26-qprof-2.26.10.6"
QPROF = r"C:\Program Files (x86)\Qualcomm\QualcommProfiler\CLI\host-windows\bins\qprof.exe"
SDK = Path(r"C:\qairt-data\qairt\2.44.0.260225")
RUN_ID = datetime.datetime.now(datetime.timezone.utc).strftime("%Y%m%dT%H%M%SZ")
OUT = PROJECT / "runs" / "qnn-htp" / RUN_ID
OUT.mkdir(parents=True)
(OUT / "qprof").mkdir()
ENV = os.environ.copy()
with winreg.OpenKey(winreg.HKEY_LOCAL_MACHINE,
                    r"SYSTEM\CurrentControlSet\Control\Session Manager\Environment") as key:
    ENV["PATH"] += ";" + os.path.expandvars(winreg.QueryValueEx(key, "Path")[0])


def root(command, timeout=20):
    # KernelSU may return transport status zero even when the command fails.
    result = subprocess.run(ADB + ["exec-out", "su", "-c",
                                  "set -e; " + command + "; echo __S26_COMMAND_OK__"],
                            capture_output=True, text=True, timeout=timeout, check=True)
    if not result.stdout.rstrip().endswith("__S26_COMMAND_OK__"):
        raise RuntimeError(result.stdout + result.stderr)
    return result.stdout.rsplit("__S26_COMMAND_OK__", 1)[0]


metadata = {"host_started_utc": RUN_ID, "device_serial": ADB[2],
            "sdk": str(SDK), "qprof": QPROF, "qnn_duration_seconds": 3,
            "capture_duration_seconds": 10, "qnn_uid": 0,
            "model_sha256": hashlib.sha256(
                (PROJECT / "runs/qnn-htp/build/libqnn_conv_relu.so").read_bytes()).hexdigest()}
server = profiler = None
server_pid = trace_pid = None
forwarded = False
try:
    metadata["before"] = root("id; getenforce; getprop ro.build.fingerprint; "
                              "cat /proc/sys/kernel/random/boot_id; cat /proc/uptime")
    existing = root(f'if [ -f {SERVER}/server.pid ]; then p=$(cat {SERVER}/server.pid); '
                    f'readlink /proc/$p/exe || true; fi')
    if SERVER + "/bins/qmonitor-grpc-server" in existing:
        raise RuntimeError("The staged profiler server is already running; stop it first.")
    subprocess.run(ADB + ["forward", "--no-rebind", "tcp:62472", "tcp:62472"], check=True)
    forwarded = True
    with (OUT / "server.log").open("w") as log:
        server = subprocess.Popen(ADB + ["exec-out", "su", "-c", f"sh {SERVER}/server.sh"],
                                  stdout=log, stderr=subprocess.STDOUT)
    time.sleep(0.5)
    server_pid = int(root(f"cat {SERVER}/server.pid").strip())
    subprocess.run([QPROF, "--configure", "--server-ip", "127.0.0.1", "--port", "62472"],
                   env=ENV, check=True, capture_output=True, timeout=10)
    capabilities = subprocess.run([QPROF, "--capabilities"], env=ENV, check=True,
                                  capture_output=True, text=True, timeout=10)
    (OUT / "capabilities.txt").write_text(capabilities.stdout, encoding="utf-8")
    if "profiler:apps-proc-cpu-metrics" not in capabilities.stdout:
        raise RuntimeError("Profiler did not enumerate the expected CPU capability.")

    config = OUT / "perfetto.pbtxt"
    config.write_text('''buffers { size_kb: 8192 fill_policy: RING_BUFFER }
duration_ms: 10000
data_sources { config { name: "linux.ftrace" ftrace_config {
  ftrace_events: "sched/sched_switch"
  ftrace_events: "sched/sched_waking"
  ftrace_events: "task/task_newtask"
  ftrace_events: "task/task_rename"
  ftrace_events: "power/cpu_frequency"
  ftrace_events: "power/cpu_idle"
  ftrace_events: "ftrace/print"
} } }
data_sources { config { name: "linux.process_stats" process_stats_config {
  scan_all_processes_on_start: true
} } }
''', encoding="utf-8")
    remote_trace = f"/data/misc/perfetto-traces/s26-htp-{RUN_ID}.perfetto-trace"
    subprocess.run(ADB + ["push", str(config), f"{TARGET}/perfetto.pbtxt"], check=True,
                   capture_output=True)
    started = root(f"perfetto --background-wait --txt -c {TARGET}/perfetto.pbtxt -o {remote_trace}")
    (OUT / "perfetto-start.txt").write_text(started, encoding="utf-8")
    trace_pid = int(started.strip().splitlines()[-1])
    profiler_command = [QPROF, "--profile", "--capabilities-list",
                        "profiler:apps-proc-cpu-metrics", "profiler:apps-proc-mem-metrics",
                        "profiler:apps-proc-thermal-metrics", "profiler:nsp-dsp-metrics",
                        "--profile-time", "10", "--streaming-rate", "500",
                        "--sampling-rate", "100", "--result-format", "csv",
                        "--result-dir-path", str(OUT / "qprof")]
    metadata["qprof_command"] = profiler_command
    with (OUT / "qprof.log").open("w") as log:
        profiler = subprocess.Popen(profiler_command, env=ENV, stdout=log,
                                    stderr=subprocess.STDOUT)
    time.sleep(1)
    workload = (f'cd {TARGET}; export LD_LIBRARY_PATH={TARGET}/lib; '
                f'export ADSP_LIBRARY_PATH="{TARGET}/dsp;/vendor/lib/rfsa/adsp;/vendor/dsp/cdsp"; '
                './clock_probe; '
                'echo "B|$$|S26_QNN_HTP_RUN" > /sys/kernel/tracing/trace_marker; '
                'cat /proc/uptime; '
                f'timeout -s KILL 20 ./qnn-net-run --model ./libqnn_conv_relu.so '
                f'--backend ./lib/libQnnHtp.so --input_list input_list.txt '
                f'--output_dir session-{RUN_ID} --profiling_level detailed '
                '--duration 3 --keep_num_outputs 2 --log_level info; '
                'cat /proc/uptime; echo "E|$$" > /sys/kernel/tracing/trace_marker; ./clock_probe')
    metadata["workload_command"] = workload
    (OUT / "qnn.log").write_text(root(workload, timeout=30), encoding="utf-8")
    metadata["qprof_exit"] = profiler.wait(timeout=20)
    if metadata["qprof_exit"] != 0:
        raise RuntimeError("qprof capture failed; see qprof.log")
    # The bounded Perfetto session starts before qprof and ends before this wait returns.
    root(f"chmod 644 {remote_trace}")
    subprocess.run(ADB + ["pull", remote_trace, str(OUT / "htp.perfetto-trace")], check=True)
    # Pull regular files; qnn's latest-log symlink is inaccessible to adb sync.
    files = root(f"find {TARGET}/session-{RUN_ID} -type f").splitlines()
    for remote in files:
        local = OUT / "qnn" / remote.split(f"/session-{RUN_ID}/", 1)[1]
        local.parent.mkdir(parents=True, exist_ok=True)
        root(f"chmod 644 {remote}")
        subprocess.run(ADB + ["pull", remote, str(local)], check=True, capture_output=True)
    metadata["status"] = "captured; analysis required"
finally:
    if profiler and profiler.poll() is None:
        profiler.terminate()
        profiler.wait(timeout=10)
    if trace_pid:
        root(f'if [ "$(readlink /proc/{trace_pid}/exe)" = /system/bin/perfetto ]; '
             f'then kill -TERM {trace_pid}; fi')
    if server_pid:
        root(f'if [ "$(readlink /proc/{server_pid}/exe)" = {SERVER}/bins/qmonitor-grpc-server ]; '
             f'then kill -TERM {server_pid}; fi')
    if server:
        server.wait(timeout=10)
    if forwarded:
        subprocess.run(ADB + ["forward", "--remove", "tcp:62472"], check=True)
    metadata["after"] = root("id; getenforce; cat /proc/sys/kernel/random/boot_id; "
                             "cat /sys/kernel/tracing/tracing_on")
    (OUT / "run.json").write_text(json.dumps(metadata, indent=2) + "\n", encoding="utf-8")
print(OUT)
