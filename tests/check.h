#ifndef TESTS_CHECK_H
#define TESTS_CHECK_H

#include <stdio.h>
#include <stdlib.h>

static int check_failures = 0;

#define CHECK(cond, ...) do { \
    if (!(cond)) { \
        check_failures++; \
        fprintf(stderr, "FAIL %s:%d: ", __FILE__, __LINE__); \
        fprintf(stderr, __VA_ARGS__); \
        fprintf(stderr, "\n"); \
    } \
} while (0)

static inline int check_finish(const char *name)
{
    if (check_failures) {
        printf("%s: %d failure(s)\n", name, check_failures);
        return 1;
    }
    printf("%s: PASS\n", name);
    return 0;
}

#endif
