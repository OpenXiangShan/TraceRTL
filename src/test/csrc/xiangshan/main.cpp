/***************************************************************************************
* TraceRTL emu entry for XiangShan designs.
*
* This file replaces difftest's emu/main.cpp. The TraceRTL options are consumed
* here and everything else is handed to the unmodified difftest Emulator. The
* trace is driven around Emulator::tick(), at the same points the TraceRTL hooks
* used to take inside tick(): right before and right after single_cycle().
***************************************************************************************/

#include <algorithm>
#include <cerrno>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <getopt.h>
#include <string>
#include <vector>

#include "args.h"
#include "common.h"
#include "config.h"
#include "dut.h"
#include "emu.h"
#include "tracertl.h"

// Trace programs start at the bare-metal reset vector.
#define TRACERTL_FIRST_INST_ADDRESS 0x80000000UL

struct TraceArgs {
  const char *tracertl_file = nullptr;
  const char *tracept_file = nullptr;
  bool enable_gen_paddr = false;
  bool fast_warmup = false;
  uint64_t fast_warmup_instr = 0;
  uint64_t skip_traceinstr = 0;
};

static uint64_t parse_num(const char *str, const char *opt) {
  char *end = nullptr;
  errno = 0;
  unsigned long long value = strtoull(str, &end, 10);
  if (errno != 0 || end == str || *end != '\0') {
    printf("[ERROR] --%s=NUM only accept numeric argument\n", opt);
    exit(EINVAL);
  }
  return value;
}

// Matches "--name", "--name=VALUE" or "--name VALUE" at argv[i]. On a match,
// stores the value (if any), advances i past it and returns true.
static bool match_option(int argc, const char *argv[], int &i, const char *name, bool has_arg, const char *&value) {
  const char *arg = argv[i];
  size_t len = strlen(name);
  if (strncmp(arg, "--", 2) != 0 || strncmp(arg + 2, name, len) != 0) {
    return false;
  }
  const char *rest = arg + 2 + len;
  if (!has_arg) {
    return *rest == '\0';
  }
  if (*rest == '=') {
    value = rest + 1;
    return true;
  }
  if (*rest != '\0') {
    return false;
  }
  if (i + 1 >= argc) {
    printf("[ERROR] --%s requires an argument\n", name);
    exit(EINVAL);
  }
  value = argv[++i];
  return true;
}

// Removes the TraceRTL options from argv; the remaining ones go to the Emulator.
static TraceArgs strip_trace_args(int argc, const char *argv[], std::vector<const char *> &rest) {
  TraceArgs trace;
  rest.push_back(argv[0]);
  for (int i = 1; i < argc; i++) {
    const char *value = nullptr;
    if (strcmp(argv[i], "--") == 0) {
      rest.insert(rest.end(), argv + i, argv + argc);
      break;
    } else if (match_option(argc, argv, i, "tracertl-file", true, value)) {
      trace.tracertl_file = value;
    } else if (match_option(argc, argv, i, "tracept-file", true, value)) {
      trace.tracept_file = value;
    } else if (match_option(argc, argv, i, "gen-paddr", false, value)) {
      trace.enable_gen_paddr = true;
    } else if (match_option(argc, argv, i, "fast-warmup", true, value)) {
      trace.fast_warmup = true;
      trace.fast_warmup_instr = parse_num(value, "fast-warmup");
    } else if (match_option(argc, argv, i, "skip-traceinstr", true, value)) {
      trace.skip_traceinstr = parse_num(value, "skip-traceinstr");
    } else {
      rest.push_back(argv[i]);
    }
  }
  return trace;
}

// Runs one more tick to let the Emulator stop through its own checks. With
// signal_num or assert_count raised, tick() returns before simulating a cycle,
// unless a cycle/instruction limit is reached first, exactly as before.
static void stop_emulator(Emulator *emu) {
  if (!emu->is_finished()) {
    emu->tick();
  }
}

