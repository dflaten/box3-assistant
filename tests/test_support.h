#pragma once

#include <stdbool.h>
#include <stdio.h>

#define ASSERT_TRUE(expr)                                                                                              \
    do {                                                                                                               \
        if (!(expr)) {                                                                                                 \
            fprintf(stderr, "Assertion failed at %s:%d: %s\n", __FILE__, __LINE__, #expr);                             \
            return false;                                                                                              \
        }                                                                                                              \
    } while (0)

#define ASSERT_EQ_INT(expected, actual)                                                                                \
    do {                                                                                                               \
        if ((expected) != (actual)) {                                                                                  \
            fprintf(                                                                                                   \
                stderr, "Assertion failed at %s:%d: expected %d got %d\n", __FILE__, __LINE__, (expected), (actual));  \
            return false;                                                                                              \
        }                                                                                                              \
    } while (0)

typedef struct {
    const char *name;
    bool (*fn)(void);
} test_case_t;
