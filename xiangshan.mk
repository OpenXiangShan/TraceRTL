#***************************************************************************************
# TraceRTL on XiangShan (MODE=xiangshan)
#
# Elaborates the parent XiangShan workspace (NOOP_HOME) with --trace-rtl and
# builds the emu through XiangShan's own, unmodified difftest makefiles. Like a
# difftest emu build, except that the harness differs: EMU_CSRC_DIR points at a
# staged directory holding difftest's Emulator, TraceRTL's xiangshan/main.cpp
# (in place of difftest's emu/main.cpp) and the TraceRTL trace sources.
#
#   make MODE=xiangshan SIM=gsim emu        # GSIM (default)
#   make MODE=xiangshan SIM=verilator emu   # Verilator
#***************************************************************************************

override NOOP_HOME := $(abspath $(NOOP_HOME))
export NOOP_HOME

SIM ?= gsim

CONFIG ?= DefaultConfig
NUM_CORES ?= 1
WITH_DRAMSIM3 ?= 0
WITH_CHISELDB ?= 0
WITH_CONSTANTIN ?= 0
EMU_THREADS ?= 0
EMU_OPTIMIZE ?= -O3
GSIM_BIN ?= gsim
# extra XiangShan SIM_ARGS, on top of --trace-rtl
XS_SIM_ARGS ?=

XS_BUILD_DIR := $(NOOP_HOME)/build
XS_RTL_DIR := $(XS_BUILD_DIR)/rtl
DIFFTEST_HOME := $(NOOP_HOME)/difftest
EMU := $(XS_BUILD_DIR)/emu

