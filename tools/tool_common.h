#ifndef TOOLS_COMMON_H
#define TOOLS_COMMON_H

#include "icmcore.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/resource.h>

static inline char *read_file(const char *path)
{
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    char *buf = malloc((size_t)n + 1);
    if (!buf) { fclose(f); return NULL; }
    size_t got = fread(buf, 1, (size_t)n, f);
    buf[got] = 0;
    fclose(f);
    return buf;
}

/* Reads ranges.<who>.range from a ranges JSON file (who = "btn" or "bb"). */
static inline int load_range(const char *path, const char *who, float *out)
{
    char *txt = read_file(path);
    if (!txt) { fprintf(stderr, "cannot read %s\n", path); return -1; }
    char key[32];
    snprintf(key, sizeof key, "\"%s\": {", who);
    char *p = strstr(txt, key);
    p = p ? strstr(p, "\"range\": \"") : NULL;
    if (!p) { fprintf(stderr, "no %s range in %s\n", who, path); free(txt); return -1; }
    p += strlen("\"range\": \"");
    char *e = strchr(p, '"');
    if (!e) { free(txt); return -1; }
    *e = 0;
    int r = range_parse(p, out);
    if (r) fprintf(stderr, "bad range in %s: %s\n", path, p);
    free(txt);
    return r;
}

static inline double peak_rss_mb(void)
{
    struct rusage ru;
    getrusage(RUSAGE_SELF, &ru);
    return ru.ru_maxrss / 1024.0;   /* Linux: kilobytes */
}

#endif
