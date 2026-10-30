#include "../arena.h"
#include "../log.h"
#include "../perf_counters.h"
#include <errno.h>
#include <linux/perf_event.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/syscall.h>
#include <unistd.h>

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Linux performance counters backend
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#ifndef __NR_perf_event_open
#define __NR_perf_event_open 298
#endif

struct PerfCounterSession {
    int fd; // the open counter, -1 when the session holds none
    bool is_active;
};

static bool g_perf_counters_initialized = false;

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static int perf_event_open(struct perf_event_attr* attr, pid_t pid, int cpu, int group_fd, unsigned long flags) {
    return syscall(__NR_perf_event_open, attr, pid, cpu, group_fd, flags);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

bool perf_counter_event_to_platform_event(PerfCounterEvent event, PerfCounterPlatformEvent* out_event) {
    switch (event) {
        case PERF_COUNTER_INSTRUCTIONS:
            *out_event = (PerfCounterPlatformEvent) { PERF_TYPE_HARDWARE, PERF_COUNT_HW_INSTRUCTIONS };
            return true;
    }

    return false;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Opens a counter for event, disabled and counting user space only, or returns -1 with errno set.

static int counter_open(PerfCounterEvent event) {
    PerfCounterPlatformEvent platform_event;
    if (!perf_counter_event_to_platform_event(event, &platform_event)) {
        errno = EINVAL;
        return -1;
    }

    struct perf_event_attr attr = { 0 };
    attr.type = platform_event.type;
    attr.size = sizeof(attr);
    attr.config = platform_event.config;
    attr.disabled = 1;
    attr.exclude_kernel = 1;
    attr.exclude_hv = 1;

    return perf_event_open(&attr, 0, -1, -1, 0);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

bool perf_counters_init(void) {
    if (g_perf_counters_initialized) {
        return true;
    }

    // Probe with the one event sessions can ask for, so a successful init means that event opens.
    int fd = counter_open(PERF_COUNTER_INSTRUCTIONS);
    if (fd < 0) {
        if (errno == EPERM || errno == EACCES) {
            log_error("Performance counters require root privileges or CAP_PERFMON capability");
        } else if (errno == ENOENT) {
            log_error("Performance counters not supported on this system");
        } else {
            log_error("Failed to initialize performance counters: %s", strerror(errno));
        }
        return false;
    }

    close(fd);
    g_perf_counters_initialized = true;
    return true;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void perf_counters_shutdown(void) {
    g_perf_counters_initialized = false;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

bool perf_counters_available(void) {
    if (!g_perf_counters_initialized) {
        return false;
    }

    int fd = counter_open(PERF_COUNTER_INSTRUCTIONS);
    if (fd < 0) {
        return false;
    }

    close(fd);
    return true;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

PerfCounterSession* perf_counter_session_create(struct FlArena* arena) {
    if (!g_perf_counters_initialized) {
        log_error("Performance counters not initialized");
        return nullptr;
    }

    PerfCounterSession* session = arena_alloc_zero(arena, PerfCounterSession);
    session->fd = -1;
    return session;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void perf_counter_session_destroy(PerfCounterSession* session) {
    if (!session) {
        return;
    }

    // The arena owns the session, which outlives this call, so the slot must not keep a descriptor
    // value the kernel may have reassigned by the time a later configure closes it.
    if (session->fd >= 0) {
        close(session->fd);
        session->fd = -1;
    }
    session->is_active = false;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

bool perf_counter_session_configure(PerfCounterSession* session, PerfCounterEvent event) {
    if (!session) {
        log_error("Invalid session for perf_counter_session_configure");
        return false;
    }

    if (session->fd >= 0) {
        close(session->fd);
        session->fd = -1;
    }
    session->is_active = false;

    int fd = counter_open(event);
    if (fd < 0) {
        log_error("Failed to open perf counter: %s", strerror(errno));
        return false;
    }

    session->fd = fd;
    return true;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

bool perf_counter_session_start(PerfCounterSession* session) {
    if (!session || session->fd < 0) {
        log_error("Invalid session for perf_counter_session_start");
        return false;
    }

    if (session->is_active) {
        log_error("Session is already active");
        return false;
    }

    if (ioctl(session->fd, PERF_EVENT_IOC_RESET, 0) != 0 || ioctl(session->fd, PERF_EVENT_IOC_ENABLE, 0) != 0) {
        log_error("Failed to start perf counter: %s", strerror(errno));
        return false;
    }

    session->is_active = true;
    return true;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

bool perf_counter_session_stop(PerfCounterSession* session, PerfCounterValue* out_value) {
    if (!session || !out_value) {
        log_error("Invalid parameters for perf_counter_session_stop");
        return false;
    }

    if (!session->is_active) {
        log_error("Session is not active");
        return false;
    }

    ioctl(session->fd, PERF_EVENT_IOC_DISABLE, 0);
    session->is_active = false;

    PerfCounterValue value = 0;
    if (read(session->fd, &value, sizeof(value)) != (ssize_t)sizeof(value)) {
        log_error("Failed to read perf counter: %s", strerror(errno));
        return false;
    }

    *out_value = value;
    return true;
}
