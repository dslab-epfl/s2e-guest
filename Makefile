BINARIES = demos/quicksort init_env/init_env.so s2ecmd/s2ecmd s2eget/s2eget
CCFLAGS  = -Iinclude -Wall -g -O0 -std=c99
LDLIBS   = -ldl

all: $(BINARIES)

s2ecmd/s2ecmd s2eget/s2eget: include/s2e.h

%: %.c
	$(CC) $(CCFLAGS) $(CFLAGS) $< -o $@

init_env/init_env.so: init_env/init_env.c
	$(CC) $(CCFLAGS) -fPIC -shared $(CFLAGS) $^ -o $@ $(LDLIBS)

clean:
	rm -f $(BINARIES)

install: $(BINARIES)
	cp $(BINARIES) $(BUILD_DIR)

.PHONY: all clean install
