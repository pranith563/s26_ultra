# S26 Ultra development and power measurement

Setup guides, source code, and measured results for a Samsung Galaxy S26 Ultra
used as a rooted native-development and profiling target, powered through a
Monsoon HV Power Monitor with its battery cell removed.

This is a record of one tested device and its tools, not a general rooting
installer. The scripts contain this device's ADB serial and this Windows host's
SDK paths. Review them before using another device or computer.

## Start here

| Task | Guide |
| --- | --- |
| Understand root and restart the phone | [Root and cold-start guide](root-after-restart.txt) |
| Start/stop measurements from the phone or ADB | [S26 Power instructions](power-logger.txt) |
| Build native code, use DMA-BUF, qprof, and QNN HTP | [Development setup](development-setup.txt) |
| Recover from a root/module problem | [Recovery procedures and their limits](module-recovery.txt) |
| Identify the tested packages and artifact hashes | [Baseline manifest](baseline-manifest-2026-11-14.json) |

## Tested baseline

- Device: SM-S948B/DS, firmware `S948BXXU1AZAB`, Android 16 / One UI 8.5,
  Linux 6.12.30 family, January 2026 security patch.
- Root: Root My Galaxy Ultra 1.4.1, DirtyFrag route, KernelSU manager
  `v3.3.1-47-gdbb73665`, driver/UAPI `32727/6`.
- Last verified security state: bootloader locked, Knox warranty bit 0,
  SELinux Enforcing; KernelSU modules disabled and image protection enabled.
- Power: black Monsoon HVPM, 3.90 V at the existing battery-strip connection.
- Host: Windows, Python, ADB, JDK 21, Android SDK platform/build-tools 36,
  NDK r30, Qualcomm Profiler CLI 2.26.10.6 and QAIRT 2.44.0.260225.

The main experiments were performed on **14 November 2026**. The phone's clock
was wrong and sometimes reported January 2026. Use host UTC records, boot IDs,
and elapsed/boot clocks when interpreting results. Earlier reports describe
the state at that stage; later evidence below supersedes their open items.

## Root and cold starts

Root is temporary. A full reboot removes it; Root My Galaxy Ultra's **Root on
boot** setting attempts to restore it after Android starts. **Root before first
unlock** and **Auto Soft reboot** are off in the tested configuration.

For a cold start, confirm 3.90 V with Vout disabled, connect the phone's USB to
the PC, then enable Vout and check that the HVPM main LED stays green. If it
turns red/amber, disable Vout and investigate the power path. Let Android finish
booting, unlock once, and wait for automatic root restoration. Verify with:

```powershell
adb -s RZGL21DRL6B shell su -c id
```

Expect `uid=0(root)`. KernelSU should report `Working [Jailbreak mode]`.
`adb root` is not supported by the production build; use `su` instead.

Five additional full reboot tests passed, following the first successful
automatic reboot. Root also returned after the cold cycle, but startup repeated
the Samsung logo 3-5 times before completing. This predated rooting and remains
unresolved. Successful USB-free operation after startup does not establish
reliable USB-free cold startup. Follow the full [restart guide](root-after-restart.txt).

## Measure with the phone USB disconnected

1. Boot and verify root with the phone USB connected.
2. Open **S26 Power**, set initialization delay/run duration/sample interval,
   and tap **Start**, or start the same collector over ADB:

   ```powershell
   adb -s RZGL21DRL6B shell "su -c 'sh /data/local/tmp/s26-power-logger/control.sh start 10 60 1000'"
   ```

3. Launch your app or binary separately. If launched from ADB, detach that
   workload too so cable removal does not terminate it.
4. Disconnect the phone USB during the initialization delay; leave Monsoon
   powered. Leave the live-reading screen to reduce display overhead.
5. The collector saves locally and stops at the run deadline, or on the next
   wake if the phone is asleep. Use the app to stop early. Reconnect USB to
   retrieve results using the commands in [power-logger.txt](power-logger.txt).

The example uses 10 seconds initialization, 60 seconds run, and 1000 ms sampling.
Analyze `phase=run` and `usb_online=0` rows. Normal sleep is preserved without a
wake lock; gaps are flagged, not interpolated. Reboot ends the run and requires
a fresh Start after root returns.

