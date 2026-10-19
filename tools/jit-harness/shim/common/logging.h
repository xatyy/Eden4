// Test shim: Eden's logging is not needed by the host harness.
#pragma once
#include <cstdio>
#define LOG_TRACE(...)  ((void)0)
#define LOG_DEBUG(...)  ((void)0)
#define LOG_INFO(...)   ((void)0)
#define LOG_WARNING(...) ((void)0)
#define LOG_ERROR(...)  ((void)0)
#define LOG_CRITICAL(...) ((void)0)
