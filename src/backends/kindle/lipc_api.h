#ifndef ER_LIPC_API_H
#define ER_LIPC_API_H

#include <stdbool.h>
#include <stddef.h>

struct er_lipc {
    void *library, *handle;
    void *(*open)(void);
    void (*close)(void *);
    void (*free_string)(char *);
    int (*get_int)(void *, const char *, const char *, int *);
    int (*get_string)(void *, const char *, const char *, char **);
    int (*set_int)(void *, const char *, const char *, int);
    int (*set_string)(void *, const char *, const char *, const char *);
    void *(*new_hash)(void *);
    int (*destroy_hash)(void *);
    int (*add_hash)(void *, size_t *);
    int (*hash_count)(void *);
    int (*get_hash_int)(void *, int, const char *, int *);
    int (*get_hash_string)(void *, int, const char *, char **);
    int (*put_hash_int)(void *, int, const char *, int);
    int (*put_hash_string)(void *, int, const char *, const char *);
    int (*access_hash)(void *, const char *, const char *, const void *, void **);
};

int er_lipc_open(struct er_lipc *api, bool hashes);
void er_lipc_close(struct er_lipc *api);
int er_lipc_error(int code);
int er_lipc_read_hash(struct er_lipc *api, const char *service, const char *property,
                      void **output);
int er_kindle_get(const char *source, const char *property, int *value);
int er_kindle_set(const char *source, const char *property, int value);

#endif
