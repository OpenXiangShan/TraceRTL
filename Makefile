#***************************************************************************************
# TraceRTL simulation makefile
#
#   MODE=standalone  the local Hollow DUT, built by verilator.mk (default)
#   MODE=xiangshan   the parent XiangShan workspace, built by xiangshan.mk
#***************************************************************************************

TRACERTL_HOME := $(abspath .)
NOOP_HOME ?= $(abspath ..)
# the XiangShan workspace, before standalone mode points NOOP_HOME at TraceRTL
XS_NOOP_HOME := $(NOOP_HOME)
MODE ?= standalone
LOCAL_GEN_CSRC_DIR := $(TRACERTL_HOME)/build/generated-src

STANDALONE_MILL_MODULE := standalone
STANDALONE_TOP_MODULE := HollowTop
STANDALONE_TOP_MAIN := hollow.HollowTop
STANDALONE_DUT_INFO_SRC := $(TRACERTL_HOME)/src/test/csrc/tracertl/tracertl_dut_info_standalone.h

help:
	@echo "Targets: sim-verilog emu emu-run clean standalone-emu standalone-emu-run xiangshan-emu xiangshan-emu-run"
	@echo "Variables: MODE=standalone|xiangshan TRACE_FILE=... TRACE_PT_FILE=... CONFIG=... NUM_CORES=... NOOP_HOME=... WAVE=1 WAVE_FULL=1 WAVE_PATH=... WAVE_FORMAT=vcd|fst EMU_EXTRA_ARGS=... VM_PARALLEL_BUILDS=1|2|4"
	@echo "MODE=xiangshan: SIM=gsim|verilator CONFIG=... WITH_DRAMSIM3=1 DRAMSIM3_HOME=... WITH_CHISELDB=1 WITH_CONSTANTIN=1 EMU_THREADS=... EMU_OPTIMIZE=... GSIM_BIN=... VERILATOR_CXX=clang++ XS_SIM_ARGS=..."

ifeq ($(MODE),standalone)
NOOP_HOME := $(TRACERTL_HOME)
ROOT_BUILD_DIR := $(TRACERTL_HOME)/build
BUILD_DIR ?= $(ROOT_BUILD_DIR)
RTL_DIR := $(ROOT_BUILD_DIR)/rtl
SIMTOP := $(STANDALONE_TOP_MAIN)
SIM_TOP ?= $(STANDALONE_TOP_MODULE)
EXTRA_SIM_CXXFLAGS += -DTRACERTL_STANDALONE_HOLLOW_TOP
SCALA_FILE := $(shell find $(TRACERTL_HOME)/src/main/scala -name '*.scala')
SIM_VERILOG_DEPS := $(SCALA_FILE) $(abspath ./build.sc) $(abspath ./.mill-version) $(LOCAL_GEN_CSRC_DIR)/tracertl_dut_info.h
MILL_GENERATE_CMD = cd $(TRACERTL_HOME) && NOOP_HOME=$(TRACERTL_HOME) mill -i $(STANDALONE_MILL_MODULE).runMain $(SIMTOP) --target-dir $(RTL_DIR)

RTL_SUFFIX ?= sv
NUM_CORES ?= 1
CONFIG ?= MinimalConfig
ISSUE ?= E.b

EMU := $(BUILD_DIR)/emu
SIM_TOP_V := $(RTL_DIR)/$(SIM_TOP).$(RTL_SUFFIX)

.DEFAULT_GOAL := emu

WAVE_FORMAT ?= vcd
VM_PARALLEL_BUILDS ?= 1

$(LOCAL_GEN_CSRC_DIR)/tracertl_dut_info.h: $(STANDALONE_DUT_INFO_SRC)
	@mkdir -p $(@D)
	cp $< $@


$(SIM_TOP_V): $(SIM_VERILOG_DEPS)
	@mkdir -p $(RTL_DIR) $(TRACERTL_HOME)/build/generated-src
	$(MILL_GENERATE_CMD)

sim-verilog: $(SIM_TOP_V)

include verilator.mk

emu: sim-verilog $(EMU)

else ifeq ($(MODE),xiangshan)
EMU_RUN_ARGS += --no-diff
include xiangshan.mk
else
$(error Unsupported MODE=$(MODE), expected standalone or xiangshan)
endif

emu-run: emu
ifndef TRACE_FILE
	$(error TRACE_FILE is required, e.g. make emu-run TRACE_FILE=/path/to/trace.zst)
endif
	$(EMU) $(EMU_RUN_ARGS) --tracertl-file $(TRACE_FILE) \
		$(if $(TRACE_PT_FILE),--tracept-file $(TRACE_PT_FILE),) \
		$(if $(MAX_CYCLES),--max-cycles=$(MAX_CYCLES),) \
		$(if $(MAX_INSTR),--max-instr=$(MAX_INSTR),) \
		$(if $(WARMUP_INSTR),--warmup-instr=$(WARMUP_INSTR),) \
		$(if $(FAST_WARMUP),--fast-warmup=$(FAST_WARMUP),) \
		$(if $(SKIP_TRACEINSTR),--skip-traceinstr=$(SKIP_TRACEINSTR),) \
		$(if $(GEN_PADDR),--gen-paddr,) \
		$(if $(WAVE),--dump-wave,) \
		$(if $(WAVE_FULL),--dump-wave-full,) \
		$(if $(WAVE_PATH),--wave-path=$(WAVE_PATH),) \
		$(EMU_EXTRA_ARGS)

standalone-emu:
	$(MAKE) MODE=standalone emu

standalone-emu-run:
	$(MAKE) MODE=standalone emu-run TRACE_FILE="$(TRACE_FILE)" TRACE_PT_FILE="$(TRACE_PT_FILE)" MAX_CYCLES="$(MAX_CYCLES)" MAX_INSTR="$(MAX_INSTR)" WARMUP_INSTR="$(WARMUP_INSTR)" FAST_WARMUP="$(FAST_WARMUP)" SKIP_TRACEINSTR="$(SKIP_TRACEINSTR)" GEN_PADDR="$(GEN_PADDR)" EMU_EXTRA_ARGS="$(EMU_EXTRA_ARGS)"

xiangshan-emu:
	$(MAKE) MODE=xiangshan emu NOOP_HOME="$(XS_NOOP_HOME)"

xiangshan-emu-run:
	$(MAKE) MODE=xiangshan emu-run NOOP_HOME="$(XS_NOOP_HOME)" TRACE_FILE="$(TRACE_FILE)" TRACE_PT_FILE="$(TRACE_PT_FILE)" MAX_CYCLES="$(MAX_CYCLES)" MAX_INSTR="$(MAX_INSTR)" WARMUP_INSTR="$(WARMUP_INSTR)" FAST_WARMUP="$(FAST_WARMUP)" SKIP_TRACEINSTR="$(SKIP_TRACEINSTR)" GEN_PADDR="$(GEN_PADDR)" EMU_EXTRA_ARGS="$(EMU_EXTRA_ARGS)"

clean:
	rm -rf $(TRACERTL_HOME)/build $(TRACERTL_HOME)/out

.PHONY: help sim-verilog emu emu-run clean standalone-emu standalone-emu-run xiangshan-emu xiangshan-emu-run
