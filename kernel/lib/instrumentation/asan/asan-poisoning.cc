// Copyright 2020 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <align.h>
#include <platform.h>
#include <string.h>
#include <zircon/assert.h>

#include <ktl/algorithm.h>
#include <ktl/atomic.h>
#include <ktl/span.h>
#include <sanitizer/asan_interface.h>
#include <vm/pmm.h>

#include "asan-internal.h"

ktl::atomic<bool> g_asan_initialized;

constexpr size_t kAsanGranularity = (1 << kAsanShift);
constexpr size_t kAsanGranularityMask = kAsanGranularity - 1;

namespace {

// Checks if an entire memory region is all zeroes.
bool is_mem_zero(ktl::span<const uint8_t> region) {
  for (auto val : region) {
    if (val != 0) {
      return false;
    }
  }
  return true;
}

// When kASAN has detected an invalid access, print information about the access and the
// corresponding parts of the shadow map. Also print PMM page state.
//
// Example:
// (Shadow address)        (shadow map contents)
//
// KASAN detected an error: ptr={{{data:0xffffff8043251830}}}, size=0x4, caller:
// {{{pc:0xffffffff001d9371}}} Shadow memory state around the buggy address 0xffffffe00864a306:
// 0xffffffe00864a2f0: 0xfa 0xfa 0xfa 0xfa 0xfa 0xfa 0xfa 0xfa
// 0xffffffe00864a2f8: 0xfa 0xfa 0xfa 0xfa 0xfa 0xfa 0xfa 0xfa
// 0xffffffe00864a300: 0xfa 0xfa 0xfa 0xfa 0xfa 0xfa 0xfa 0xfa
//                                                    ^^
// 0xffffffe00864a308: 0xfa 0xfa 0xfa 0xfa 0xfa 0xfa 0xfa 0xfa
// 0xffffffe00864a310: 0xfa 0xfa 0xfa 0xfa 0xfa 0xfa 0xfa 0xfa
// page 0xffffff807f475f30: address 0x43251000 state heap flags 0
void print_error_shadow(uintptr_t address, size_t bytes, void* caller, uintptr_t poisoned_addr) {
  const uintptr_t shadow = reinterpret_cast<uintptr_t>(addr2shadow(address));

  dprintf(CRITICAL,
          "\nKASAN detected an error: ptr={{{data:%#lx}}}, size=%#zx, caller: {{{pc:%p}}}\n",
          address, bytes, caller);

  // TODO(30033): Decode the shadow value into 'use-after-free'/redzone/page free/etc.
  printf("Shadow memory state around the buggy address %#lx:\n", shadow);
  // Print at least 16 bytes of the shadow map before and after the invalid access.
  uintptr_t start_addr = (shadow & ~0x07) - 0x10;
  start_addr = ktl::max(KASAN_SHADOW_OFFSET, start_addr);
  // Print the shadow map memory state and look for the location to print a caret.
  bool caret = false;
  size_t caret_ind = 0;
  for (size_t i = 0; i < 5; i++) {
    // TODO(fxb/51170): When kernel printf properly supports #, switch.
    printf("0x%016lx:", start_addr);
    for (size_t j = 0; j < 8; j++) {
      printf(" 0x%02hhx", reinterpret_cast<uint8_t*>(start_addr)[j]);
      if (!caret) {
        if ((start_addr + j) == reinterpret_cast<uintptr_t>(addr2shadow(poisoned_addr))) {
          caret = true;
          caret_ind = j;
        }
      }
    }
    printf("\n");
    if (caret) {
      // The address takes 16 characters; add in space for ':', and "0x".
      printf("%*s", 16 + 1 + 2, "");
      // Print either a caret or spaces under the line containing the invalid access.
      for (size_t j = 0; j < 8; j++) {
        printf("  %2s ", j == caret_ind ? "^^" : "");
      }
      printf("\n");
      caret = false;
    }
    start_addr += 8;
  }
  // Dump additional VM Page state - this is useful to debug use-after-state-change bugs.
  paddr_to_vm_page(vaddr_to_paddr(reinterpret_cast<void*>(address)))->dump();
}

inline bool RangesOverlap(uintptr_t offset1, size_t len1, uintptr_t offset2, size_t len2) {
  return !((offset1 + len1 <= offset2) || (offset2 + len2 <= offset1));
}

}  // namespace

