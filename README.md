# sp - A Custom Shared Pointer Implementation in C++

This code implements a custom smart pointer library in C++ with `SharedPtr` and `WeakPtr` classes, similar to `std::shared_ptr` and `std::weak_ptr` from the C++ Standard Library, but with additional features and debug output.

## Overview

The code defines a namespace `sp` containing a custom implementation of shared and weak pointers (`SharedPtr` and `WeakPtr`) with support for:

- Shared ownership of dynamically allocated objects via reference counting
- Weak references to avoid cyclic dependencies
- Custom deleters and allocators for flexible memory management
- Array support with specialized handling for array types (e.g., `SharedPtr<T[]>`)
- Thread-safe reference counting using `std::atomic_long`
- Extensive debug logging via `std::println` to track object lifetime, reference counts, and memory operations

The implementation uses a control block (`IControlBlockBase` and its derivatives) to manage the reference counts and resource cleanup.

## Key Components

### 1. Namespace and Tags

- **Namespace**: `sp` encapsulates the entire library
- **Constructor Tags**: `from_raw_ptr`, `from_raw_ptr_with_deleter`, `from_raw_ptr_with_deleter_alloc`, and `with_defaults` are used to disambiguate constructor overloads when creating `SharedPtr` from raw pointers

### 2. Control Blocks (in `detail` namespace)

The control blocks manage the lifetime of the pointed-to object and the reference counts:

- **`IControlBlockBase`**: Abstract base class with:

  - `strongCount`: Atomic counter for shared references
  - `weakCount`: Atomic counter for weak references
  - Virtual methods: `destroyObject()` (deletes the managed object), `destroyBlock()` (deallocates the control block), and `deleter()` (accesses the deleter)

- **`ControlBlockDirect`**: Used for objects constructed in-place (e.g., via `makeShared`)

  - Stores the object inline in `m_storage` using placement new
  - Supports custom deleters and allocators

- **`ControlBlockPtr`**: Used for managing pre-allocated raw pointers

  - Stores a pointer to the object (`m_ptr`) and handles cleanup with a custom deleter
  - Specialized for arrays (`T[]`) with a separate implementation

- **Reference Counting Helpers**:
  - `incrementStrongRef`/`incrementWeakRef`: Atomically increment the respective counters
  - `releaseSharedRef`: Decrements `strongCount`; if it reaches 0, destroys the object and potentially the block
  - `releaseWeakRef`: Decrements `weakCount`; if both counts are 0, destroys the block

### 3. SharedPtr<T> (Non-Array Version)

Manages shared ownership of a single object of type `T`.

**Key Features**:

- **Constructors**: Default, from raw pointers (with optional deleter/allocator), copy, and move
- **Operators**: Dereference (`*`, `->`), boolean conversion, assignment
- **Methods**: `get()` (raw pointer access), `strongCount()` (reference count), `reset()` (releases ownership), `swap()`
- Thread-safe reference counting via `detail::incrementStrongRef` and `detail::releaseSharedRef`
- **Debugging**: Logs construction, destruction, and reference count changes

### 4. SharedPtr<T[]> (Array Version)

Specialized for arrays (`T[]`).

**Key Differences**:

- Disables `*` and `->` operators (not meaningful for arrays)
- Adds `operator[]` for array indexing
- Supports custom deleters and allocators for array cleanup
- **Construction**: Similar to the non-array version but tailored for arrays

### 5. WeakPtr<T> and WeakPtr<T[]>

Provides non-owning references to objects managed by `SharedPtr`.

**Key Features**:

- Constructed from `SharedPtr` or another `WeakPtr`
- `lock()`: Attempts to create a `SharedPtr` if the object is still alive
- `expired()`: Checks if the object has been destroyed (`strongCount == 0`)
- `strongCount()`: Queries the current strong reference count
- **Debugging**: Logs weak reference operations

### 6. Factory Functions

- `makeShared<T>(Args&&... args)`: Constructs an object of type `T` in-place and returns a `SharedPtr`
- `allocateShared<T>(Alloc, Args&&... args)`: Similar, but with a custom allocator
- `makeSharedArray<T>(size_t size)`: Creates an array of `T` with default-constructed elements
- `allocateSharedArray<T>(Alloc, size_t size)`: Array version with a custom allocator

## Design Highlights

### Memory Management

- Uses control blocks to separate object lifetime from pointer management
- Supports in-place construction (`ControlBlockDirect`) and raw pointer management (`ControlBlockPtr`)

### Thread Safety

- Reference counts are managed with `std::atomic_long` and appropriate memory orders (`acq_rel`, `release`)
- `weakPtrLockImpl` uses `compare_exchange_weak` for safe locking

### Flexibility

- Custom deleters and allocators allow fine-grained control over resource cleanup
- Array specialization ensures proper handling of array types

### Debugging

- Extensive use of `std::println` for tracing pointer operations, useful for debugging memory issues

## Usage Example

```cpp
#include "sp.hpp"

int main() {
    // Single object
    auto sp1 = sp::makeShared<int>(42);
    std::println("Value: {}", *sp1); // Logs construction and value

    // Weak pointer
    sp::WeakPtr<int> wp1 = sp1;
    auto sp2 = wp1.lock(); // Creates another shared pointer if alive

    // Array
    auto sp3 = sp::makeSharedArray<int>(5);
    sp3[0] = 10; // Access array elements

    // Custom deleter
    auto deleter = [](int* p) { std::println("Deleting {}", *p); delete p; };
    auto sp4 = sp::SharedPtr<int>(sp::from_raw_ptr_with_deleter, new int(99), deleter);

    return 0;
}
```
