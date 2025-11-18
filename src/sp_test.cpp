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

TEST_CASE(default_constructor) {
    sp::SharedPtr<int> ptr;
    testing::assert_that<"Default constructed SharedPtr should be null">(!ptr);
    testing::assert_eq<"ptr.get()", "nullptr">(ptr.get(), nullptr);
    testing::assert_eq<"ptr.strong_count()", "0">(ptr.strong_count(), 0uz);
}

TEST_CASE(make_shared_construction) {
    testing::AllocationTracker::reset();
    {
        auto ptr = sp::make_shared<int>(42);
        testing::assert_that<"make_shared returned null">(ptr);
        testing::assert_eq<"*ptr", "42">(*ptr, 42);
        testing::assert_eq<"ptr.strong_count()", "1">(ptr.strong_count(), 1uz);
        testing::assert_that<"Too many allocations">(
            testing::AllocationTracker::allocations <= 3,
            testing::detail::format_value(testing::AllocationTracker::allocations.load()));
    }
    testing::AllocationTracker::check_balanced();
}

TEST_CASE(copy_semantics) {
    auto ptr1 = sp::make_shared<int>(42);
    {
        sp::SharedPtr<int> ptr2(ptr1);
        testing::assert_eq<"ptr1.get()", "ptr2.get()">(ptr1.get(), ptr2.get());
        testing::assert_eq<"ptr1.strong_count()", "2">(ptr1.strong_count(), 2uz);
        testing::assert_eq<"ptr2.strong_count()", "2">(ptr2.strong_count(), 2uz);

        sp::SharedPtr<int> ptr3;
        ptr3 = ptr2;
        testing::assert_eq<"ptr1.strong_count()", "3">(ptr1.strong_count(), 3uz);
    }
    testing::assert_eq<"ptr1.strong_count()", "1">(ptr1.strong_count(), 1uz);
}

TEST_CASE(move_semantics) {
    auto ptr1 = sp::make_shared<int>(42);
    {
        sp::SharedPtr<int> ptr2(std::move(ptr1));
        testing::assert_that<"Moved-from pointer should be null">(!ptr1);
        testing::assert_eq<"ptr1.strong_count()", "0">(ptr1.strong_count(), 0uz);
        testing::assert_that<"ptr2 should be valid">(ptr2);
        testing::assert_eq<"*ptr2", "42">(*ptr2, 42);
        testing::assert_eq<"ptr2.strong_count()", "1">(ptr2.strong_count(), 1uz);

        sp::SharedPtr<int> ptr3;
        ptr3 = std::move(ptr2);
        testing::assert_that<"ptr2 should be null after move">(!ptr2);
        testing::assert_that<"ptr3 should be valid">(ptr3);
        testing::assert_eq<"ptr3.strong_count()", "1">(ptr3.strong_count(), 1uz);
    }
}

TEST_CASE(weak_ptr_functionality) {
    auto shared = sp::make_shared<int>(42);
    sp::WeakPtr<int> weak(shared);

    testing::assert_that<"WeakPtr should not be expired">(!weak.expired());
    testing::assert_eq<"weak.strong_count()", "1">(weak.strong_count(), 1uz);

    if (auto locked = weak.lock()) {
        testing::assert_eq<"*locked", "42">(*locked, 42);
        testing::assert_eq<"shared.strong_count()", "2">(shared.strong_count(), 2uz);
    }

    shared.reset();
    testing::assert_that<"WeakPtr should be expired">(weak.expired());
    testing::assert_eq<"weak.lock().get()", "nullptr">(weak.lock().get(), nullptr);
}

TEST_CASE(nullptr_construction) {
    sp::SharedPtr<int> ptr(nullptr);
    testing::assert_that<"Should be null">(!ptr);
    testing::assert_eq<"Refcount should be 0", "0">(ptr.strong_count(), 0uz);
}

TEST_CASE(zero_size_array) {
    auto arr = sp::make_shared_array<int>(0);
    testing::assert_that<"Should be null">(!arr);
    testing::assert_eq<"Refcount should be 0", "0">(arr.strong_count(), 0uz);
}

TEST_CASE(thread_safety) {
    constexpr int kThreads = 10;
    constexpr int kIterations = 1000;

    auto shared = sp::make_shared<std::atomic_int>(0);
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
        sp::SharedPtr<int> ptr(new int(42), deleter);
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
        auto arr = sp::make_shared_array<Tracked>(5);
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
        auto arr = sp::make_shared_array<Thrower>(5);
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

    auto ptr = sp::make_shared<MoveOnly>();
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
        auto _ = sp::allocated_shared<int>(alloc, 42);
        testing::assert_that<"Allocator should have performed allocation">(alloc.allocs() > 0);
        testing::assert_eq<"alloc.deallocs()", "0">(alloc.deallocs(), 0);
    }

    testing::assert_eq<"alloc.deallocs()", "alloc.allocs()">(alloc.deallocs(), alloc.allocs());
}

