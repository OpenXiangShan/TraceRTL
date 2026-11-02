/***************************************************************************************
* Copyright (c) 2020-2023 Institute of Computing Technology, Chinese Academy of Sciences
* Copyright (c) 2020-2021 Peng Cheng Laboratory
*
* DiffTest is licensed under Mulan PSL v2.
* You can use this software according to the terms and conditions of the Mulan PSL v2.
* You may obtain a copy of Mulan PSL v2 at:
*          http://license.coscl.org.cn/MulanPSL2
*
* THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND,
* EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT,
* MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
*
* See the Mulan PSL v2 for more details.
***************************************************************************************/

#include "emu.h"
#include "device.h"
#include "ram.h"
#include "tracertl.h"
#include <algorithm>
#include <ctime>
#include <getopt.h>
#include <stdexcept>
#include <sys/resource.h>

static uint64_t parse_and_update_ramsize(const char *arg_ramsize_str) {
  unsigned long ram_size_value = 0;
  char ram_size_unit[64] = {};
  sscanf(arg_ramsize_str, "%ld%s", &ram_size_value, &ram_size_unit[0]);
  assert(ram_size_value > 0);

  if (!strcmp(ram_size_unit, "GB") || !strcmp(ram_size_unit, "gb")) {
    return ram_size_value * 1024 * 1024 * 1024;
  }
  if (!strcmp(ram_size_unit, "MB") || !strcmp(ram_size_unit, "mb")) {
    return ram_size_value * 1024 * 1024;
  }
  printf("Invalid ram size %s\n", ram_size_unit);
  return 0;
}

static inline long long int atoll_strict(const char *str, const char *arg) {
  if (strspn(str, " +-0123456789") != strlen(str)) {
    printf("[ERROR] --%s=NUM only accept numeric argument\n", arg);
    exit(EINVAL);
  }
  return atoll(str);
}

static inline void print_help(const char *file) {
  printf("Usage: %s [OPTION...]\n", file);
  printf("\n");
  printf("  -s, --seed=NUM             use this seed\n");
  printf("  -C, --max-cycles=NUM       execute at most NUM cycles\n");
  printf("  -I, --max-instr=NUM        execute at most NUM committed trace instructions\n");
  printf("  -W, --warmup-instr=NUM     report warmup completion after NUM committed instructions\n");
  printf("  -i, --image=FILE           initialize RAM with FILE, default /dev/zero\n");
  printf("  -X, --fork-interval=NUM    fork every NUM ms when --enable-fork is on\n");
  printf("  -b, --log-begin=NUM        dump waveform from NUM th cycle\n");
  printf("  -e, --log-end=NUM          stop waveform dump at NUM th cycle\n");
  printf("      --dump-wave            dump waveform when cycle is in log range\n");
  printf("      --dump-wave-full       dump full waveform when cycle is in log range\n");
  printf("      --wave-path=FILE       dump waveform to FILE\n");
  printf("      --ram-size=SIZE        simulation memory size, for example 8GB / 128MB\n");
  printf("      --enable-fork          enable LightSSS fork support\n");
  printf("      --tracertl-file=NAME   load TraceRTL stream from NAME\n");
  printf("      --tracept-file=NAME    load TraceRTL page-table side file from NAME\n");
  printf("      --gen-paddr            generate physical addresses from virtual addresses\n");
  printf("      --fast-warmup=NUM      enable TraceRTL fast warmup for NUM instructions\n");
  printf("      --skip-traceinstr=NUM  skip the first NUM instructions in the trace\n");
  printf("  -h, --help                 print program help info\n");
  printf("\n");
}

