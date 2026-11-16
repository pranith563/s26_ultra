#include <cerrno>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <fcntl.h>
#include <string>
#include <sys/file.h>
#include <sys/stat.h>
#include <sys/system_properties.h>
#include <unistd.h>

static volatile sig_atomic_t stopping = 0;
static void stop(int) { stopping = 1; }

static long long clock_ns(clockid_t clock) {
    timespec value{};
    if (clock_gettime(clock, &value) != 0) {
        std::perror("clock_gettime");
        std::exit(1);
    }
    return static_cast<long long>(value.tv_sec) * 1000000000LL + value.tv_nsec;
}

static std::string read_text(const char* path) {
    char buffer[256]{};
    const int fd = open(path, O_RDONLY | O_CLOEXEC);
    if (fd < 0) return {};
    const ssize_t count = read(fd, buffer, sizeof(buffer) - 1);
    close(fd);
    if (count <= 0) return {};
    std::string text(buffer, count);
    while (!text.empty() && (text.back() == '\n' || text.back() == '\r')) text.pop_back();
    return text;
}

struct Reading {
    const char* column;
    const char* path;
    int fd = -1;
    long long value = 0;
    bool valid = false;
};

int main(int argc, char** argv) {
    if (argc != 5) {
        std::fprintf(stderr, "Usage: power-logger OUTPUT_DIRECTORY INIT_SECONDS RUN_SECONDS INTERVAL_MS\n");
        return 1;
    }
    long options[3]{};
    for (int i = 0; i < 3; ++i) {
        char* end = nullptr;
        errno = 0;
        options[i] = std::strtol(argv[i + 2], &end, 10);
        const long minimum = i == 0 ? 0 : (i == 1 ? 1 : 250);
        const long maximum = i == 2 ? 60000 : 86400;
        if (!*argv[i + 2] || *end || errno == ERANGE || options[i] < minimum || options[i] > maximum) {
            std::fprintf(stderr, "Init: 0..86400 s; run: 1..86400 s; sample interval: 250..60000 ms\n");
            return 1;
        }
    }
    const long init_seconds = options[0], run_seconds = options[1], interval_ms = options[2];
    const std::string base = argv[1];
    const int lock_fd = open((base + "/lock").c_str(), O_CREAT | O_RDWR | O_CLOEXEC, 0600);
    if (lock_fd < 0 || flock(lock_fd, LOCK_EX | LOCK_NB) != 0) {
        std::perror("collector lock (another collector may be running)");
        return 1;
    }
    const std::string boot = read_text("/proc/sys/kernel/random/boot_id");
    if (boot.empty()) return 1;
    const long long started = clock_ns(CLOCK_BOOTTIME);
    const long long run_start = started + init_seconds * 1000000000LL;
    const long long run_end = run_start + run_seconds * 1000000000LL;
    const std::string csv_path = base + "/power-" + boot + "-" + std::to_string(started) + ".csv";
    FILE* csv = std::fopen(csv_path.c_str(), "wx");
    if (!csv) { std::perror("create CSV"); return 1; }
    setvbuf(csv, nullptr, _IOLBF, 0);
    Reading readings[] = {
        {"battery_voltage_uv", "/sys/class/power_supply/battery/voltage_now"},
        {"battery_current_ua", "/sys/class/power_supply/battery/current_now"},
        {"battery_current_avg_ua", "/sys/class/power_supply/battery/current_avg"},
        {"battery_temp_deci_c", "/sys/class/power_supply/battery/temp"},
        {"system_current_ma", "/sys/class/power_supply/battery/power_now"},
        {"system_current_avg_ma", "/sys/class/power_supply/battery/power_avg"},
        {"charge_uah", "/sys/class/power_supply/battery/charge_counter"},
        {"capacity_pct", "/sys/class/power_supply/battery/capacity"},
        {"charger_temp_deci_c", "/sys/class/power_supply/battery/chg_temp"},
        {"usb_temp_deci_c", "/sys/class/power_supply/battery/usb_temp"},
        {"usb_online", "/sys/class/power_supply/usb/online"},
    };
    for (auto& r : readings) r.fd = open(r.path, O_RDONLY | O_CLOEXEC);

    FILE* metadata = std::fopen((csv_path + ".meta.txt").c_str(), "wx");
    if (!metadata) { std::perror("create metadata"); return 1; }
    char firmware[PROP_VALUE_MAX]{};
    __system_property_get("ro.build.PDA", firmware);
    std::fprintf(metadata, "boot_id=%s\nfirmware=%s\ninterval_ms=%ld\ninit_seconds=%ld\nrun_seconds=%ld\npid=%d\nstart_boottime_ns=%lld\nstart_realtime_ns=%lld\n",
                 boot.c_str(), firmware, interval_ms, init_seconds, run_seconds, getpid(), started, clock_ns(CLOCK_REALTIME));
    std::fprintf(metadata, "units=column suffixes; Samsung power_now/power_avg are ISYS mA on S948BXXU1AZAB\n"
                 "battery_interface_power_w=signed voltage*current, not calibrated total-device power\n"
                 "read_error_mask=bit N identifies missing/invalid reading N in header order\n"
                 "suspend=no wake lock; logging pauses during system suspend; boottime exposes gaps\n"
                 "persistence=each row flushed to OS; fsync on clean stop; sudden power loss may lose recent rows\n");
    if (std::fclose(metadata) != 0) { std::perror("write metadata"); return 1; }

    std::signal(SIGTERM, stop);
    std::signal(SIGINT, stop);
    std::fprintf(csv, "sequence,phase,elapsed_seconds,boottime_ns,monotonic_ns,sample_gap_ns,sampling_gap,read_end_boottime_ns,process_cpu_ns");
    for (const auto& r : readings) std::fprintf(csv, ",%s", r.column);
    std::fprintf(csv, ",battery_interface_power_w,read_error_mask\n");

    const std::string pid_path = base + "/pid";
    long long sequence = 0;
    long long deadline = started;
    long long previous = 0;
    int result = 0;
    while (!stopping) {
        const auto before = clock_ns(CLOCK_BOOTTIME);
        if (before >= run_end) break;
        const auto monotonic = clock_ns(CLOCK_MONOTONIC);
        const auto gap = sequence ? before - previous : 0;
        previous = before;
        unsigned errors = 0;
        unsigned bit = 0;
        for (auto& r : readings) {
            char buffer[64]{};
            const ssize_t count = r.fd < 0 ? -1 : pread(r.fd, buffer, sizeof(buffer) - 1, 0);
            char* tail = nullptr;
            errno = 0;
            r.value = count > 0 ? std::strtoll(buffer, &tail, 10) : 0;
            r.valid = count > 0 && tail != buffer && errno != ERANGE;
            if (r.valid) {
                while (*tail == ' ' || *tail == '\n' || *tail == '\r' || *tail == '\t') ++tail;
                r.valid = *tail == '\0';
            }
            if (!r.valid) errors |= 1U << bit;
            ++bit;
        }
        std::fprintf(csv, "%lld,%s,%.3f,%lld,%lld,%lld,%d,%lld,%lld", sequence,
                     before < run_start ? "init" : "run", (before - started) / 1e9, before,
                     monotonic, gap, gap > interval_ms * 1500000LL ? 1 : 0,
                     clock_ns(CLOCK_BOOTTIME), clock_ns(CLOCK_PROCESS_CPUTIME_ID));
        for (const auto& r : readings) {
            if (r.valid) std::fprintf(csv, ",%lld", r.value);
            else std::fputc(',', csv);
        }
        if (readings[0].valid && readings[1].valid)
            std::fprintf(csv, ",%.9f", static_cast<double>(readings[0].value) * readings[1].value / 1e12);
        else std::fputc(',', csv);
        std::fprintf(csv, ",%u\n", errors);
        if (std::fflush(csv) != 0 || std::ferror(csv)) { std::perror("write sample"); result = 1; break; }
        if (sequence == 0) {
            FILE* current = std::fopen((base + "/current").c_str(), "w");
            if (!current) { std::perror("current log path"); result = 1; break; }
            std::fprintf(current, "%s\n", csv_path.c_str());
            if (std::fclose(current) != 0) { result = 1; break; }
            FILE* pid = std::fopen(pid_path.c_str(), "w");
            if (!pid) { std::perror("PID file"); result = 1; break; }
            std::fprintf(pid, "%d\n", getpid());
            if (std::fclose(pid) != 0) { result = 1; break; }
        }
        ++sequence;
        deadline += interval_ms * 1000000LL;
        const auto now = clock_ns(CLOCK_BOOTTIME);
        if (deadline <= now) deadline = now + interval_ms * 1000000LL;
        if (deadline > run_end) deadline = run_end;
        const timespec target{static_cast<time_t>(deadline / 1000000000LL),
                              static_cast<long>(deadline % 1000000000LL)};
        int error;
        do { error = clock_nanosleep(CLOCK_BOOTTIME, TIMER_ABSTIME, &target, nullptr); }
        while (error == EINTR && !stopping);
        if (error && error != EINTR) { errno = error; std::perror("sleep"); result = 1; break; }
    }
    if (fsync(fileno(csv)) != 0) { std::perror("sync CSV"); result = 1; }
    if (std::fclose(csv) != 0) result = 1;
    metadata = std::fopen((csv_path + ".meta.txt").c_str(), "a");
    if (metadata) {
        std::fprintf(metadata, "stop_reason=%s\nstop_boottime_ns=%lld\n",
                     result ? "error" : (stopping ? "requested" : "duration"), clock_ns(CLOCK_BOOTTIME));
        std::fclose(metadata);
    }
    unlink(pid_path.c_str());
    for (const auto& r : readings) if (r.fd >= 0) close(r.fd);
    close(lock_fd);
    return result;
}
