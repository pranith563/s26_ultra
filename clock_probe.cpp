#include <cstdio>
#include <ctime>

static long long read_clock(clockid_t clock) {
    timespec time{};
    if (clock_gettime(clock, &time) != 0) return -1;
    return static_cast<long long>(time.tv_sec) * 1000000000LL + time.tv_nsec;
}

int main() {
    const auto before = read_clock(CLOCK_BOOTTIME);
    const auto monotonic = read_clock(CLOCK_MONOTONIC);
    const auto raw = read_clock(CLOCK_MONOTONIC_RAW);
    const auto realtime = read_clock(CLOCK_REALTIME);
    unsigned long long ticks, frequency;
    asm volatile("isb; mrs %0, cntvct_el0" : "=r"(ticks) : : "memory");
    asm volatile("mrs %0, cntfrq_el0" : "=r"(frequency));
    const auto after = read_clock(CLOCK_BOOTTIME);
    if (before < 0 || monotonic < 0 || raw < 0 || realtime < 0 || after < 0 || !frequency) return 1;
    const auto counter_ns = static_cast<unsigned long long>(
        static_cast<unsigned __int128>(ticks) * 1000000000ULL / frequency);
    std::printf("{\"boot_before_ns\":%lld,\"monotonic_ns\":%lld,"
                "\"monotonic_raw_ns\":%lld,\"realtime_ns\":%lld,\"boot_after_ns\":%lld,"
                "\"counter_ticks\":%llu,\"counter_frequency_hz\":%llu,\"counter_ns\":%llu}\n",
                before, monotonic, raw, realtime, after, ticks, frequency, counter_ns);
}