// Checks whether a memory |address| is poisoned.
//
// ASAN tracks address poison status at byte granularity in a shadow map.
// kAsanGranularity bytes are represented by one byte in the shadow map.
//
// If the value in the shadow map is 0, accesses to address are allowed.
// If the value is in [1, kAsanGranularity), accesses to the corresponding
// addresses less than the value are allowed.
// All other values disallow access to the entire aligned region.
bool asan_address_is_poisoned(uintptr_t address) {
  const uint8_t shadow_val = *addr2shadow(address);
  // Zero values in the shadow map mean that all 8 bytes are valid.
  if (shadow_val == 0) {
    return false;
  }
  if (shadow_val >= kAsanSmallestPoisonedValue) {
    return true;
  }
  // Part of this region is poisoned. Check whether address is below the last valid byte.
  const size_t offset = address & kAsanGranularityMask;
  return shadow_val <= offset;
}

uintptr_t asan_region_is_poisoned(uintptr_t address, size_t size) {
  const uintptr_t end = address + size;
  const uintptr_t aligned_begin = ROUNDUP(address, kAsanGranularity);
  const uintptr_t aligned_end = ROUNDDOWN(end, kAsanGranularity);
  const uint8_t* const shadow_beg = addr2shadow(aligned_begin);
  const uint8_t* const shadow_end = addr2shadow(aligned_end);

  if (!asan_address_is_poisoned(address) && !asan_address_is_poisoned(end - 1) &&
      (shadow_end <= shadow_beg ||
       is_mem_zero({shadow_beg, static_cast<size_t>(shadow_end - shadow_beg)}))) {
    return 0;
  }

  for (size_t i = 0; i < size; i++) {
    if (asan_address_is_poisoned(address + i)) {
      return address + i;
    }
  }
  panic("Unreachable code\n");
}

void asan_check(uintptr_t address, size_t bytes, void* caller) {
  // TODO(30033): Inline the fast path for constant-size checks.
  const uintptr_t poisoned_addr = asan_region_is_poisoned(address, bytes);
  if (!poisoned_addr) {
    return;
  }
  platform_panic_start();
  print_error_shadow(address, bytes, caller, poisoned_addr);
  panic("kasan\n");
}

void asan_check_memory_overlap(uintptr_t offset1, size_t len1, uintptr_t offset2, size_t len2) {
  if (!RangesOverlap(offset1, len1, offset2, len2))
    return;
  platform_panic_start();
  printf("KASAN detected a memory range overlap error.\n");
  printf("ptr: 0x%016lx size: %#zx overlaps with ptr: 0x%016lx size: %#zx\n", offset1, len1,
         offset2, len2);
  panic("kasan\n");
}

void asan_poison_shadow(uintptr_t address, size_t size, uint8_t value) {
  // pmm_alloc_page is called before the kasan shadow map has been remapped r/w.
  // Do not attempt to poison memory in that case.
  if (!g_asan_initialized.load()) {
    return;
  }

  DEBUG_ASSERT(IS_ALIGNED(address, kAsanGranularity));
  DEBUG_ASSERT(address >= kAsanStartAddress);
  DEBUG_ASSERT(address + size < kAsanEndAddress);

  uint8_t* const shadow_addr = addr2shadow(address);
  __unsanitized_memset(shadow_addr, value, size >> kAsanShift);

  // The last shadow byte should capture how many bytes
  // are unpoisoned at the end of the poisoned area.
  const uint8_t remaining = size & kAsanGranularityMask;
  if (remaining != 0 && value == 0) {
    shadow_addr[size >> kAsanShift] = remaining;
  }
}
