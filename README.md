# sp — Custom Shared/Weak Pointer (Header-only, C++20)

This is a lightweight, header-only smart pointer library that provides `sp::SharedPtr` and `sp::WeakPtr` for both single objects and arrays. It mirrors the core behavior of `std::shared_ptr/std::weak_ptr`, adds array support, custom deleters/allocators, optional debug logs, and uses a compact control-block design with thread-safe reference counting.

- Header: src/sp.hpp
- Minimum standard: C++20 (optional logging uses <print> from C++23, guarded by a macro)

## Features

- Shared ownership with atomic reference counting
- Weak references with lock() semantics
- Single-object and array specializations: `SharedPtr<T>` and `SharedPtr<T[]>`
- Custom deleters and allocators from raw pointers
- Efficient make_shared for single objects (in-place storage, one allocation)
- Optional debug logging via `SP_DEBUG`
- Introspect stored deleter/allocator via `ptr.deleter<Deleter>()`

## Design

- Control blocks (in `sp::detail`)
  - `IControlBlockBase`: holds `strongCnt`, `weakCnt`, and virtual hooks:
    - `destroy_object()`, `destroy_block()`, `deleter(const std::type_info&)`
  - `ControlBlockDirect<T, Deleter, Alloc>`: in-place object storage (used by make_shared/allocated_shared)
  - `ControlBlockPtr<T, Deleter, Alloc>`: manages pre-allocated single objects
  - `ControlBlockPtr<T[], Deleter, Alloc>`: manages pre-allocated arrays
- Refcount ops:
  - `incr_strong_ref`, `incr_weak_ref`
  - `release_shared_ref` (destroys object at 0 and block if no weaks)
  - `release_weak_ref` (destroys block when both counts reach 0)
- Weak lock:
  - `weak_ptr_lock_impl` uses CAS on `strongCnt` for safe promotion

Thread-safety: counts use `std::atomic_size_t` and appropriate memory orders.

## API Overview

Types
- `sp::SharedPtr<T>`: single object
- `sp::SharedPtr<T[]>`: arrays
- `sp::WeakPtr<T>`, `sp::WeakPtr<T[]>`

Construction (single object)
- `SharedPtr<T>()`, `SharedPtr<T>(std::nullptr_t)`
- From raw pointer:
  - `SharedPtr<T>(U* p)` where `U*` convertible to `T*`
  - `SharedPtr<T>(U* p, Deleter d)`
  - `SharedPtr<T>(U* p, Deleter d, Alloc alloc)`
- Cross-type copy/move when pointer types are convertible

Construction (arrays)
- `SharedPtr<T[]>(U* p, Deleter d = {}, Alloc alloc = {})` where `U` is exactly `T`

Observers
- `T* get() const`, `operator bool()`
- `size_t strong_count() const`
- `*` and `->` on `SharedPtr<T>`
- `operator[](ptrdiff_t)` on `SharedPtr<T[]>`
- `template<class D> D* deleter() const` to access stored deleter/allocator (if present)

Modifiers
- `reset()` / `reset(newPtr)` (array overload for `SharedPtr<T[]>`)
- `swap(SharedPtr&)`

Weak pointers
- `WeakPtr<T>`/`WeakPtr<T[]>` constructible from `SharedPtr`
- `lock() -> SharedPtr<T>`; `expired()`
- `strong_count()`, `weak_count()`
- `reset()`, `swap()`

Factory functions
- `make_shared<T>(Args&&... args)`
- `allocated_shared<T>(const Alloc& alloc, Args&&... args)`   // note: name is allocated_shared
- `make_shared_array<T>(size_t size)`
- `allocate_shared_array<T>(const Alloc& alloc, size_t size)`

## Debug Logging

Define `SP_DEBUG` before including `sp.hpp` to enable logging:
- Macro: `SP_LOG(...)` -> `std::println(...)`
- Disabled by default (no overhead when off)

Example:
```cpp
#define SP_DEBUG 1
#include "src/sp.hpp"
```

## Usage Examples

Single object and weak pointer
```cpp
#include "src/sp.hpp"
#include <cassert>

int main() {
    auto sp1 = sp::make_shared<int>(42);
    assert(*sp1 == 42);

    sp::WeakPtr<int> wp = sp1;
    auto sp2 = wp.lock();
    assert(sp2 && sp2.get() == sp1.get());
}
```

Raw pointer + custom deleter
```cpp
#include "src/sp.hpp"
#include <cstdio>

struct FileCloser {
    void operator()(std::FILE* f) const noexcept { if (f) std::fclose(f); }
};

int main() {
    std::FILE* f = std::fopen("data.txt", "w");
    sp::SharedPtr<std::FILE> fp(f, FileCloser{});
    // use fp.get()
}
```

Arrays
```cpp
#include "src/sp.hpp"
#include <cstddef>

int main() {
    auto arr = sp::make_shared_array<int>(5);
    for (std::size_t i = 0; i < 5; ++i) arr[i] = static_cast<int>(i);
}
```

Custom allocator with in-place object
```cpp
#include "src/sp.hpp"
#include <memory>

struct Foo { int x; Foo(int v): x(v) {} };

int main() {
    std::allocator<Foo> alloc;
    auto spf = sp::allocated_shared<Foo>(alloc, 7);
}
```

## Notes and Limitations

- Header-only; include `src/sp.hpp` in your project.
- Requires C++20. Optional `<print>` usage is guarded by `SP_DEBUG`.
- No aliasing constructors and no `enable_shared_from_this` integration.
- When constructing from raw pointers, ensure the deleter/allocator match how the object/array was created.
