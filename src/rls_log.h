#pragma once
#include <stdarg.h>

// ---------------------------------------------------------------------------
// Log levels
// ---------------------------------------------------------------------------

typedef enum {
    RLS_LOG_DEBUG   = 0,
    RLS_LOG_INFO    = 1,
    RLS_LOG_WARNING = 2,
    RLS_LOG_ERROR   = 3,
    RLS_LOG_NONE    = 4,   // disable all output
} RlsLogLevel;

// ---------------------------------------------------------------------------
// API
// ---------------------------------------------------------------------------

// Set the minimum level that will be emitted (default: RLS_LOG_INFO).
void        rls_set_log_level(RlsLogLevel level);

// Parse "debug" / "info" / "warning" / "error" / "none".
// Returns RLS_LOG_INFO for unrecognised strings.
RlsLogLevel rls_log_level_from_string(const char *s);

void rls_logv(RlsLogLevel level, const char *fmt, va_list ap);
void rls_log (RlsLogLevel level, const char *fmt, ...);

// ---------------------------------------------------------------------------
// Convenience macros
// ---------------------------------------------------------------------------

#define RLS_DEBUG(...)   rls_log(RLS_LOG_DEBUG,   __VA_ARGS__)
#define RLS_INFO(...)    rls_log(RLS_LOG_INFO,    __VA_ARGS__)
#define RLS_WARNING(...) rls_log(RLS_LOG_WARNING, __VA_ARGS__)
#define RLS_ERROR(...)   rls_log(RLS_LOG_ERROR,   __VA_ARGS__)