TRACERTL_CSRC_DIR := $(TRACERTL_HOME)/src/test/csrc/tracertl
TRACERTL_XS_CSRC_DIR := $(TRACERTL_HOME)/src/test/csrc/xiangshan
DIFFTEST_EMU_CSRC_DIR := $(DIFFTEST_HOME)/src/test/csrc/emu
TRACERTL_EMU_CSRC_DIR := $(XS_BUILD_DIR)/tracertl-csrc
TRACERTL_EMU_CSRC := \
  $(filter-out %/main.cpp,$(wildcard $(DIFFTEST_EMU_CSRC_DIR)/*.cpp $(DIFFTEST_EMU_CSRC_DIR)/*.h)) \
  $(wildcard $(TRACERTL_XS_CSRC_DIR)/*.cpp $(TRACERTL_XS_CSRC_DIR)/*.h) \
  $(wildcard $(TRACERTL_CSRC_DIR)/*.cpp $(TRACERTL_CSRC_DIR)/*.h)

# Variables given to this make are not passed down: XiangShan and difftest only
# see what is listed below.
MAKEOVERRIDES :=

XS_ARGS = CONFIG=$(CONFIG) NUM_CORES=$(NUM_CORES) \
  WITH_DRAMSIM3=$(WITH_DRAMSIM3) $(if $(DRAMSIM3_HOME),DRAMSIM3_HOME=$(DRAMSIM3_HOME)) \
  WITH_CHISELDB=$(WITH_CHISELDB) WITH_CONSTANTIN=$(WITH_CONSTANTIN) \
  SIM_ARGS="--trace-rtl $(XS_SIM_ARGS)"

# ChiselDB is always linked: XiangShan's PerfCCT and ChiselDB DPI-C calls are in
# the RTL even when the tables are disabled.
DIFFTEST_ARGS = SIM_TOP=SimTop DESIGN_DIR=$(NOOP_HOME) NUM_CORES=$(NUM_CORES) RTL_SUFFIX=sv \
  EMU_CSRC_DIR=$(TRACERTL_EMU_CSRC_DIR) \
  WITH_CHISELDB=1 WITH_CONSTANTIN=$(WITH_CONSTANTIN) \
  WITH_DRAMSIM3=$(WITH_DRAMSIM3) $(if $(DRAMSIM3_HOME),DRAMSIM3_HOME=$(DRAMSIM3_HOME)) \
  EMU_THREADS=$(EMU_THREADS) EMU_OPTIMIZE="$(EMU_OPTIMIZE)"

.DEFAULT_GOAL := emu

stage-csrc:
	@rm -rf $(TRACERTL_EMU_CSRC_DIR) && mkdir -p $(TRACERTL_EMU_CSRC_DIR)
	@ln -s $(TRACERTL_EMU_CSRC) $(TRACERTL_EMU_CSRC_DIR)/

#---------------------------------------------------------------------------------------
# GSIM: CHIRRTL -> GSIM C++ model -> emu
#---------------------------------------------------------------------------------------

GSIM_EMU_BUILD_DIR := $(XS_BUILD_DIR)/gsim-compile
GSIM_EMU_TARGET := $(GSIM_EMU_BUILD_DIR)/emu
GSIM_MODEL_STAMP := $(GSIM_EMU_BUILD_DIR)/.tracertl-model
GSIM_DIFFTEST_ARGS = GSIM=1 GSIM_BIN=$(GSIM_BIN) $(DIFFTEST_ARGS)

# XiangShan's sim-chirrtl and sim-verilog only re-elaborate when the Scala
# sources change, so RTL left by a build with other arguments (an exec build
# without --trace-rtl, the other simulator) would be reused as is. Remove it
# whenever the elaboration arguments differ from the last TraceRTL build.
ELAB_STAMP := $(XS_BUILD_DIR)/.tracertl-elab

define elab-guard
	@if [ "$$(cat $(ELAB_STAMP) 2>/dev/null)" != '$(1) $(XS_ARGS)' ]; then \
		echo "[TraceRTL] removing $(XS_RTL_DIR): not elaborated by this TraceRTL build"; \
		rm -rf $(XS_RTL_DIR) $(XS_BUILD_DIR)/generated-src $(ELAB_STAMP); \
	fi
endef

define elab-stamp
	@echo '$(1) $(XS_ARGS)' > $(ELAB_STAMP)
endef

gsim-rtl:
	$(call elab-guard,gsim)
	$(MAKE) -C $(NOOP_HOME) sim-chirrtl GSIM=1 $(XS_ARGS)
	$(call elab-stamp,gsim)

# difftest's gsim.mk regenerates the model into the old directory and only
# depends on the .fir, so start from scratch whenever the .fir is newer than
# the last complete model. Run GSIM under pipefail so that a failure is not
# hidden by its tee.
gsim-model: gsim-rtl stage-csrc
	@if [ ! -f $(GSIM_MODEL_STAMP) ] || [ $(XS_RTL_DIR)/SimTop.fir -nt $(GSIM_MODEL_STAMP) ]; then \
		echo "[TraceRTL] removing stale $(GSIM_EMU_BUILD_DIR)"; \
		rm -rf $(GSIM_EMU_BUILD_DIR); \
	fi
	$(MAKE) -C $(DIFFTEST_HOME) gsim-gen-cpp $(GSIM_DIFFTEST_ARGS) SHELL=/bin/bash .SHELLFLAGS="-o pipefail -c"
	@touch $(GSIM_MODEL_STAMP)

# Objects outside the model do not depend on headers in gsim.mk either.
gsim-emu: gsim-model
	@if [ -e $(GSIM_EMU_TARGET) ] && [ -n "$$(find -L $(TRACERTL_EMU_CSRC_DIR) $(DIFFTEST_HOME)/src/test/csrc $(DIFFTEST_HOME)/config \
		-name '*.h' -newer $(GSIM_EMU_TARGET) | head -1)" ]; then \
		echo "[TraceRTL] headers changed, removing $(GSIM_EMU_BUILD_DIR)/other"; \
		rm -rf $(GSIM_EMU_BUILD_DIR)/other; \
	fi
	$(MAKE) -C $(DIFFTEST_HOME) emu $(GSIM_DIFFTEST_ARGS)

#---------------------------------------------------------------------------------------
# Verilator: SystemVerilog -> Verilator -> emu
#---------------------------------------------------------------------------------------

# difftest's Verilator flags (OPT_FAST) are for clang; Verilator's own
# makefile defaults to the compiler it was configured with.
VERILATOR_CXX ?= clang++

verilator-rtl:
	$(call elab-guard,verilator)
	$(MAKE) -C $(NOOP_HOME) sim-verilog $(XS_ARGS)
	$(call elab-stamp,verilator)

verilator-emu: verilator-rtl stage-csrc
	$(MAKE) -C $(DIFFTEST_HOME) emu $(DIFFTEST_ARGS) CXX=$(VERILATOR_CXX) LINK=$(VERILATOR_CXX)

ifeq ($(SIM),gsim)
emu: gsim-emu
else ifeq ($(SIM),verilator)
emu: verilator-emu
else
$(error Unsupported SIM=$(SIM), expected gsim or verilator)
endif

.PHONY: stage-csrc gsim-rtl gsim-model gsim-emu verilator-rtl verilator-emu emu