Phone telemetry is not a calibrated substitute for Monsoon. In particular,
Samsung's `power_now` and `power_avg` fields on this firmware represent ISYS
current in mA, not watts. No usable individual CPU/GPU/HTP rail-energy tracks
were exposed in the tested interfaces. See the [power assessment](runs/power-check/assessment.txt).

## Source and tested app

| File | Purpose |
| --- | --- |
| [power_logger.cpp](power_logger.cpp) | Native on-phone telemetry collector |
| [power-logger.sh](power-logger.sh) | Shared timed Start/Stop/status control |
| [power-app/MainActivity.java](power-app/MainActivity.java) | On-phone controls and live readings |
| [power-app/build.py](power-app/build.py) | Build and sign the APK using installed Windows tools |
| [dma_buf_smoke.cpp](dma_buf_smoke.cpp) | Allocation, mapping, synchronization, and FD-sharing test |
| [clock_probe.cpp](clock_probe.cpp) | Clock samples for aligning profiling streams |
| [qprof-server.sh](qprof-server.sh) | Isolated profiler-library setup |
| [run_htp_profile.py](run_htp_profile.py) | Bounded QNN HTP, qprof, and Perfetto capture on the staged target |
| [reliability_test.py](reliability_test.py) | Full-reboot root-restoration test; running it reboots the phone |

The tested [S26-Power.apk](power-app/build/S26-Power.apk) is included, together
with its [build record](power-app/build/build.json). SHA-256:

```text
84c876ba3fa4d623c268b2e9998cd9253d9e1d755b870364288341cef23bd827
```

Build from the project directory with `python .\power-app\build.py`. SDK paths
are recorded in that script. The local signing key is deliberately not in Git;
retain it separately to sign compatible updates to the installed app. A fresh
checkout creates a different development key and cannot update an installation
signed with the original key without addressing that signature difference.

The HTP runner requires the model, libraries, and clock probe already staged on
the device as described in the development guide, plus the locally built model
library. Its [recorded build commands](runs/qnn-htp/build/commands.json) use the
separately installed QAIRT SDK. Keep USB connected for that PC-driven runner;
it is separate from the USB-independent power collector.

## Results and limits

| Area | Recorded result | Evidence |
| --- | --- | --- |
| Manual root and first automatic reboot | Passed | [Initial root tests](rooting-test-results-2026-11-14.json) |
| Five additional full reboots | 5/5 restored root automatically | [Reliability results](reliability-results-2026-11-14.json) |
| Cold power cycle | Root restored; clean startup unresolved | [Cold-cycle record](reliability-results-2026-11-14.json) |
| Native ARM64 and DMA-BUF | Allocation/mapping/sync and cross-process FD sharing passed | [Capability checks](capability-check-2026-11-14.json) |
| QNN HTP | Small quantized Conv/ReLU graph executed; overlapping qprof/Perfetto capture completed | [HTP analysis](runs/qnn-htp/20261114T130003Z/analysis.json) |
| CPU versus HTP output | Finite outputs; maximum absolute difference 0.10941744, not bit-exact | [Output comparison](runs/qnn-htp/output-comparison.json) |
| Power interfaces | Battery/ISYS telemetry available; no exposed rail-energy tracks | [Power assessment](runs/power-check/assessment.txt) |
| USB-disconnected logging | Phone/process stayed running; 33 disconnected samples, zero read errors; maximum gap 11.9 s | [Disconnect test](runs/power-logger/usb-disconnect-test.json) |
| APK and timed control | On-phone Start/Stop and ADB timing passed | [Logger validation](runs/power-logger/validation.json) |

GPU counters need validation against a known workload. External GPU/HTP DMA-BUF
import and zero-copy operation have not been established. QNN tests use a small
sample, not a representative full application or an energy benchmark.

Raw CSVs, profiler traces, output tensors, logs, screenshots, and test metadata
are retained in [runs/](runs/). This repository contains device identifiers and
host-specific paths and is intended to remain private unless reviewed for sharing.

## What stays local

Downloaded installers/SDK archives/rooting APKs, proprietary device `.ko` files,
build intermediates, and signing keys are excluded by [.gitignore](.gitignore).
Vendor-driver analysis and disassembly are retained as evidence. Obtain third-party
tools from their original sources listed in the guides; their licenses still apply.
The baseline manifest records the original workspace, including excluded files;
it is not a promise that every historical artifact is included in this repository.

Git preserves original file bytes to keep archived evidence and hashes intact.
The repository setup did not rerun hardware tests or change the phone.
