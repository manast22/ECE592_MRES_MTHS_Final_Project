# Builds every bench/*.c, attack/*.c and env/*.c into build/<name>.
CC      ?= gcc
CFLAGS  ?= -O2 -g -Wall -Wextra -march=sapphirerapids -mamx-tile -mamx-int8 -mamx-bf16 -Ibench/common
LDLIBS  ?=
# Off-core probes link against these (installed on artemisia):
DSA_LIBS = -laccel-config
QAT_LIBS = -lqat -lusdm

SRCS := $(wildcard bench/*.c attack/*.c env/*.c)
BINS := $(addprefix build/,$(notdir $(SRCS:.c=)))

vpath %.c bench attack env

all: $(BINS)

build/dsa_%: dsa_%.c | build ; $(CC) $(CFLAGS) $< -o $@ $(LDLIBS) $(DSA_LIBS)
build/qat_%: qat_%.c | build ; $(CC) $(CFLAGS) $< -o $@ $(LDLIBS) $(QAT_LIBS)
build/%:     %.c     | build ; $(CC) $(CFLAGS) $< -o $@ $(LDLIBS)

build: ; mkdir -p build

hostinfo: ; env/collect_hostinfo.sh > env/hostinfo_$$(hostname -s)_$$(date +%F).txt
verify: build/freq_probe ; env/verify_env.sh $(CPU)

clean: ; $(RM) -r build

.PHONY: all clean hostinfo verify
