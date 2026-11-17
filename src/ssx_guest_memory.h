#pragma once

// Safe reads of the game's memory from host code: heap scans, and pointers into data the game may
// have freed or not loaded yet.

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <vector>

#ifndef _WIN32
#include <sys/uio.h>
#include <unistd.h>
#endif

#include <rex/system/xmemory.h>

// Copies `size` bytes of this process's memory; false if any of it can't be read. On Linux the runtime
// can report guest memory as committed and readable while the host page behind it isn't (reading it
// raised SIGBUS in a heap scan, now and then), so reads of the game's heap go through here.
inline bool SsxCopyHost(void* dst, const void* src, size_t size) {
#ifdef _WIN32
  std::memcpy(dst, src, size);
  return true;
#else
  iovec local{dst, size}, remote{const_cast<void*>(src), size};
  return process_vm_readv(getpid(), &local, 1, &remote, 1, 0) == ssize_t(size);
#endif
}

// Whether `size` bytes at a guest address can be read.
inline bool SsxGuestReadable(rex::memory::Memory* memory, uint32_t address, uint32_t size) {
  rex::memory::BaseHeap* heap = address ? memory->LookupHeap(address) : nullptr;
  rex::memory::HeapAllocationInfo info{};
  if (!heap || !heap->QueryRegionInfo(address, &info) || !(info.state & rex::memory::kMemoryAllocationCommit) ||
      !(info.protect & rex::memory::kMemoryProtectRead) ||
      uint64_t(address) + size > uint64_t(info.base_address) + info.region_size) {
    return false;
  }
#ifdef _WIN32
  return true;
#else
  std::vector<uint8_t> probe(size);
  return SsxCopyHost(probe.data(), memory->TranslateVirtual<const uint8_t*>(address), size);
#endif
}

// Calls fn(guest address of the copy, copy, bytes) for each readable megabyte of the guest heap that
// holds `start`, from a copy that runs `overlap` bytes on (for a match across the megabyte's end).
// Note: a megabyte with one unreadable page is skipped whole; go per page if that ever misses something.
template <typename Fn>
void SsxScanGuestHeap(rex::memory::Memory* memory, uint32_t start, uint32_t overlap, Fn&& fn) {
  rex::memory::BaseHeap* heap = memory->LookupHeap(start);
  if (!heap) return;
  constexpr uint32_t kChunk = 1u << 20;
  std::vector<uint8_t> chunk(kChunk + overlap);
  uint64_t end = uint64_t(heap->heap_base()) + heap->heap_size();
  for (uint64_t address = heap->heap_base(); address < end;) {
    rex::memory::HeapAllocationInfo info{};
    if (!heap->QueryRegionInfo(uint32_t(address), &info) || !info.region_size) {
      address += heap->page_size();
      continue;
    }
    if ((info.state & rex::memory::kMemoryAllocationCommit) && (info.protect & rex::memory::kMemoryProtectRead)) {
      const uint8_t* region = memory->TranslateVirtual<const uint8_t*>(info.base_address);
      for (uint32_t offset = 0; offset + overlap <= info.region_size; offset += kChunk) {
        uint32_t n = std::min<uint32_t>(kChunk + overlap, info.region_size - offset);
        if (SsxCopyHost(chunk.data(), region + offset, n)) fn(info.base_address + offset, chunk.data(), n);
      }
    }
    address = uint64_t(info.base_address) + info.region_size;
  }
}
