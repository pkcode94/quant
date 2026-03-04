# libquant -- Makefile
#
# Builds the Quant math engine as a static and shared library.
#
# Usage:
#   make              Build both libquant.a and libquant.so
#   make static       Build only libquant.a
#   make shared       Build only libquant.so
#   make install      Install to PREFIX (default /usr/local)
#   make clean        Remove build artifacts
#   make test         Build and run the example program
#
# Variables:
#   CXX          C++ compiler        (default: g++)
#   CXXFLAGS     Compiler flags      (default: -std=c++17 -O2 -Wall -Wextra)
#   PREFIX       Install prefix      (default: /usr/local)
#   DESTDIR      Staging directory   (default: empty)

CXX       ?= g++
CXXFLAGS  ?= -std=c++17 -O2 -Wall -Wextra
PREFIX    ?= /usr/local
DESTDIR   ?=

# Directories
SRCDIR    := src
INCDIR    := include
BUILDDIR  := build
LIBDIR    := $(BUILDDIR)/lib

# Targets
STATIC_LIB := $(LIBDIR)/libquant.a
SHARED_LIB := $(LIBDIR)/libquant.so

# Sources and objects
SRCS := $(SRCDIR)/quantmath.cpp
OBJS := $(BUILDDIR)/quantmath.o
OBJS_PIC := $(BUILDDIR)/quantmath.pic.o

.PHONY: all static shared clean install test

all: static shared

# -- Static library ------------------------------------------------

static: $(STATIC_LIB)

$(STATIC_LIB): $(OBJS) | $(LIBDIR)
	ar rcs $@ $^

$(BUILDDIR)/quantmath.o: $(SRCDIR)/quantmath.cpp $(INCDIR)/quantmath.h | $(BUILDDIR)
	$(CXX) $(CXXFLAGS) -I$(INCDIR) -c $< -o $@

# -- Shared library ------------------------------------------------

shared: $(SHARED_LIB)

$(SHARED_LIB): $(OBJS_PIC) | $(LIBDIR)
	$(CXX) $(CXXFLAGS) -shared -o $@ $^

$(BUILDDIR)/quantmath.pic.o: $(SRCDIR)/quantmath.cpp $(INCDIR)/quantmath.h | $(BUILDDIR)
	$(CXX) $(CXXFLAGS) -fPIC -I$(INCDIR) -c $< -o $@

# -- Directories ---------------------------------------------------

$(BUILDDIR):
	mkdir -p $(BUILDDIR)

$(LIBDIR):
	mkdir -p $(LIBDIR)

# -- Install -------------------------------------------------------

install: all
	install -d $(DESTDIR)$(PREFIX)/include
	install -d $(DESTDIR)$(PREFIX)/lib
	install -m 644 $(INCDIR)/quantmath.h $(DESTDIR)$(PREFIX)/include/
	install -m 644 $(STATIC_LIB) $(DESTDIR)$(PREFIX)/lib/
	install -m 755 $(SHARED_LIB) $(DESTDIR)$(PREFIX)/lib/

# -- Test (build + run example) ------------------------------------

test: all
	$(MAKE) -C example LIBQUANT_DIR=$(CURDIR)
	./example/quant_example

# -- Clean ---------------------------------------------------------

clean:
	rm -rf $(BUILDDIR)
	$(MAKE) -C example clean 2>/dev/null || true
