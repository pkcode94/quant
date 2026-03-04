# example -- Makefile for quant_example
#
# Usage:
#   make                        Build linked against libquant.a (static)
#   make LINK_MODE=shared       Build linked against libquant.so (shared)
#   make clean                  Remove build artifacts
#
# When called from the parent Makefile, LIBQUANT_DIR is set automatically.

LIBQUANT_DIR ?= ..

CXX      ?= g++
CXXFLAGS ?= -std=c++17 -O2 -Wall -Wextra

INCDIR   := $(LIBQUANT_DIR)/include
LIBDIR   := $(LIBQUANT_DIR)/build/lib

TARGET   := quant_example
SRC      := main.cpp

LINK_MODE ?= static

.PHONY: all clean

all: $(TARGET)

ifeq ($(LINK_MODE),shared)
$(TARGET): $(SRC) $(LIBDIR)/libquant.so
	$(CXX) $(CXXFLAGS) -I$(INCDIR) $< -L$(LIBDIR) -lquant -Wl,-rpath,$(LIBDIR) -o $@
else
$(TARGET): $(SRC) $(LIBDIR)/libquant.a
	$(CXX) $(CXXFLAGS) -I$(INCDIR) $< $(LIBDIR)/libquant.a -o $@
endif

clean:
	rm -f $(TARGET)
