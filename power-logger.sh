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
        init=${2:-10}
        duration=${3:-60}
        interval=${4:-1000}
        for value in "$init" "$duration" "$interval"; do
            case "$value" in ''|*[!0-9]*) echo 'Times must be whole positive numbers (init may be zero)'; exit 1 ;; esac
        done
        [ "$init" -ge 0 ] && [ "$init" -le 86400 ] || exit 1
        [ "$duration" -ge 1 ] && [ "$duration" -le 86400 ] || exit 1
        [ "$interval" -ge 250 ] && [ "$interval" -le 60000 ] || exit 1
        if running; then
            echo "Already running: PID $pid"
        else
            nohup setsid "$binary" "$base" "$init" "$duration" "$interval" </dev/null >>"$base/service.log" 2>&1 &
            attempts=0
            until running; do
                attempts=$((attempts + 1))
                if [ "$attempts" -ge 50 ]; then cat "$base/service.log"; exit 1; fi
                sleep 0.1
            done
            echo "Started: PID $pid, init $init s, run $duration s, interval $interval ms"
        fi
        cat "$base/current"
        ;;
    status)
        if running; then
            echo 'STATE=running'
        else
            echo 'STATE=stopped'
        fi
        if [ -r "$base/current" ]; then
            read -r log < "$base/current"
            echo "FILE=$log"
            cat "$log.meta.txt"
            printf 'CSV_HEADER='
            head -n 1 "$log"
            printf 'CSV_LAST='
            tail -n 1 "$log"
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
    *) echo "Usage: sh $0 {start [init_seconds run_seconds interval_ms]|status|stop}"; exit 1 ;;
esac
