#define _GNU_SOURCE
#include "lipc_api.h"
#include <dlfcn.h>
#include <errno.h>
#include <string.h>

int er_lipc_error(int code)
{
    switch (code) {
    case 0:
        return 0;
    case 3:
    case 18:
    case 0x101:
        return -ENETDOWN;
    case 4:
    case 8:
        return -EOPNOTSUPP;
    case 5:
        return -ENOMEM;
    case 7:
        return -ENOENT;
    case 9:
        return -EACCES;
    case 10:
    case 14:
        return -ENOSPC;
    case 12:
        return -EINVAL;
    case 13:
    case 0x100:
        return -EBUSY;
    case 15:
        return -ETIMEDOUT;
    default:
        return -EIO;
    }
}

void er_lipc_close(struct er_lipc *api)
{
    if (api->handle)
        api->close(api->handle);
    if (api->library)
        dlclose(api->library);
    memset(api, 0, sizeof(*api));
}

int er_lipc_open(struct er_lipc *api, bool hashes)
{
    memset(api, 0, sizeof(*api));
    /* LIPC and its dependencies retain thread cleanup callbacks after LipcClose.
     * Keep their code mapped until process exit, including between worker calls. */
    api->library = dlopen("liblipc.so", RTLD_NOW | RTLD_LOCAL | RTLD_NODELETE);
    if (!api->library)
        return -EOPNOTSUPP;

#define LOAD(member, symbol)                                                                       \
    do {                                                                                           \
        void *address = dlsym(api->library, symbol);                                               \
        if (!address) {                                                                            \
            er_lipc_close(api);                                                                    \
            return -EOPNOTSUPP;                                                                    \
        }                                                                                          \
        memcpy(&api->member, &address, sizeof(address));                                           \
    } while (0)
    LOAD(open, "LipcOpenNoName");
    LOAD(close, "LipcClose");
    LOAD(get_int, "LipcGetIntProperty");
    LOAD(set_int, "LipcSetIntProperty");
    if (hashes) {
        LOAD(free_string, "LipcFreeString");
        LOAD(get_string, "LipcGetStringProperty");
        LOAD(set_string, "LipcSetStringProperty");
        LOAD(new_hash, "LipcHasharrayNew");
        LOAD(destroy_hash, "LipcHasharrayDestroy");
        LOAD(add_hash, "LipcHasharrayAddHash");
        LOAD(hash_count, "LipcHasharrayGetHashCount");
        LOAD(get_hash_int, "LipcHasharrayGetInt");
        LOAD(get_hash_string, "LipcHasharrayGetString");
        LOAD(put_hash_int, "LipcHasharrayPutInt");
        LOAD(put_hash_string, "LipcHasharrayPutString");
        LOAD(access_hash, "LipcAccessHasharrayProperty");
    }
#undef LOAD
    api->handle = api->open();
    if (!api->handle) {
        er_lipc_close(api);
        return -ENETDOWN;
    }
    return 0;
}

int er_kindle_get(const char *source, const char *property, int *value)
{
    struct er_lipc api;
    int result = er_lipc_open(&api, false);
    if (!result) {
        result = er_lipc_error(api.get_int(api.handle, source, property, value));
        er_lipc_close(&api);
    }
    return result;
}

int er_kindle_set(const char *source, const char *property, int value)
{
    struct er_lipc api;
    int result = er_lipc_open(&api, false);
    if (!result) {
        result = er_lipc_error(api.set_int(api.handle, source, property, value));
        er_lipc_close(&api);
    }
    return result;
}

int er_lipc_read_hash(struct er_lipc *api, const char *service, const char *property, void **output)
{
    void *input = api->new_hash(api->handle);
    if (!input)
        return -ENOMEM;
    int result = er_lipc_error(api->access_hash(api->handle, service, property, input, output));
    api->destroy_hash(input);
    /* An empty wifid result is a NULL hash array. */
    if (!result && !*output) {
        *output = api->new_hash(api->handle);
        if (!*output)
            result = -ENOMEM;
    }
    return result;
}
