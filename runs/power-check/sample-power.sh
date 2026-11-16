#!/system/bin/sh
set -eu
base=/data/local/tmp/s26-qnn-2.44
batt=/sys/class/power_supply/battery
export LD_LIBRARY_PATH="$base/lib"
cd "$base"
printf 'phase	uptime_s	voltage_uv	current_ua	current_avg_ua	temp_deciC	charge_uah	capacity_pct	power_now_raw	power_avg_raw	usb_online	status
'
for phase in idle cpu recovery; do
  echo "B|$$|S26_POWER_$phase" > /sys/kernel/tracing/trace_marker
  if [ "$phase" = cpu ]; then
    timeout -s KILL 15 ./qnn-net-run --model ./libqnn_conv_relu.so --backend ./lib/libQnnCpu.so --input_list input_list.txt --output_dir /data/local/tmp/s26-power-check/cpu-output --duration 8 --keep_num_outputs 2 --log_level info > /data/local/tmp/s26-power-check/cpu-load.log 2>&1 &
    cpu_pid=$!
  fi
  n=0
  while [ "$n" -lt 32 ]; do
    read -r up unused < /proc/uptime
    read -r v < "$batt/voltage_now"
    read -r i < "$batt/current_now"
    read -r avg < "$batt/current_avg"
    read -r temp < "$batt/temp"
    read -r charge < "$batt/charge_counter"
    read -r cap < "$batt/capacity"
    read -r pn < "$batt/power_now"
    read -r pa < "$batt/power_avg"
    read -r usb < /sys/class/power_supply/usb/online
    read -r status < "$batt/status"
    printf '%s	%s	%s	%s	%s	%s	%s	%s	%s	%s	%s	%s
' "$phase" "$up" "$v" "$i" "$avg" "$temp" "$charge" "$cap" "$pn" "$pa" "$usb" "$status"
    n=$((n + 1))
    sleep 0.25
  done
  if [ "$phase" = cpu ]; then wait "$cpu_pid"; fi
  echo "E|$$" > /sys/kernel/tracing/trace_marker
done
