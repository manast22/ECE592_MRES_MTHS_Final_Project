// key=value command-line parsing shared by probes.
#pragma once
#include <string.h>

static inline const char *kv(int argc, char **argv, const char *k, const char *def) {
    size_t n = strlen(k);
    for (int i = 1; i < argc; i++)
        if (!strncmp(argv[i], k, n) && argv[i][n] == '=') return argv[i] + n + 1;
    return def;
}
