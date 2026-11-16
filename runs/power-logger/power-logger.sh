#!/system/bin/sh
set -eu
base=/data/local/tmp/s26-power-logger
binary=$base/power-logger

running() {
    [ -r "$base/pid" ] || return 1
    read -r pid < "$base/pid"
    case "$pid" in ''|*[!0-9]*) return 1 ;; esac
    [ "$(readlink /proc/$pid/exe 2>/dev/null)" = "$binary" ] && kill -0 "$pid" 2>/dev/null
}

case "${1:-status}" in
    start)
        interval=${2:-1000}
        case "$interval" in ''|*[!0-9]*) echo 'Interval must be 250..60000 ms'; exit 1 ;; esac
        [ "$interval" -ge 250 ] && [ "$interval" -le 60000 ] || exit 1
        if running; then
            echo "Already running: PID $pid"
        else
            nohup setsid "$binary" "$base" "$interval" </dev/null >>"$base/service.log" 2>&1 &
            attempts=0
            until running; do
                attempts=$((attempts + 1))
                if [ "$attempts" -ge 50 ]; then cat "$base/service.log"; exit 1; fi
                sleep 0.1
            done
            echo "Started: PID $pid, interval $interval ms"
        fi
        cat "$base/current"
        ;;
    status)
        if running; then
            echo "Running: PID $pid"
            read -r log < "$base/current"
            echo "$log"
            tail -n 2 "$log"
        else
            echo 'Stopped'
        fi
        ;;
    stop)
        if running; then
            kill -TERM "$pid"
            attempts=0
            while running; do
                attempts=$((attempts + 1))
                [ "$attempts" -lt 100 ] || { echo 'Still stopping'; exit 1; }
                sleep 0.1
            done
            echo 'Stopped; log flushed'
        else
            echo 'Already stopped'
        fi
        ;;
    *) echo "Usage: sh $0 {start [interval_ms]|status|stop}"; exit 1 ;;
esac
