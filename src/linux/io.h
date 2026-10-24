#ifndef ER_IO_H
#define ER_IO_H
#include <stddef.h>
#include <stdint.h>

int er_read_text(const char *path, char *text, size_t size);
int er_read_line(const char *path, char *text, size_t size);
int er_parse_int(const char *text, int *value);
int er_read_int(const char *path, int *value);
int er_write_bytes(const char *path, const void *data, size_t size);
int er_write_text(const char *path, const char *text);
int er_write_int(const char *path, int value);
int er_run(char *const argv[], char *output, size_t size);
int er_run_timeout(char *const argv[], char *output, size_t size, int timeout_ms);
int64_t er_now_ms(void);
int er_sleep_ms(unsigned milliseconds);
#endif
