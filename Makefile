CFLAGS  := $(shell pkg-config --cflags libdw gtk+-3.0)
LDFLAGS := $(shell pkg-config --libs   libdw gtk+-3.0)

# The -Wno-deprecated-declarations is needed due to dwarf_formref()
CFLAGS += -g -Wno-deprecated-declarations

ID := io.github.namhyung.dwarview

all: dwarview

dwarview: main.c dwarview.c demangle.c
	gcc -o $@ $(CFLAGS) $^ $(LDFLAGS)

install: dwarview
	install -Dm755 dwarview $(PREFIX)/bin/dwarview
	install -Dm644 dwarview.glade $(PREFIX)/share/dwarview.glade
	install -Dm644 appdata/dwarview-logo.png $(PREFIX)/share/icons/hicolor/256x256/apps/$(ID).png
	install -Dm644 appdata/metainfo.xml $(PREFIX)/share/metainfo/$(ID).metainfo.xml
	install -Dm644 appdata/desktop-entry $(PREFIX)/share/applications/$(ID).desktop

clean:
	rm -f dwarview *.o
