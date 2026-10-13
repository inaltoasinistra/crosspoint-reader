#pragma once
namespace HalMemory {
struct Heap {
  unsigned freeBytes, minFreeBytes, largestBlockBytes;
};
inline Heap getInternalHeap() { return {100000, 90000, 80000}; }
}  // namespace HalMemory