static EmuArgs parse_args(int argc, const char *argv[]) {
  EmuArgs args;
  int long_index = 0;

  /* clang-format off */
  const struct option long_options[] = {
    { "dump-wave",        0, NULL,  0  },
    { "dump-wave-full",   0, NULL,  0  },
    { "wave-path",        1, NULL,  0  },
    { "ram-size",         1, NULL,  0  },
    { "enable-fork",      0, NULL,  0  },
    { "tracertl-file",    1, NULL,  0  },
    { "tracept-file",     1, NULL,  0  },
    { "gen-paddr",        0, NULL,  0  },
    { "fast-warmup",      1, NULL,  0  },
    { "skip-traceinstr",  1, NULL,  0  },
    { "seed",             1, NULL, 's' },
    { "max-cycles",       1, NULL, 'C' },
    { "fork-interval",    1, NULL, 'X' },
    { "max-instr",        1, NULL, 'I' },
    { "warmup-instr",     1, NULL, 'W' },
    { "image",            1, NULL, 'i' },
    { "log-begin",        1, NULL, 'b' },
    { "log-end",          1, NULL, 'e' },
    { "help",             0, NULL, 'h' },
    { 0,                   0, NULL,  0  }
  };
  /* clang-format on */

  int o;
  while ((o = getopt_long(argc, const_cast<char *const *>(argv), "-s:C:X:I:W:i:b:e:h", long_options, &long_index)) !=
         -1) {
    switch (o) {
      case 0:
        switch (long_index) {
          case 0:
            args.enable_waveform = true;
            continue;
          case 1:
            args.enable_waveform = true;
            args.enable_waveform_full = true;
            continue;
          case 2:
            args.wave_path = optarg;
            continue;
          case 3:
            args.ram_size = optarg;
            continue;
          case 4:
            args.enable_fork = true;
            continue;
          case 5:
            args.tracertl_file = optarg;
            continue;
          case 6:
            args.tracept_file = optarg;
            continue;
          case 7:
            args.enable_gen_paddr = true;
            continue;
          case 8:
            args.fast_warmup = true;
            args.fast_warmup_instr = atoll_strict(optarg, "fast-warmup");
            continue;
          case 9:
            args.skip_traceinstr = atoll_strict(optarg, "skip-traceinstr");
            continue;
          default:
            print_help(argv[0]);
            exit(0);
        }
      case 's':
        if (std::string(optarg) != "NO_SEED") {
          args.seed = atoll_strict(optarg, "seed");
          Info("Using seed = %d\n", args.seed);
        }
        break;
      case 'C':
        args.max_cycles = atoll_strict(optarg, "max-cycles");
        break;
      case 'X':
        args.fork_interval = atoll_strict(optarg, "fork-interval");
        break;
      case 'I':
        args.max_instr = atoll_strict(optarg, "max-instr");
        break;
      case 'W':
        args.warmup_instr = atoll_strict(optarg, "warmup-instr");
        break;
      case 'i':
        args.image = optarg;
        break;
      case 'b':
        args.log_begin = atoll_strict(optarg, "log-begin");
        break;
      case 'e':
        args.log_end = atoll_strict(optarg, "log-end");
        break;
      case 'h':
      default:
        print_help(argv[0]);
        exit(0);
    }
  }

  if (args.image == nullptr) {
    args.image = "/dev/zero";
  }
  if (args.fast_warmup && args.warmup_instr == static_cast<uint64_t>(-1)) {
    args.warmup_instr = args.fast_warmup_instr;
  }
  if (args.enable_waveform && args.enable_fork) {
    printf("--dump-wave should not exist with --enable-fork\n");
    exit(1);
  }
  args.enable_waveform = args.enable_waveform && !args.enable_fork;

  Verilated::commandArgs(argc, argv);
  return args;
}

static int output_stat(char *buf) {
  const char *path = getenv("SIDE_STAT_PATH");
  if (path == nullptr) {
    path = getenv("NOOP_HOME");
#ifdef NOOP_HOME
    if (path == nullptr) {
      path = NOOP_HOME;
    }
#endif
    assert(path != nullptr);
    return snprintf(buf, 1024, "%s/build/", path);
  }
  return snprintf(buf, 1024, "%s/", path);
}

Emulator::Emulator(int argc, const char *argv[])
  : dut_ptr(new VerilatedDutTop), cycles(0), trapCode(STATE_RUNNING), elapsed_time(uptime()) {
#if !defined(VERILATOR_VERSION_INTEGER) || VERILATOR_VERSION_INTEGER < 5026000
  const size_t emu_stack_size = 32 * 1024 * 1024;
  struct rlimit rlim;
  getrlimit(RLIMIT_STACK, &rlim);
  rlim.rlim_cur = emu_stack_size;
  if (setrlimit(RLIMIT_STACK, &rlim)) {
    printf("[warning] cannot set stack size. Large designs may cause SIGSEGV.\n");
  }
#endif

  args = parse_args(argc, argv);
  srand(args.seed);
  srand48(args.seed);
  Verilated::randSeed(args.seed);
  Verilated::randReset(2);

#if VM_TRACE == 1
  if (args.enable_waveform) {
    Verilated::traceEverOn(true);
#ifdef ENABLE_FST
    tfp = new VerilatedFstC;
#else
    tfp = new VerilatedVcdC;
#endif
    dut_ptr->trace(tfp, 99);
    if (args.wave_path != nullptr) {
      tfp->open(args.wave_path);
    } else {
      time_t now = time(nullptr);
      tfp->open(waveform_filename(now));
    }
  }
#endif

  if (args.tracertl_file == nullptr) {
    throw std::runtime_error("trace file not specified");
  }

  uint64_t ram_size = DEFAULT_EMU_RAM_SIZE;
  if (args.ram_size != nullptr) {
    ram_size = parse_and_update_ramsize(args.ram_size);
  }
  init_ram(args.image, ram_size);
  init_device();

  if (args.enable_fork) {
    lightsss = new LightSSS;
    FORK_PRINTF("enable fork debugging...\n")
  }

  init_tracefastsim(args.fast_warmup, args.fast_warmup_instr);
  init_traceicache(args.tracept_file);
  init_tracertl(args.tracertl_file, args.enable_gen_paddr, args.max_instr, args.skip_traceinstr);

  if (args.fast_warmup) {
    uint64_t squashed = tracertl_squashed_warmup_instrs();
    printf("Fast warmup squashed duplicate instructions: %lu\n", squashed);
    if (args.warmup_instr != static_cast<uint64_t>(-1)) {
      args.warmup_instr -= std::min(args.warmup_instr, squashed);
    }
    if (args.max_instr != static_cast<uint64_t>(-1)) {
      args.max_instr -= std::min(args.max_instr, squashed);
    }
  }

#ifndef TRACERTL_MODE
  dut_ptr->difftest_logCtrl_begin = args.log_begin;
  dut_ptr->difftest_logCtrl_end = args.log_end;
#endif

  reset_ncycles(args.reset_cycles);
}

Emulator::~Emulator() {
#if VM_TRACE == 1
  if (args.enable_waveform) {
    tfp->close();
  }
#endif

  display_trapinfo();
  finish_device();

  if (simMemory != nullptr) {
    simMemory->display_stats();
    delete simMemory;
    simMemory = nullptr;
  }

  elapsed_time = uptime() - elapsed_time;
  Info(ANSI_COLOR_BLUE "Guest cycle spent: %'" PRIu64 "\n" ANSI_COLOR_RESET, cycles);
  Info(ANSI_COLOR_BLUE "Committed trace instructions: %'" PRIu64 "\n" ANSI_COLOR_RESET, tracertl_committed_instrs());
  Info(ANSI_COLOR_BLUE "Host time spent: %'dms\n" ANSI_COLOR_RESET, elapsed_time);

  if (args.enable_fork && lightsss != nullptr) {
    if (!is_fork_child()) {
      bool keep_oldest_child = trapCode != STATE_LIMIT_EXCEEDED && trapCode != STATE_SIG && trapCode != STATE_TRACE_OVER;
      if (keep_oldest_child) {
        lightsss->wakeup_child(cycles);
      } else {
        lightsss->do_clear();
      }
    }
    delete lightsss;
    lightsss = nullptr;
  }

  delete dut_ptr;
}

inline void Emulator::reset_ncycles(size_t reset_cycles) {
  for (size_t i = 0; i < reset_cycles; i++) {
    dut_ptr->reset = 1;
    dut_ptr->clock = 1;
    dut_ptr->eval();

#if VM_TRACE == 1
    if (args.enable_waveform && args.enable_waveform_full && args.log_begin == 0) {
      tfp->dump(2 * i);
    }
#endif

    dut_ptr->clock = 0;
    dut_ptr->eval();

#if VM_TRACE == 1
    if (args.enable_waveform && args.enable_waveform_full && args.log_begin == 0) {
      tfp->dump(2 * i + 1);
    }
#endif

    dut_ptr->reset = 0;
  }
}

