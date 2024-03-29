#===============================================================================
# User Options
#===============================================================================

COMPILER    = clang
OPTIMIZE    = yes
DEBUG       = no
PROFILE     = no
SM_VERSION  = 80

#===============================================================================
# Program name & source code list
#===============================================================================

program = XSBench

source = \
Main.cu \
io.cu \
GridInit.cu \
XSutils.cu \
Materials.cu

obj = $(source:.cu=.o)

#===============================================================================
# Sets Flags
#===============================================================================

# Standard Flags
CFLAGS := 

# Linker Flags
LDFLAGS = -lm

# NVIDIA Compiler
ifeq ($(COMPILER),nvidia)
  CC = nvcc
  CFLAGS += -Xcompiler -Wall -Xcompiler -O3 -arch=sm_$(SM_VERSION) -std=c++14
endif
ifeq ($(COMPILER),clang)
  CC = clang++
  CFLAGS += -O3 -offload-arch=sm_$(SM_VERSION) -std=c++14
	LDFLAGS += -x cuda -lcudart
endif

# Debug Flags
ifeq ($(DEBUG),yes)
  CFLAGS += -g -G
  LDFLAGS  += -g -G
endif

# Profiling Flags
ifeq ($(PROFILE),yes)
  CFLAGS += -pg
  LDFLAGS  += -pg
endif

# Optimization Flags
ifeq ($(OPTIMIZE),yes)
  CFLAGS += -O3
endif

#===============================================================================
# Targets to Build
#===============================================================================

$(program): $(obj) XSbench_header.cuh Makefile
	$(CC) $(CFLAGS) $(obj) Simulation.o -o $@ $(LDFLAGS)

%.o: %.cu XSbench_header.cuh Makefile
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -rf $(program) $(obj)

edit:
	vim -p $(source) XSbench_header.cuh

run:
	./$(program)
