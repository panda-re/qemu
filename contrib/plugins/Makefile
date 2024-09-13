# -*- Mode: makefile -*-
#
# This Makefile example is fairly independent from the main makefile
# so users can take and adapt it for their build. We only really
# include config-host.mak so we don't have to repeat probing for
# programs that the main configure has already done for us.
#

include config-host.mak

TOP_SRC_PATH = $(SRC_PATH)/../..

VPATH += $(SRC_PATH)

NAMES :=
NAMES += bbv
NAMES += execlog
NAMES += hotblocks
NAMES += hotpages
NAMES += howvec

# The lockstep example communicates using unix sockets,
# and can't be easily made to work on windows.
ifneq ($(CONFIG_WIN32),y)
NAMES += lockstep
endif

NAMES += hwprofile
NAMES += cache
NAMES += drcov
NAMES += ips
NAMES += stoptrigger
NAMES += cflow
NAMES += stringsearch
NAMES += syscalls
NAMES += syscalls_logger

# Replace '/' with '_' in plugin names
NAMES_UNDERSCORE := $(subst /,_,$(NAMES))

ifeq ($(CONFIG_WIN32),y)
SO_SUFFIX := .dll
LDLIBS += $(shell $(PKG_CONFIG) --libs glib-2.0)
else
SO_SUFFIX := .so
endif

SONAMES := $(addsuffix $(SO_SUFFIX),$(addprefix lib,$(NAMES_UNDERSCORE)))

# The main QEMU uses Glib extensively so it is perfectly fine to use it
# in plugins (which many example do).
PLUGIN_CFLAGS := $(shell $(PKG_CONFIG) --cflags glib-2.0)
PLUGIN_CFLAGS += -fPIC -Wall
PLUGIN_CFLAGS += -I$(TOP_SRC_PATH)/include/qemu

# Helper that honours V=1 so we get some output when compiling
quiet-@ = $(if $(V),,@$(if $1,printf "  %-7s %s\n" "$(strip $1)" "$(strip $2)" && ))
quiet-command = $(call quiet-@,$2,$3)$1

# for including , in command strings
COMMA := ,

all: $(SONAMES)

PLUGIN_OBJS := $(addsuffix .o,$(NAMES_UNDERSCORE))

# Helper function to generate object file names from source file paths
define obj_from_src
$(subst /,_,$(basename $(1))).o
endef

# Helper function to build an object file from a source file
define build_from_src
$(call obj_from_src,$(1)): $(1)
	$(call quiet-command, \
		$(CC) $(CFLAGS) $(PLUGIN_CFLAGS) -c -o $$@ $$<, \
		BUILD, plugin $$@)
endef

ifeq ($(CONFIG_WIN32),y)
lib%$(SO_SUFFIX): %.o win32_linker.o ../../plugins/libqemu_plugin_api.a
	$(call quiet-command, \
		$(CC) -shared -o $@ $^ $(LDLIBS), \
		LINK, plugin $@)
else ifeq ($(CONFIG_DARWIN),y)
lib%$(SO_SUFFIX): %.o
	$(call quiet-command, \
		$(CC) -bundle -Wl$(COMMA)-undefined$(COMMA)dynamic_lookup -o $@ $^ $(LDLIBS), \
		LINK, plugin $@)
else
lib%$(SO_SUFFIX): %.o
	$(call quiet-command, \
		$(CC) -shared -o $@ $^ $(LDLIBS), \
		LINK, plugin $@)
endif

# Pattern rule for compiling .c files
%.o: %.c
	$(call quiet-command, \
		$(CC) $(CFLAGS) $(PLUGIN_CFLAGS) -c -o $@ $<, \
		BUILD, plugin $@)

# Pattern rule for compiling .cpp files
%.o: %.cpp
	$(call quiet-command, \
		$(CXX) $(CXXFLAGS) $(PLUGIN_CFLAGS) -c -o $@ $<, \
		BUILD, plugin $@)

clean distclean:
	rm -f *.o *$(SO_SUFFIX) *.d
	rm -Rf .libs

.PHONY: all clean
.SECONDARY:
