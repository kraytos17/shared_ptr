#include "sp.hpp"
#include <atomic>
#include <barrier>
#include <functional>
#include <memory>
#include <ranges>
#include <thread>
#include <vector>
#include "utils.hpp"

namespace views = std::views;

// ==================== TEST IMPLEMENTATION ====================

TEST_CASE(default_constructor) {
    sp::SharedPtr<int> ptr;
    testing::assert_that<"Default constructed SharedPtr should be null">(!ptr);
    testing::assert_eq<"ptr.get()", "nullptr">(ptr.get(), nullptr);
    testing::assert_eq<"ptr.strongCount()", "0">(ptr.strongCount(), 0);
}

TEST_CASE(make_shared_construction) {
    testing::AllocationTracker::reset();
    {
        auto ptr = sp::makeShared<int>(42);
        testing::assert_that<"makeShared returned null">(ptr);
        testing::assert_eq<"*ptr", "42">(*ptr, 42);
        testing::assert_eq<"ptr.strongCount()", "1">(ptr.strongCount(), 1);
        testing::assert_that<"Too many allocations">(
            testing::AllocationTracker::allocations <= 3,
            testing::detail::format_value(testing::AllocationTracker::allocations.load()));
    }
    testing::AllocationTracker::check_balanced();
}

TEST_CASE(copy_semantics) {
    auto ptr1 = sp::makeShared<int>(42);
    {
        sp::SharedPtr<int> ptr2(ptr1);
        testing::assert_eq<"ptr1.get()", "ptr2.get()">(ptr1.get(), ptr2.get());
        testing::assert_eq<"ptr1.strongCount()", "2">(ptr1.strongCount(), 2);
        testing::assert_eq<"ptr2.strongCount()", "2">(ptr2.strongCount(), 2);

        sp::SharedPtr<int> ptr3;
        ptr3 = ptr2;
        testing::assert_eq<"ptr1.strongCount()", "3">(ptr1.strongCount(), 3);
    }
    testing::assert_eq<"ptr1.strongCount()", "1">(ptr1.strongCount(), 1);
}

TEST_CASE(move_semantics) {
    auto ptr1 = sp::makeShared<int>(42);
    {
        sp::SharedPtr<int> ptr2(std::move(ptr1));
        testing::assert_that<"Moved-from pointer should be null">(!ptr1);
        testing::assert_eq<"ptr1.strongCount()", "0">(ptr1.strongCount(), 0);
        testing::assert_that<"ptr2 should be valid">(ptr2);
        testing::assert_eq<"*ptr2", "42">(*ptr2, 42);
        testing::assert_eq<"ptr2.strongCount()", "1">(ptr2.strongCount(), 1);

        sp::SharedPtr<int> ptr3;
        ptr3 = std::move(ptr2);
        testing::assert_that<"ptr2 should be null after move">(!ptr2);
        testing::assert_that<"ptr3 should be valid">(ptr3);
        testing::assert_eq<"ptr3.strongCount()", "1">(ptr3.strongCount(), 1);
    }
}

TEST_CASE(weak_ptr_functionality) {
    auto shared = sp::makeShared<int>(42);
    sp::WeakPtr<int> weak(shared);

    testing::assert_that<"WeakPtr should not be expired">(!weak.expired());
    testing::assert_eq<"weak.strongCount()", "1">(weak.strongCount(), 1);

    if (auto locked = weak.lock()) {
        testing::assert_eq<"*locked", "42">(*locked, 42);
        testing::assert_eq<"shared.strongCount()", "2">(shared.strongCount(), 2);
    }

    shared.reset();
    testing::assert_that<"WeakPtr should be expired">(weak.expired());
    testing::assert_eq<"weak.lock().get()", "nullptr">(weak.lock().get(), nullptr);
}

TEST_CASE(nullptr_construction) {
    sp::SharedPtr<int> ptr(nullptr);
    testing::assert_that<"Should be null">(!ptr);
    testing::assert_eq<"Refcount should be 0", "0">(ptr.strongCount(), 0);
}

TEST_CASE(zero_size_array) {
    auto arr = sp::makeSharedArray<int>(0);
    testing::assert_that<"Should be null">(!arr);
    testing::assert_eq<"Refcount should be 0", "0">(arr.strongCount(), 0);
}

TEST_CASE(thread_safety) {
    constexpr int kThreads = 10;
    constexpr int kIterations = 1000;

    auto shared = sp::makeShared<std::atomic_int>(0);
    std::barrier sync_point(kThreads + 1);
    std::vector<std::jthread> threads;

    for (auto _: views::iota(0, kThreads)) {
        threads.emplace_back([&] {
            for (auto _: views::iota(0, kIterations)) {
                auto local_copy = shared;
                local_copy->fetch_add(1, std::memory_order_relaxed);
                sp::WeakPtr<std::atomic_int> weak(shared);
                if (auto locked = weak.lock()) {
                    locked->fetch_add(1, std::memory_order_relaxed);
                }
            }
            sync_point.arrive_and_wait();
        });
    }

    sync_point.arrive_and_wait();
    int actual = shared->load(std::memory_order_seq_cst);
    int expected = kThreads * kIterations * 2;
    testing::assert_that<"Counter should reach expected value">(
        actual == expected,
        std::format("Expected {} ({} threads * {} iterations * 2 ops), got {}",
                    expected,
                    kThreads,
                    kIterations,
                    actual));

    testing::assert_eq<"actual", "expected">(actual, expected);
}

TEST_CASE(custom_deleter) {
    bool deleted = false;
    auto deleter = [&](int* p) {
        std::println("Deleter called");
        delete p;
        deleted = true;
    };

    {
        sp::SharedPtr<int> ptr(sp::from_raw_ptr_with_deleter, new int(42), deleter);
        testing::assert_that<"Deleter should not be called before SharedPtr destruction">(!deleted);
    }
    testing::assert_that<"Custom deleter was not invoked">(deleted);
}

TEST_CASE(array_support) {
    static int constructions = 0;
    static int destructions = 0;

    struct Tracked {
        Tracked() { constructions++; }
        ~Tracked() { destructions++; }
    };

    constructions = destructions = 0;
    {
        auto arr = sp::makeSharedArray<Tracked>(5);
        testing::assert_eq<"constructions", "5">(constructions, 5);
        testing::assert_eq<"destructions", "0">(destructions, 0);
    }
    testing::assert_eq<"destructions", "5">(destructions, 5);
}

struct Thrower {
    Thrower() {
        if (++count == 3) {
            throw std::runtime_error("Oops");
        }
    }
    static int count;
};

int Thrower::count = 0;

TEST_CASE(exception_safety) {
    Thrower::count = 0;
    try {
        auto arr = sp::makeSharedArray<Thrower>(5);
        testing::assert_that<"Should have thrown std::runtime_error after 3rd Thrower">(false);
    } catch (const std::runtime_error&) {
        testing::assert_eq<"Thrower::count", "3">(Thrower::count, 3);
    }
}

TEST_CASE(move_only_types) {
    struct MoveOnly {
        MoveOnly() = default;
        MoveOnly(const MoveOnly&) = delete;
        MoveOnly(MoveOnly&&) = default;
        MoveOnly& operator=(MoveOnly&&) = default;
    };

    auto ptr = sp::makeShared<MoveOnly>();
    auto ptr2 = std::move(ptr);
    testing::assert_that<"Move should leave source null">(!ptr);
    testing::assert_that<"ptr2 should be valid">(ptr2);
}

struct TrackingCounter {
    int allocs{0};
    int deallocs{0};
};

template<typename T>
struct TrackingAllocator {
    using value_type = T;

    std::shared_ptr<TrackingCounter>* counter_ptr = nullptr;

    constexpr TrackingAllocator() = default;
    constexpr TrackingAllocator(std::shared_ptr<TrackingCounter>& counter) :
        counter_ptr(&counter) {}

    template<typename U>
    constexpr TrackingAllocator(const TrackingAllocator<U>& other) noexcept :
        counter_ptr(other.counter_ptr) {}

    constexpr T* allocate(size_t n) {
        if (counter_ptr && *counter_ptr) {
            ++(*counter_ptr)->allocs;
        }
        return std::allocator<T>{}.allocate(n);
    }

    constexpr void deallocate(T* p, size_t n) {
        if (counter_ptr && *counter_ptr) {
            ++(*counter_ptr)->deallocs;
        }
        std::allocator<T>{}.deallocate(p, n);
    }

    constexpr int allocs() const noexcept {
        return (counter_ptr && *counter_ptr) ? (*counter_ptr)->allocs : 0;
    }

    constexpr int deallocs() const noexcept {
        return (counter_ptr && *counter_ptr) ? (*counter_ptr)->deallocs : 0;
    }

    template<typename U>
    struct rebind {
        using other = TrackingAllocator<U>;
    };

    template<typename U>
    constexpr bool operator==(const TrackingAllocator<U>& other) const {
        return counter_ptr == other.counter_ptr;
    }

    template<typename U>
    constexpr bool operator!=(const TrackingAllocator<U>& other) const {
        return !(*this == other);
    }
};

TEST_CASE(allocator_support) {
    auto counter = std::make_shared<TrackingCounter>();
    TrackingAllocator<int> alloc(counter);

    {
        auto _ = sp::allocateShared<int>(alloc, 42);
        testing::assert_that<"Allocator should have performed allocation">(alloc.allocs() > 0);
        testing::assert_eq<"alloc.deallocs()", "0">(alloc.deallocs(), 0);
    }

    testing::assert_eq<"alloc.deallocs()", "alloc.allocs()">(alloc.deallocs(), alloc.allocs());
}

TEST_CASE(allocator_with_array) {
    auto counter = std::make_shared<TrackingCounter>();
    TrackingAllocator<int> alloc(counter);

    {
        auto arr = sp::allocateSharedArray<int>(alloc, 5);
        testing::assert_that<"Should have allocations">(alloc.allocs() > 0);
    }

    testing::assert_eq<"Deallocations should match", "alloc.allocs()">(alloc.deallocs(),
                                                                       alloc.allocs());
}

// ==================== TEST RUNNER ====================

auto main() -> int {
    try {
        std::println("Running {} tests...", testing::test_registry().size());
        for (const auto& test: testing::test_registry()) {
            std::print("Running test: {}... ", test.name);
            try {
                test.func();
                std::println("PASSED");
            } catch (...) {
                std::println("FAILED");
                throw;
            }
        }

        std::println("All tests passed successfully!");
        return 0;
    } catch (const std::exception& e) {
        std::println("Test failed with exception: {}", e.what());
        return 1;
    } catch (...) {
        std::println("Test failed with unknown exception");
        return 1;
    }
}
