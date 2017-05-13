CFLAGS  := $(shell pkg-config --cflags libdw gtk+-3.0)
LDFLAGS := $(shell pkg-config --libs   libdw gtk+-3.0)

# The -Wno-deprecated-declarations is needed due to dwarf_formref()
CFLAGS += -g -Wno-deprecated-declarations

all: dwarview

dwarview: main.c dwarview.c demangle.c
	gcc -o $@ $(CFLAGS) $^ $(LDFLAGS)

clean:
	rm -f dwarview *.o