inline void Emulator::single_cycle() {

  dut_ptr->clock = 1;
  dut_ptr->eval();

#if VM_TRACE == 1
  if (args.enable_waveform && args.log_begin <= cycles && cycles <= args.log_end) {
    if (args.enable_waveform_full) {
      tfp->dump(2 * args.reset_cycles + 2 * cycles);
    } else {
      tfp->dump(cycles);
    }
  }
#endif

#ifndef TRACERTL_MODE
  if (dut_ptr->difftest_uart_out_valid) {
    printf("%c", dut_ptr->difftest_uart_out_ch);
    fflush(stdout);
  }
  if (dut_ptr->difftest_uart_in_valid) {
    extern uint8_t uart_getc();
    dut_ptr->difftest_uart_in_ch = uart_getc();
  }
#endif

  dut_ptr->clock = 0;
  dut_ptr->eval();

#if VM_TRACE == 1
  if (args.enable_waveform && args.enable_waveform_full && args.log_begin <= cycles && cycles <= args.log_end) {
    tfp->dump(2 * args.reset_cycles + 1 + 2 * cycles);
  }
#endif

  cycles++;
}

int Emulator::tick() {
#ifdef SHOW_SCREEN
  uint32_t now = uptime();
  if (now - lasttime_poll > 100) {
    poll_event();
    lasttime_poll = now;
  }
#endif

  if (args.max_cycles != static_cast<uint64_t>(-1) && cycles >= args.max_cycles) {
    trapCode = STATE_LIMIT_EXCEEDED;
    return trapCode;
  }

  uint64_t committed_instrs = tracertl_committed_instrs();
  if (args.max_instr != static_cast<uint64_t>(-1) && committed_instrs >= args.max_instr) {
    trapCode = STATE_LIMIT_EXCEEDED;
    return trapCode;
  }

  if (assert_count > 0) {
    Info("The simulation stopped. There might be some assertion failed.\n");
    tracertl_assert_dump();
    trapCode = STATE_ABORT;
    return trapCode;
  }
  if (signal_num != 0) {
    trapCode = STATE_SIG;
    return trapCode;
  }
#ifndef TRACERTL_MODE
  if (dut_ptr->difftest_exit) {
    if (dut_ptr->difftest_exit == static_cast<uint64_t>(-1)) {
      trapCode = STATE_SIM_EXIT;
    } else {
      Info("The simulation aborted via the top-level exit of 0x%lx.\n", dut_ptr->difftest_exit);
      trapCode = STATE_ABORT;
    }
    return trapCode;
  }
#endif

  if (!warmup_reported && args.warmup_instr != static_cast<uint64_t>(-1) && committed_instrs >= args.warmup_instr &&
      tracertl_fastsim_finished()) {
    warmup_reported = true;
    printf("Warmup finished. committed=%lu cycle=%lu\n", committed_instrs, cycles);
  }

  tracertl_prepare_read();
  if (tracertl_over()) {
    tracertl_success_dump();
    trapCode = STATE_TRACE_OVER;
    return trapCode;
  }
  tracertl_prepare_fastsim_memaddr();
  single_cycle();

#ifdef TRACERTL_FPGA
  tracertl_check_commit_fpga(cycles);
#else
  tracertl_check_commit(cycles);
  tracertl_check_drive();
#endif

  if (tracertl_stuck()) {
    eprintf("TraceRTL timed out at cycle %lu\n", cycles);
    tracertl_error_dump();
    trapCode = STATE_ABORT;
    return trapCode;
  }
  if (tracertl_error()) {
    eprintf("TraceRTL commit mismatch\n");
    tracertl_error_dump();
    trapCode = STATE_ABORT;
    return trapCode;
  }
#ifndef TRACERTL_FPGA
  if (tracertl_error_drive()) {
    eprintf("TraceRTL drive mismatch\n");
    tracertl_error_drive_dump();
    trapCode = STATE_ABORT;
    return trapCode;
  }
  if (tracertl_emu_conflict()) {
    eprintf("TraceRTL detected emulator conflict\n");
    tracertl_error_drive_dump();
    trapCode = STATE_ABORT;
    return trapCode;
  }
#endif
  if (tracertl_over()) {
    tracertl_success_dump();
    trapCode = STATE_TRACE_OVER;
    return trapCode;
  }

  if (args.enable_fork && !is_fork_child()) {
    static bool have_initial_fork = false;
    uint32_t now = uptime();
    if (!have_initial_fork || now - lasttime_snapshot > args.fork_interval) {
      have_initial_fork = true;
      lasttime_snapshot = now;
      switch (lightsss->do_fork()) {
        case FORK_ERROR:
          trapCode = STATE_ABORT;
          return trapCode;
        case FORK_CHILD:
          fork_child_init();
          break;
        default:
          break;
      }
    }
  }

  return trapCode;
}

