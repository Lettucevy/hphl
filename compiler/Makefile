# Makefile do compilador HP-HL (Milestone 1)
# Requer GCC 16+ (recomendado: MSYS2 ucrt64)

CXX := g++
CXXFLAGS := -std=c++17 -O2 -Wall -Wextra -Wshadow -Wconversion -Wno-sign-conversion
SRCDIR := src
BUILDDIR := build
TARGET := hphlc.exe

SOURCES := $(SRCDIR)/main.cpp $(SRCDIR)/lexer.cpp $(SRCDIR)/parser.cpp \
           $(SRCDIR)/semantic.cpp $(SRCDIR)/codegen.cpp $(SRCDIR)/hir.cpp \
           $(SRCDIR)/hirOpt.cpp $(SRCDIR)/irgen.cpp $(SRCDIR)/llvmapi.cpp \
           $(SRCDIR)/mir.cpp $(SRCDIR)/stdbuiltins.cpp
OBJECTS := $(SOURCES:$(SRCDIR)/%.cpp=$(BUILDDIR)/%.o)

all: $(TARGET)

$(TARGET): $(OBJECTS)
	$(CXX) $(CXXFLAGS) -o $@ $(OBJECTS) -lLLVM-22

$(BUILDDIR)/%.o: $(SRCDIR)/%.cpp | $(BUILDDIR)
	$(CXX) $(CXXFLAGS) -c $< -o $@

$(BUILDDIR):
	mkdir -p $(BUILDDIR)

clean:
	rm -rf $(BUILDDIR) $(TARGET) *.tmp.s src/runtime.o

.PHONY: all clean
