CC = cc
AR = ar
PREFIX ?= /usr/local
CPPFLAGS += -Iinclude -Isrc
CFLAGS ?= -O2
CFLAGS += -std=c11 -Wall -Wextra -Wpedantic
LDLIBS += -lrt -ldl -lm -pthread

LIB_SOURCES = \
	src/core/device.c \
	src/core/capabilities.c \
	src/core/events.c \
	src/controls/light.c \
	src/controls/network.c \
	src/controls/battery.c \
	src/controls/usb_gadget.c \
	src/controls/usb_mode.c \
	src/controls/power.c \
	src/linux/io.c \
	src/linux/process.c \
	src/linux/framebuffer.c \
	src/linux/evdev.c \
	src/linux/usb_configfs.c \
	src/profiles/detect.c \
	src/profiles/kindle.c \
	src/backends/kindle/lipc_api.c \
	src/backends/kindle/network.c \
	src/backends/kindle/usb.c \
	src/backends/kindle/usb_mode.c \
	src/profiles/kobo.c \
	src/backends/kobo/setup.c \
	src/backends/kobo/light.c \
	src/backends/kobo/wifi.c \
	src/backends/kobo/network.c \
	src/backends/kobo/wpa.c
OBJECTS = $(LIB_SOURCES:%.c=build/%.o)
SHARED_OBJECTS = $(LIB_SOURCES:%.c=build/shared/%.o)
HEADERS = $(wildcard include/*.h src/*.h src/*/*.h src/backends/*/*.h)
FORMAT_SOURCES = $(LIB_SOURCES) cli/main.c $(HEADERS)

.PHONY: all shared clean install install-shared format format-check lint python-format python-format-check python-lint
all: build/libereader-hal.a build/ereader-hal

shared: build/libereader-hal.so

build/shared/%.o: %.c
	mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS) -fPIC -MMD -MP -c $< -o $@

build/libereader-hal.so: $(SHARED_OBJECTS)
	$(CC) -shared $(SHARED_LDFLAGS) -Wl,-soname,libereader-hal.so -o $@ $^ $(LDLIBS)

build/%.o: %.c
	mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS) -MMD -MP -c $< -o $@

build/libereader-hal.a: $(OBJECTS)
	$(AR) rcs $@ $^

build/ereader-hal: build/cli/main.o build/libereader-hal.a
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $^ $(LDLIBS)

install: all
	install -d $(DESTDIR)$(PREFIX)/include $(DESTDIR)$(PREFIX)/lib $(DESTDIR)$(PREFIX)/bin
	install -m 644 include/*.h $(DESTDIR)$(PREFIX)/include/
	install -m 644 build/libereader-hal.a $(DESTDIR)$(PREFIX)/lib/
	install -m 755 build/ereader-hal $(DESTDIR)$(PREFIX)/bin/

install-shared: shared
	install -d $(DESTDIR)$(PREFIX)/lib
	install -m 755 build/libereader-hal.so $(DESTDIR)$(PREFIX)/lib/

python-format:
	ruff format python

python-format-check:
	ruff format --check python

python-lint:
	ruff check python

format:
	clang-format -i $(FORMAT_SOURCES)

format-check:
	clang-format --dry-run --Werror $(FORMAT_SOURCES)

lint:
	cppcheck --enable=warning,style,performance,portability --error-exitcode=1 \
		--std=c11 --platform=unix32 --library=posix --template=gcc \
		-Iinclude -Isrc $(LIB_SOURCES) cli/main.c
	shellcheck scripts/*.sh

clean:
	rm -rf build

-include $(OBJECTS:.o=.d) $(SHARED_OBJECTS:.o=.d) build/cli/main.d
