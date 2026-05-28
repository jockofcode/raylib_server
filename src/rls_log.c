#include "rls_log.h"
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <pthread.h>

static RlsLogLevel     g_level = RLS_LOG_INFO;
static pthread_mutex_t g_mu    = PTHREAD_MUTEX_INITIALIZER;

static const char *LEVEL_LABEL[] = {
    "DEBUG  ",
    "INFO   ",
    "WARNING",
    "ERROR  ",
};

void rls_set_log_level(RlsLogLevel level) {
    g_level = level;
}

RlsLogLevel rls_log_level_from_string(const char *s) {
    if (!s)                        return RLS_LOG_INFO;
    if (strcmp(s, "debug")   == 0) return RLS_LOG_DEBUG;
    if (strcmp(s, "info")    == 0) return RLS_LOG_INFO;
    if (strcmp(s, "warning") == 0) return RLS_LOG_WARNING;
    if (strcmp(s, "error")   == 0) return RLS_LOG_ERROR;
    if (strcmp(s, "none")    == 0) return RLS_LOG_NONE;
    return RLS_LOG_INFO;
}

void rls_logv(RlsLogLevel level, const char *fmt, va_list ap) {
    if (level < g_level) return;

    int idx = (level < RLS_LOG_NONE) ? (int)level : (int)RLS_LOG_NONE - 1;

    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    struct tm tm;
    localtime_r(&ts.tv_sec, &tm);
    long ms = ts.tv_nsec / 1000000L;

    pthread_mutex_lock(&g_mu);
    fprintf(stderr, "[%02d:%02d:%02d.%03ld] [%s] ",
            tm.tm_hour, tm.tm_min, tm.tm_sec, ms, LEVEL_LABEL[idx]);
    vfprintf(stderr, fmt, ap);
    fprintf(stderr, "\n");
    fflush(stderr);
    pthread_mutex_unlock(&g_mu);
}

void rls_log(RlsLogLevel level, const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    rls_logv(level, fmt, ap);
    va_end(ap);
}
