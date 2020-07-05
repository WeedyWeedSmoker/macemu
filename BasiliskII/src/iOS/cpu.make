GENCPU_SOURCES=gencpu.c cpudefs.cpp readcpu.cpp
BUILD_SOURCES=cpudefs.cpp cpuemu.cpp cpustbl.cpp cputbl.h

all: gencpu
	./gencpu

cpudefs.cpp: build68k table68k
	./build68k <table68k >$@

build68k: build68k.c
	$(CC) -I$(SRCROOT) $< -o $@

gencpu: $(GENCPU_SOURCES)
	$(CC) -I$(SRCROOT) $(GENCPU_SOURCES) -o $@

clean:
	rm -f gencpu build68k $(BUILD_SOURCES)

install: all
