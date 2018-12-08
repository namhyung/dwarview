CFLAGS  := $(shell pkg-config --cflags libdw gtk+-3.0)
LDFLAGS := $(shell pkg-config --libs   libdw gtk+-3.0)

# The -Wno-deprecated-declarations is needed due to dwarf_formref()
CFLAGS += -g -Wno-deprecated-declarations

RM = rm -f
INSTALL = install
TARGET = dwarview
DATA = $(TARGET).glade

prefix ?= /usr/local
bindir = $(prefix)/bin
_datadir = $(prefix)/share/$(TARGET)
data = $(_datadir)/$(DATA)

all: $(TARGET)

dwarview: main.c dwarview.c demangle.c
	gcc -o $@ $(CFLAGS) $^ $(LDFLAGS)

install: all
	if [ ! -d $(bindir) ]; then \
		$(INSTALL) -d $(bindir); \
	fi
	$(INSTALL) $(TARGET) $(bindir)
	$(INSTALL) -D $(DATA) $(data)

uninstall:
	$(RM) $(bindir)/$(TARGET)
	$(RM) -r $(_datadir)
	
clean:
	rm -f dwarview *.o
