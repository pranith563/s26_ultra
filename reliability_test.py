"""Run the requested five full reboots, stopping on the first failed check."""
import json
import subprocess
import time
import xml.etree.ElementTree as ET
from datetime import datetime, timezone
from pathlib import Path

DEVICE = "RZGL21DRL6B"
OUTPUT = Path(__file__).with_name("reliability-results-2026-11-14.json")
STATE = "/data/user/0/dev.experimental.azhlroot/files/boot-root-state.json"
PREFS = "/data/user/0/dev.experimental.azhlroot/shared_prefs/appearance.xml"


def adb(*args):
    try:
        result = subprocess.run(
            ["adb", "-s", DEVICE, *args], capture_output=True,
            text=True, encoding="utf-8", timeout=8,
        )
        return result.returncode, result.stdout.strip(), result.stderr.strip()
    except subprocess.TimeoutExpired:
        return -1, "", "ADB command timed out"


def save():
    OUTPUT.write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")


code, initial_root, error = adb("shell", "su", "-c", "id")
if code or "uid=0(root)" not in initial_root:
    raise SystemExit("Baseline root verification failed: " + error + initial_root)
code, prefs, error = adb("exec-out", "su", "-c", "cat " + PREFS)
if code or not any(
    node.get("name") == "disable_ksu_modules" and node.get("value") == "true"
    for node in ET.fromstring(prefs)
):
    raise SystemExit("Cannot confirm modules are disabled; no reboot performed.")
code, previous_boot, error = adb("shell", "cat", "/proc/sys/kernel/random/boot_id")
if code or not previous_boot:
    raise SystemExit("Cannot read baseline boot ID; no reboot performed.")

report = {
    "started_utc": datetime.now(timezone.utc).isoformat(),
    "requested_additional_reboots": 5,
    "baseline_boot_id": previous_boot,
    "modules_disabled_at_start": True,
    "manual_root_launches_during_test": 0,
    "status": "running",
    "reboots": [],
    "cold_power_cycle": {"status": "pending"},
}
save()

for number in range(1, 6):
    test = {"number": number, "before_boot_id": previous_boot, "passed": False}
    started = time.monotonic()
    print(f"Starting full reboot {number}/5", flush=True)
    code, _, error = adb("reboot")
    if code:
        test["error"] = "Reboot command failed: " + error
    else:
        deadline = started + 180
        observed_boots = []
        while time.monotonic() < deadline:
            _, completed, _ = adb("shell", "getprop", "sys.boot_completed")
            if completed != "1":
                time.sleep(3)
                continue
            _, boot_id, _ = adb("shell", "cat", "/proc/sys/kernel/random/boot_id")
            if not boot_id or boot_id == previous_boot:
                time.sleep(3)
                continue
            if boot_id not in observed_boots:
                observed_boots.append(boot_id)
            test["observed_new_boot_ids"] = observed_boots
            if len(observed_boots) > 1:
                test["error"] = "An additional unexpected reboot was observed."
                break
            code, root_id, error = adb("shell", "su", "-c", "id")
            if code or "uid=0(root)" not in root_id:
                test["last_root_response"] = root_id or error
                time.sleep(3)
                continue
            code, state_text, _ = adb("exec-out", "su", "-c", "cat " + STATE)
            try:
                state = json.loads(state_text) if code == 0 else {}
            except json.JSONDecodeError:
                state = {}
            if state.get("verified") and state.get("verifiedBoot") == boot_id:
                test.update(passed=True, after_boot_id=boot_id,
                            root_shell=root_id, automatic_root_state=state)
                previous_boot = boot_id
                break
            time.sleep(3)
        if not test["passed"] and "error" not in test:
            test["error"] = "Automatic root not verified within 180 seconds."
    test["elapsed_seconds"] = round(time.monotonic() - started, 1)
    report["reboots"].append(test)
    save()
    print(json.dumps(test), flush=True)
    if not test["passed"]:
        report["status"] = "stopped_after_failed_check"
        save()
        raise SystemExit(1)

report["status"] = "five_reboots_passed_cold_cycle_pending"
save()
print("Five additional full reboots passed. Cold power cycle remains pending.", flush=True)