int Emulator::is_finished() {
  return Verilated::gotFinish() || trapCode != STATE_RUNNING;
}

int Emulator::is_good() {
  return is_good_trap();
}

inline char *Emulator::timestamp_filename(time_t t, char *buf) {
  char buf_time[64];
  strftime(buf_time, sizeof(buf_time), "%F@%T", localtime(&t));
  int path_len = output_stat(buf);
  int time_len = snprintf(buf + path_len, 1024, "%s", buf_time);
  return buf + path_len + time_len;
}

inline char *Emulator::waveform_filename(time_t t) {
  static char buf[1024];
  char *p = timestamp_filename(t, buf);
#ifdef ENABLE_FST
  strcpy(p, ".fst");
#else
  strcpy(p, ".vcd");
#endif
  Info("dump wave to %s...\n", buf);
  return buf;
}

void Emulator::fork_child_init() {
#ifdef VERILATOR_VERSION_INTEGER
#if VERILATOR_VERSION_INTEGER >= 5016000
  dut_ptr->atClone();
#endif
#elif EMU_THREAD > 1
#ifdef VERILATOR_4_210
  dut_ptr->vlSymsp->__Vm_threadPoolp = new VlThreadPool(dut_ptr->contextp(), EMU_THREAD - 1, 0);
#else
  dut_ptr->__Vm_threadPoolp = new VlThreadPool(dut_ptr->contextp(), EMU_THREAD - 1, 0);
#endif
#endif

  FORK_PRINTF("fork child activated...\n")
}

void Emulator::display_trapinfo() {
  uint64_t committed_instrs = tracertl_committed_instrs();
  switch (trapCode) {
    case STATE_TRACE_OVER:
      eprintf(ANSI_COLOR_GREEN "TRACE FINISHED after %'" PRIu64 " cycles, %'" PRIu64 " committed instructions\n" ANSI_COLOR_RESET,
              cycles, committed_instrs);
      break;
    case STATE_LIMIT_EXCEEDED:
      eprintf(ANSI_COLOR_YELLOW "EXCEEDED LIMIT after %'" PRIu64 " cycles, %'" PRIu64 " committed instructions\n" ANSI_COLOR_RESET,
              cycles, committed_instrs);
      break;
    case STATE_SIM_EXIT:
      eprintf(ANSI_COLOR_GREEN "SIM EXIT after %'" PRIu64 " cycles, %'" PRIu64 " committed instructions\n" ANSI_COLOR_RESET,
              cycles, committed_instrs);
      break;
    case STATE_SIG:
      eprintf(ANSI_COLOR_YELLOW "STOPPED BY SIGNAL after %'" PRIu64 " cycles\n" ANSI_COLOR_RESET, cycles);
      break;
    case STATE_ABORT:
      eprintf(ANSI_COLOR_RED "ABORT after %'" PRIu64 " cycles, %'" PRIu64 " committed instructions\n" ANSI_COLOR_RESET,
              cycles, committed_instrs);
      break;
    case STATE_RUNNING:
      break;
    default:
      eprintf(ANSI_COLOR_RED "Unknown trap code: %d\n" ANSI_COLOR_RESET, trapCode);
      break;
  }
}