TEST_CASE(allocator_with_array) {
    auto counter = std::make_shared<TrackingCounter>();
    TrackingAllocator<int> alloc(counter);

    {
        auto arr = sp::allocate_shared_array<int>(alloc, 5);
        testing::assert_that<"Should have allocations">(alloc.allocs() > 0);
    }

    testing::assert_eq<"Deallocations should match", "alloc.allocs()">(alloc.deallocs(),
                                                                       alloc.allocs());
}

TEST_CASE(move_semantics_additional) {
    sp::SharedPtr<int> empty1;
    sp::SharedPtr<int> empty2(std::move(empty1));
    testing::assert_that<"Moved-from empty should stay null">(!empty1);
    testing::assert_that<"Moved-to empty should stay null">(!empty2);

    auto ptr = sp::make_shared<int>(42);
    sp::SharedPtr<int>& ref = ptr;
    ptr = std::move(ref);

    testing::assert_that<"Move from reference should leave pointer valid">(ptr);
    testing::assert_eq<"Value should be preserved", "42">(*ptr, 42);
}

TEST_CASE(weak_ptr_edge_cases) {
    sp::SharedPtr<int> empty;
    sp::WeakPtr<int> weak_from_empty(empty);
    testing::assert_that<"Weak from empty should be expired">(weak_from_empty.expired());

    auto shared = sp::make_shared<int>(42);
    sp::WeakPtr<int> weak1(shared);
    sp::WeakPtr<int> weak2(weak1);
    testing::assert_eq<"Both weak pointers should have same count">(weak1.strong_count(),
                                                                    weak2.strong_count());
}

TEST_CASE(deleter_access) {
    auto deleter = [](int* p) { delete p; };
    sp::SharedPtr<int> ptr(new int(42), deleter);

    auto* retrieved_deleter = ptr.deleter<decltype(deleter)>();
    testing::assert_that<"Should be able to retrieve deleter">(retrieved_deleter != nullptr);
}

TEST_CASE(control_block_sharing) {
    auto ptr1 = sp::make_shared<int>(42);
    auto ptr2 = ptr1;

    testing::assert_eq<"Both should point to same object">(ptr1.get(), ptr2.get());
    testing::assert_eq<"Both should have same refcount">(ptr1.strong_count(), ptr2.strong_count());

    ptr1.reset();
    testing::assert_eq<"ptr2 should still have 1 ref">(ptr2.strong_count(), 1uz);
}

TEST_CASE(array_indexing) {
    auto arr = sp::make_shared_array<int>(3);
    arr[0] = 1;
    arr[1] = 2;
    arr[2] = 3;

    testing::assert_eq<"arr[0]", "1">(arr[0], 1);
    testing::assert_eq<"arr[1]", "2">(arr[1], 2);
    testing::assert_eq<"arr[2]", "3">(arr[2], 3);

    const auto& const_arr = arr;
    testing::assert_eq<"const_arr[0]", "1">(const_arr[0], 1);
}

TEST_CASE(various_deleter_types) {
    void (*func_deleter)(int*) = [](int* p) { delete p; };
    sp::SharedPtr<int> ptr1(new int(42), func_deleter);

    struct FunctorDeleter {
        void operator()(int* p) const { delete p; }
    };

    sp::SharedPtr<int> ptr2(new int(42), FunctorDeleter{});

    struct StatefulDeleter {
        int* count;
        void operator()(int* p) const {
            delete p;
            (*count)++;
        }
    };

    int delete_count = 0;
    {
        sp::SharedPtr<int> ptr3(new int(42), StatefulDeleter{&delete_count});
    }

    testing::assert_eq<"Stateful deleter should be called", "1">(delete_count, 1);
}

TEST_CASE(inheritance_support) {
    struct Base {
        virtual ~Base() = default;
        virtual int value() const { return 1; }
    };

    struct Derived : Base {
        int value() const override { return 2; }
    };

    sp::SharedPtr<Derived> derived = sp::make_shared<Derived>();
    sp::SharedPtr<Base> base = derived;

    testing::assert_eq<"Base should point to Derived object", "2">(base->value(), 2);
    testing::assert_eq<"Refcount should be shared", "2">(derived.strong_count(), 2uz);
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