int main(int argc, const char *argv[]) {
  common_init_without_assertion(argv[0]);

  std::vector<const char *> emu_argv;
  TraceArgs trace = strip_trace_args(argc, argv, emu_argv);
  if (trace.tracertl_file == nullptr) {
    printf("[ERROR] trace file not specified; use --tracertl-file=NAME\n");
    return EINVAL;
  }

  // The Emulator keeps its parsed arguments private, so peek at the limits on a
  // copy of argv (getopt permutes it) and rewind getopt for the Emulator.
  std::vector<const char *> peek_argv(emu_argv);
  CommonArgs args = parse_args(peek_argv.size(), peek_argv.data());
  optind = 1;
  if (trace.fast_warmup && args.warmup_instr == static_cast<uint64_t>(-1)) {
    args.warmup_instr = trace.fast_warmup_instr;
  }

  FIRST_INST_ADDRESS = TRACERTL_FIRST_INST_ADDRESS;
  init_tracefastsim(trace.fast_warmup, trace.fast_warmup_instr);
  init_traceicache(trace.tracept_file);
  init_tracertl(trace.tracertl_file, trace.enable_gen_paddr, args.max_instr, trace.skip_traceinstr);

  // Fast warmup squashes duplicate instructions out of the trace, so the
  // Emulator's instruction limits shrink accordingly. Later options override
  // earlier ones in getopt, so the adjusted limits are appended.
  static std::string warmup_instr, max_instr;
  if (trace.fast_warmup) {
    uint64_t squashed = tracertl_squashed_warmup_instrs();
    printf("Fast warmup squashed duplicate instructions: %lu\n", squashed);
    if (args.warmup_instr != static_cast<uint64_t>(-1)) {
      warmup_instr = std::to_string(args.warmup_instr - std::min(args.warmup_instr, squashed));
      emu_argv.push_back("-W");
      emu_argv.push_back(warmup_instr.c_str());
    }
    if (args.max_instr != static_cast<uint64_t>(-1)) {
      max_instr = std::to_string(args.max_instr - std::min(args.max_instr, squashed));
      emu_argv.push_back("-I");
      emu_argv.push_back(max_instr.c_str());
    }
  }

  // initialize the design-under-test (DUT)
  auto emu = new Emulator(emu_argv.size(), emu_argv.data());

  // allow assertions only after DUT resets
  common_enable_assert();

  bool trace_over = false;   // the whole trace has been committed
  bool trace_failed = false; // a TraceRTL check failed
  while (!emu->is_finished()) {
    tracertl_prepare_read();
    if (tracertl_over()) {
      signal_num = SIGTERM;
      stop_emulator(emu);
      if (emu->get_trapcode() == STATE_SIG) {
        tracertl_success_dump();
        trace_over = true;
      }
      break;
    }
    tracertl_prepare_fastsim_memaddr();

    uint64_t cycles = emu->get_cycles();
    emu->tick();
    if (emu->get_cycles() == cycles) {
      // tick() stopped before simulating a cycle
      continue;
    }
    cycles = emu->get_cycles();

    tracertl_check_commit(cycles);
    tracertl_check_drive();
    if (tracertl_stuck()) {
      eprintf("TraceRTL timed out at cycle %lu\n", cycles);
      tracertl_error_dump();
      trace_failed = true;
    } else if (tracertl_error()) {
      eprintf("TraceRTL commit mismatch\n");
      tracertl_error_dump();
      trace_failed = true;
    } else if (tracertl_error_drive()) {
      eprintf("TraceRTL drive mismatch\n");
      tracertl_error_drive_dump();
      trace_failed = true;
    } else if (tracertl_emu_conflict()) {
      eprintf("TraceRTL detected emulator conflict\n");
      tracertl_error_drive_dump();
      trace_failed = true;
    } else if (tracertl_over()) {
      tracertl_success_dump();
      trace_over = true;
      signal_num = SIGTERM;
      stop_emulator(emu);
      break;
    }
    if (trace_failed) {
      assert_count++;
      stop_emulator(emu);
      break;
    }
  }

  if (!trace_failed && emu->get_trapcode() == STATE_ABORT && assert_count > 0) {
    tracertl_assert_dump();
  }
  bool is_good = !trace_failed && (emu->is_good() || (trace_over && emu->get_trapcode() == STATE_SIG));
  delete emu;

  stats.display();

  common_finish();

  return !is_good;
}
