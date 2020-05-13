// Copyright 2016 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include "kernel/dpc.h"

#include <assert.h>
#include <err.h>
#include <trace.h>
#include <zircon/listnode.h>

#include <kernel/event.h>
#include <kernel/percpu.h>
#include <kernel/spinlock.h>
#include <lk/init.h>

#define DPC_THREAD_PRIORITY HIGH_PRIORITY

static spin_lock_t dpc_lock = SPIN_LOCK_INITIAL_VALUE;

zx_status_t Dpc::Queue(bool reschedule) {
  DEBUG_ASSERT(func_);

  // disable interrupts before finding lock
  spin_lock_saved_state_t state;
  spin_lock_irqsave(&dpc_lock, state);

  if (InContainer()) {
    spin_unlock_irqrestore(&dpc_lock, state);
    return ZX_ERR_ALREADY_EXISTS;
  }

  struct percpu* cpu = get_local_percpu();

  // put the dpc at the tail of the list and signal the worker
  cpu->dpc_list.push_back(this);

  spin_unlock_irqrestore(&dpc_lock, state);

  cpu->dpc_event.SignalEtc(reschedule);

  return ZX_OK;
}

zx_status_t Dpc::QueueThreadLocked() {
  DEBUG_ASSERT(func_);

  // interrupts are already disabled
  spin_lock(&dpc_lock);

  if (InContainer()) {
    spin_unlock(&dpc_lock);
    return ZX_ERR_ALREADY_EXISTS;
  }

  struct percpu* cpu = get_local_percpu();

  // put the dpc at the tail of the list and signal the worker
  cpu->dpc_list.push_back(this);
  cpu->dpc_event.SignalThreadLocked();

  spin_unlock(&dpc_lock);

  return ZX_OK;
}

void Dpc::Invoke() {
  if (func_)
    func_(this);
}

zx_status_t DpcSystem::Shutdown(uint cpu_id, zx_time_t deadline) {
  DEBUG_ASSERT(cpu_id < SMP_MAX_CPUS);

  spin_lock_saved_state_t state;
  spin_lock_irqsave(&dpc_lock, state);

  auto& percpu = percpu::Get(cpu_id);
  DEBUG_ASSERT(!percpu.dpc_stop);

  // Ask the DPC thread to terminate.
  percpu.dpc_stop = true;

  // Take the thread pointer so we can join outside the spinlock.
  Thread* t = percpu.dpc_thread;
  percpu.dpc_thread = nullptr;

  spin_unlock_irqrestore(&dpc_lock, state);

  // Wake it.
  percpu.dpc_event.SignalNoResched();

  // Wait for it to terminate.
  return t->Join(nullptr, deadline);
}

void DpcSystem::ShutdownTransitionOffCpu(uint cpu_id) {
  DEBUG_ASSERT(cpu_id < SMP_MAX_CPUS);

  spin_lock_saved_state_t state;
  spin_lock_irqsave(&dpc_lock, state);

  uint cur_cpu = arch_curr_cpu_num();
  DEBUG_ASSERT(cpu_id != cur_cpu);

  auto& percpu = percpu::Get(cpu_id);
  // The DPC thread should already be stopped.
  DEBUG_ASSERT(percpu.dpc_stop);
  DEBUG_ASSERT(percpu.dpc_thread == nullptr);

  fbl::DoublyLinkedList<Dpc*>& src_list = percpu.dpc_list;
  fbl::DoublyLinkedList<Dpc*>& dst_list = percpu::Get(cur_cpu).dpc_list;

  // Move the contents of src_list to the back of dst_list.
  auto back = dst_list.end();
  dst_list.splice(back, src_list);

  // Reset the state so we can restart DPC processing if the CPU comes back online.
  percpu.dpc_event.Unsignal();
  DEBUG_ASSERT(percpu.dpc_list.is_empty());
  percpu.dpc_stop = false;
  percpu.dpc_initialized = false;

  spin_unlock_irqrestore(&dpc_lock, state);
}

int DpcSystem::WorkerThread(void* arg) {
  spin_lock_saved_state_t state;
  arch_interrupt_save(&state, SPIN_LOCK_FLAG_INTERRUPTS);

  struct percpu* cpu = get_local_percpu();
  Event* event = &cpu->dpc_event;
  fbl::DoublyLinkedList<Dpc*>& list = cpu->dpc_list;

  arch_interrupt_restore(state, SPIN_LOCK_FLAG_INTERRUPTS);

  for (;;) {
    // wait for a dpc to fire
    __UNUSED zx_status_t err = event->Wait();
    DEBUG_ASSERT(err == ZX_OK);

    spin_lock_irqsave(&dpc_lock, state);

    if (cpu->dpc_stop) {
      spin_unlock_irqrestore(&dpc_lock, state);
      return 0;
    }

    // pop a dpc off the list, make a local copy.
    Dpc* dpc = list.pop_front();

    // if the list is now empty, unsignal the event so we block until it is
    if (!dpc) {
      event->Unsignal();
      spin_unlock_irqrestore(&dpc_lock, state);
      continue;
    }

    // Copy the dpc to the stack.
    Dpc dpc_local = *dpc;

    spin_unlock_irqrestore(&dpc_lock, state);

    // Call the dpc.
    dpc_local.Invoke();
  }

  return 0;
}

void DpcSystem::InitForCpu() {
  struct percpu* cpu = get_local_percpu();
  uint cpu_num = arch_curr_cpu_num();

  // the cpu's dpc state was initialized on a previous hotplug event
  if (cpu->dpc_initialized) {
    return;
  }

  cpu->dpc_initialized = true;
  cpu->dpc_stop = false;

  char name[10];
  snprintf(name, sizeof(name), "dpc-%u", cpu_num);
  cpu->dpc_thread = Thread::Create(name, &DpcSystem::WorkerThread, nullptr, DPC_THREAD_PRIORITY);
  DEBUG_ASSERT(cpu->dpc_thread != nullptr);
  cpu->dpc_thread->SetCpuAffinity(cpu_num_to_mask(cpu_num));

  // The DPC thread may use up to 150us out of every 300us (i.e. 50% of the CPU)
  // in the worst case. DPCs usually take only a small fraction of this and have
  // a much lower frequency than 3.333KHz.
  // TODO(38571): Make this runtime tunable. It may be necessary to change the
  // DPC deadline params later in boot, after configuration is loaded somehow.
  cpu->dpc_thread->SetDeadline({ZX_USEC(150), ZX_USEC(300), ZX_USEC(300)});

  cpu->dpc_thread->Resume();
}

static void dpc_init(unsigned int level) {
  // Initialize dpc for the main CPU.
  DpcSystem::InitForCpu();
}

LK_INIT_HOOK(dpc, dpc_init, LK_INIT_LEVEL_THREADING)
