// Test shim: asserts abort with the failing expression.
#pragma once
#include <cstdio>
#include <cstdlib>
#define ASSERT_MSG(_a_, ...) do { if (!(_a_)) { std::fprintf(stderr, "ASSERT FAILED %s:%d: %s\n", __FILE__, __LINE__, #_a_); std::abort(); } } while (0)
#define ASSERT(_a_) ASSERT_MSG(_a_, "")
#define UNREACHABLE_MSG(...) do { std::fprintf(stderr, "UNREACHABLE %s:%d\n", __FILE__, __LINE__); std::abort(); } while (0)
#define UNREACHABLE() UNREACHABLE_MSG("")
#define DEBUG_ASSERT(_a_) do {} while (0)
#define DEBUG_ASSERT_MSG(_a_, ...) do {} while (0)
#define UNIMPLEMENTED() ASSERT(false)
#define UNIMPLEMENTED_MSG(...) ASSERT(false)
#define UNIMPLEMENTED_IF(cond) ASSERT(!(cond))
#define UNIMPLEMENTED_IF_MSG(cond, ...) ASSERT(!(cond))
