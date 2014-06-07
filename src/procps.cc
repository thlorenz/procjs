#include <node.h>
#include <nan.h>

#include "procps.h"
#include "proc/sysinfo.h"

#include <cassert>

using v8::FunctionTemplate;
using v8::Handle;
using v8::Local;
using v8::Object;
using v8::String;
using v8::Integer;
using v8::Uint32;
using v8::Array;
using v8::Value;
using v8::Function;

/*
 * readproc
 */

#define MAX_PROCS 5000

/*
  * By default readproc will consider all processes as valid to parse
  * and return, but not actually fill in the cmdline, environ, and /proc/#/statm
  * derived memory fields.
  *
  * `flags' (a bitwise-or of PROC_* below) modifies the default behavior.
  *
  * The "fill" options will cause more of the proc_t to be filled in.
  *
  * The "filter" options all use the second argument as the pointer to a list of objects:
  *    process status', process id's, user id's.
  *
  * The third argument is the length of the list (currently only used for lists of user
  * id's since uid_t supports no convenient termination sentinel.)
  */
static proc_t **get_proctab(uint32_t flags) {
  return readproctab(flags);
}

NAN_METHOD(Readproctab) {
  NanScope();

  // our JS API ensures we always pass flags (defaults if none were given)
  Local<Integer> flags = args[0].As<Integer>();
  assert(flags->IsUint32());

  NanCallback *cb = new NanCallback(args[1].As<Function>());

  proc_t **proctab = get_proctab(flags->Value());
  int len = -1;
  while(*(proctab + (++len)));

  assert(len <= MAX_PROCS && "exceeded MAX_PROCS");

  Local<Value> argv[MAX_PROCS];
  for (int i = 0; i < len; i++) {
    Proc *proc = new Proc(proctab[i]);
    argv[i] = proc->Wrap();
  }

  // Passing result by calling function with arguments instead of populating
  // and returning v8::Array since it's so much faster
  // This technique came from node: https://github.com/joyent/node/blob/5344d0c1034b28f9e6de914430d8c8436ad85105/src/node_file.cc#L326
  // (thanks @trevnorris for explaining it to me)
  cb->Call(len, argv);

  NanReturnUndefined();
}

/*
 * sysinfo
 */

NAN_METHOD(Sysinfo_Meminfo) {
  NanScope();

  Local<Integer> shiftArg = args[0].As<Integer>();
  assert(shiftArg->IsUint32());
  unsigned long long shift = shiftArg->Value();

  NanCallback *cb = new NanCallback(args[1].As<Function>());

  meminfo();

  #define X(field) NanNew<Uint32>((uint32_t) ((unsigned long long)(field) << 10) >> shift)

  Local<Value> argv[] = {
    /* all are unsigned long */
      X(kb_main_buffers)
    , X(kb_main_cached)
    , X(kb_main_free)
    , X(kb_main_total)
    , X(kb_swap_free)
    , X(kb_swap_total)
    /* recently introduced */
    , X(kb_high_free)
    , X(kb_high_total)
    , X(kb_low_free)
    , X(kb_low_total)
    /* 2.4.xx era */
    , X(kb_active)
    , X(kb_inact_laundry)
    , X(kb_inact_dirty)
    , X(kb_inact_clean)
    , X(kb_inact_target)
    , X(kb_swap_cached)  /* late 2.4 and 2.6+ only */
    /* derived values */
    , X(kb_swap_used)
    , X(kb_main_used)
    /* 2.5.41+ */
    , X(kb_writeback)
    , X(kb_slab)
    , X(nr_reversemaps)
    , X(kb_committed_as)
    , X(kb_dirty)
    , X(kb_inactive)
    , X(kb_mapped)
    , X(kb_pagetables)
  };
  #undef X

  cb->Call(sizeof(argv) / sizeof(argv[0]), argv);

  NanReturnUndefined();
}

NAN_METHOD(Sysinfo_Hertz) {
  NanScope();

  /* Herz is globally defined in sysinfo.c */
  NanReturnValue(NanNew<Uint32>((uint32_t) Hertz));
}

NAN_METHOD(Sysinfo_Getstat) {
  jiff cpu_use, cpu_nic, cpu_sys, cpu_idl, cpu_iow, cpu_xxx, cpu_yyy, cpu_zzz;
  unsigned long pgpgin, pgpgout, pswpin, pswpout;
  unsigned int intr, ctxt;
  unsigned int running, blocked, btime, processes;

  NanScope();

  NanCallback *cb = new NanCallback(args[0].As<Function>());

	getstat(&cpu_use, &cpu_nic, &cpu_sys, &cpu_idl,
		&cpu_iow, &cpu_xxx, &cpu_yyy, &cpu_zzz,
		&pgpgin, &pgpgout, &pswpin, &pswpout,
	  &intr, &ctxt,
	  &running, &blocked,
	  &btime, &processes);

  // FIXME: currently converting 64-bit unigned long long to 32-bit
  //        this works as long as the machine we are looking up hasn't been up for a long time
  //        need to figure out how to correct this by correctly passing 64-bit number to JS layer
  #define X(field) NanNew<Uint32>((uint32_t) field)
  Local<Value> argv[] = {
      X(cpu_use)
    , X(cpu_nic)
    , X(cpu_sys)
    , X(cpu_idl)
    , X(cpu_iow)
    , X(cpu_xxx)
    , X(cpu_yyy)
    , X(cpu_zzz)
    , X(pgpgin)
    , X(pgpgout)
    , X(pswpin)
    , X(pswpout)
    , X(intr)
    , X(ctxt)
    , X(running)
    , X(blocked)
    , X(btime)
    , X(processes)
  };
  #undef X

  cb->Call(sizeof(argv) / sizeof(argv[0]), argv);

  NanReturnUndefined();
}

NAN_METHOD(Sysinfo_GetDiskStat) {
  NanScope();
  NanCallback *cb = new NanCallback(args[0].As<Function>());

  disk_stat *disks;
  partition_stat *partitions;

  int ndisks = getdiskstat(&disks, &partitions);
  int npartitions = getpartitions_num(disks, ndisks);

  // disks
  Local<Array> wrapDisks = NanNew<Array>(ndisks);

  for (int i = 0; i < ndisks; i++) {
    DiskStat *stat = new DiskStat(disks[i]);
    wrapDisks->Set(i, stat->Wrap());
  }

  // partitions
  Local<Array> wrapPartitions = NanNew<Array>(npartitions);

  for (int i = 0; i < npartitions; i++) {
    PartitionStat *stat = new PartitionStat(partitions[i]);
    wrapPartitions->Set(i, stat->Wrap());
  }

  Local<Value> argv[] = { wrapDisks, wrapPartitions };

  cb->Call(sizeof(argv) / sizeof(argv[0]), argv);

  NanReturnUndefined();
}

void init(Handle<Object> exports) {
  exports->Set(NanNew<String>("readproctab"), NanNew<FunctionTemplate>(Readproctab)->GetFunction());
  exports->Set(NanNew<String>("sysinfo_meminfo"), NanNew<FunctionTemplate>(Sysinfo_Meminfo)->GetFunction());
  exports->Set(NanNew<String>("sysinfo_Hertz"), NanNew<FunctionTemplate>(Sysinfo_Hertz)->GetFunction());
  exports->Set(NanNew<String>("sysinfo_getstat"), NanNew<FunctionTemplate>(Sysinfo_Getstat)->GetFunction());
  exports->Set(NanNew<String>("sysinfo_getdiskstat"), NanNew<FunctionTemplate>(Sysinfo_GetDiskStat)->GetFunction());
}

NODE_MODULE(procps, init)
