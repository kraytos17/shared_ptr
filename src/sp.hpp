#pragma once

#include <atomic>
#include <cstddef>
#include <memory>
#include <print>
#include <type_traits>
#include <typeinfo>
#include <utility>

namespace sp {
    // to disambiguate ctors
    struct from_raw_ptr_tag {};
    struct from_raw_ptr_with_deleter_tag {};
    struct from_raw_ptr_with_deleter_alloc_tag {};
    struct with_defaults_tag {};

    inline constexpr from_raw_ptr_tag from_raw_ptr{};
    inline constexpr from_raw_ptr_with_deleter_tag from_raw_ptr_with_deleter{};
    inline constexpr from_raw_ptr_with_deleter_alloc_tag from_raw_ptr_with_deleter_alloc{};
    inline constexpr with_defaults_tag with_defaults{};

    template<typename T>
    class WeakPtr;

    class IControlBlockBase {
    public:
        std::atomic_long strongCount{0};
        std::atomic_long weakCount{0};

        virtual ~IControlBlockBase() = default;
        constexpr virtual void destroyObject() = 0;
        constexpr virtual void destroyBlock() = 0;
        constexpr virtual void* deleter(const std::type_info&) const noexcept = 0;
    };

    template<typename T, typename Deleter = std::default_delete<T>,
             typename Alloc = std::allocator<T>>
    class ControlBlockDirect : public IControlBlockBase {
    public:
        template<typename... Args>
        constexpr explicit ControlBlockDirect(Deleter d, Alloc alloc, Args&&... args) :
            m_deleter(std::move(d)), m_alloc(std::move(alloc)) {
            ::new (static_cast<void*>(ptr())) T(std::forward<Args>(args)...);
        }

        ~ControlBlockDirect() = default;

        constexpr void destroyObject() override { std::destroy_at(ptr()); }
        constexpr T* ptr() noexcept {
            return std::assume_aligned<alignof(T)>(std::launder(reinterpret_cast<T*>(&m_storage)));
        }

        constexpr const T* ptr() const noexcept {
            return std::assume_aligned<alignof(T)>(std::launder(reinterpret_cast<T*>(&m_storage)));
        }

        constexpr void destroyBlock() override {
            using BlockAlloc =
              typename std::allocator_traits<Alloc>::template rebind_alloc<ControlBlockDirect>;
            BlockAlloc blockAlloc(m_alloc);
            std::allocator_traits<BlockAlloc>::deallocate(blockAlloc, this, 1);
        }

        constexpr void* deleter(const std::type_info& type) const noexcept override {
            if (type == typeid(Deleter)) {
                return const_cast<Deleter*>(&m_deleter);
            }
            if (type == typeid(Alloc)) {
                return const_cast<Alloc*>(&m_alloc);
            }
            return nullptr;
        }

    private:
        alignas(T) std::byte m_storage[sizeof(T)];
        [[no_unique_address]] Deleter m_deleter;
        [[no_unique_address]] Alloc m_alloc;
    };

    template<typename T, typename Deleter = std::default_delete<T>,
             typename Alloc = std::allocator<T>>
    class ControlBlockPtr : public IControlBlockBase {
    public:
        constexpr explicit ControlBlockPtr(T* ptr, Deleter d, Alloc alloc) :
            m_ptr(ptr), m_deleter(std::move(d)), m_alloc(std::move(alloc)) {
            std::println("ControlBlockPtr created:");
            std::println("  - ptr: {}", static_cast<void*>(m_ptr));
            std::println("  - deleter type: {}", typeid(Deleter).name());
            std::println("  - deleter address: {}", static_cast<void*>(&m_deleter));
            std::println("  - initial strongCount: {}", strongCount.load());
            std::println("  - initial weakCount: {}", weakCount.load());

            if constexpr (std::is_class_v<Deleter> && !std::is_final_v<Deleter>) {
                std::println("  - deleter appears to be a class type (possibly lambda)");
            }
        }

        constexpr void destroyObject() override {
            std::println("\nControlBlockPtr::destroyObject():");
            std::println("  - ptr: {}", static_cast<void*>(m_ptr));
            std::println("  - current deleter address: {}", static_cast<void*>(&m_deleter));
            std::println("  - calling deleter...");

            try {
                m_deleter(m_ptr);
                std::println("  - deleter completed successfully");
            } catch (const std::exception& e) {
                std::println("  - deleter threw exception: {}", e.what());
                throw;
            } catch (...) {
                std::println("  - deleter threw unknown exception");
                throw;
            }
        }

        constexpr void* deleter(const std::type_info& type) const noexcept override {
            std::println("ControlBlockPtr::deleter() - type query: {}", type.name());
            if (type == typeid(Deleter)) {
                return const_cast<Deleter*>(&m_deleter);
            }
            if (type == typeid(Alloc)) {
                return const_cast<Alloc*>(&m_alloc);
            }
            return nullptr;
        }

        constexpr void destroyBlock() override {
            std::println("ControlBlockPtr::destroyBlock() - ptr={}", static_cast<void*>(m_ptr));
            using BlockAlloc =
              typename std::allocator_traits<Alloc>::template rebind_alloc<ControlBlockPtr>;
            BlockAlloc blockAlloc(m_alloc);
            std::allocator_traits<BlockAlloc>::deallocate(blockAlloc, this, 1);
            std::println("ControlBlockPtr::destroyBlock() - completed");
        }

        constexpr T* ptr() noexcept { return m_ptr; }
        constexpr const T* ptr() const noexcept { return m_ptr; }

    private:
        T* m_ptr;
        [[no_unique_address]] Deleter m_deleter;
        [[no_unique_address]] Alloc m_alloc;
    };

    template<typename T, typename Deleter, typename Alloc>
    class ControlBlockPtr<T[], Deleter, Alloc> : public IControlBlockBase {
    public:
        constexpr explicit ControlBlockPtr(T* ptr, Deleter d, Alloc alloc) :
            m_ptr(ptr), m_deleter(std::move(d)), m_alloc(std::move(alloc)) {}

        constexpr void destroyObject() override { m_deleter(m_ptr); }
        constexpr void* deleter(const std::type_info& type) const noexcept override {
            if (type == typeid(Deleter)) {
                return const_cast<Deleter*>(&m_deleter);
            }
            if (type == typeid(Alloc)) {
                return const_cast<Alloc*>(&m_alloc);
            }
            return nullptr;
        }

        constexpr void destroyBlock() override {
            using BlockAlloc =
              typename std::allocator_traits<Alloc>::template rebind_alloc<ControlBlockPtr>;
            BlockAlloc blockAlloc(m_alloc);
            std::allocator_traits<BlockAlloc>::deallocate(blockAlloc, this, 1);
        }

        constexpr T* ptr() noexcept { return m_ptr; }
        constexpr const T* ptr() const noexcept { return m_ptr; }

    private:
        T* m_ptr;
        [[no_unique_address]] Deleter m_deleter;
        [[no_unique_address]] Alloc m_alloc;
    };

    template<typename T>
    class SharedPtr {
    public:
        using element_type = std::remove_extent_t<T>;

        constexpr SharedPtr() noexcept = default;
        constexpr SharedPtr(std::nullptr_t) noexcept {}

        template<typename U>
        explicit SharedPtr(from_raw_ptr_tag, U* ptr)
            requires std::convertible_to<U*, element_type*> && (!std::is_array_v<U>)
            :
            SharedPtr(from_raw_ptr_with_deleter, ptr, std::default_delete<U>{},
                      std::allocator<U>{}) {}

        template<typename U, typename Deleter>
        SharedPtr(from_raw_ptr_with_deleter_tag, U* ptr, Deleter d)
            requires std::convertible_to<U*, element_type*>
            : SharedPtr(from_raw_ptr_with_deleter, ptr, std::move(d), std::allocator<U>{}) {}

        template<typename U, typename Deleter, typename Alloc>
        SharedPtr(from_raw_ptr_with_deleter_tag, U* ptr, Deleter d, Alloc alloc)
            requires std::convertible_to<U*, element_type*> && std::invocable<Deleter&, U*>
        {
            std::println("\nSharedPtr(from_raw_ptr_with_deleter_tag) constructor:");
            std::println("  - raw ptr: {}", static_cast<void*>(ptr));
            std::println("  - deleter type: {}", typeid(Deleter).name());
            std::println("  - incoming deleter address: {}", static_cast<void*>(&d));

            if (ptr) {
                using Block = ControlBlockPtr<U, std::remove_cv_t<Deleter>, Alloc>;
                using BlockAlloc =
                  typename std::allocator_traits<Alloc>::template rebind_alloc<Block>;
                BlockAlloc blockAlloc(alloc);

                std::println("  - allocating control block...");
                Block* block = blockAlloc.allocate(1);
                std::println("  - control block allocated at: {}", static_cast<void*>(block));

                std::println("  - constructing control block with deleter...");
                std::construct_at(block, ptr, std::move(d), alloc);

                m_ptr = ptr;
                m_ctl = block;
                m_ctl->strongCount.fetch_add(1, std::memory_order_acq_rel);

                std::println("  - SharedPtr initialized:");
                std::println("    - stored ptr: {}", static_cast<void*>(m_ptr));
                std::println("    - control block: {}", static_cast<void*>(m_ctl));
                std::println("    - initial strongCount: {}", m_ctl->strongCount.load());
            } else {
                std::println("  - nullptr input, creating empty SharedPtr");
            }
        }

        ~SharedPtr() {
            std::println("~SharedPtr() - ptr={}, ctl={}, strongCount={}",
                         static_cast<void*>(m_ptr),
                         static_cast<void*>(m_ctl),
                         m_ctl ? m_ctl->strongCount.load() : 0);
            release();
        }

        constexpr SharedPtr(const SharedPtr& other) noexcept :
            m_ptr(other.m_ptr), m_ctl(other.m_ctl) {
            if (m_ctl) {
                m_ctl->strongCount.fetch_add(1, std::memory_order_acq_rel);
            }
        }

        constexpr SharedPtr(SharedPtr&& other) noexcept :
            m_ptr(std::exchange(other.m_ptr, nullptr)), m_ctl(std::exchange(other.m_ctl, nullptr)) {
        }

        SharedPtr& operator=(const SharedPtr& other) noexcept {
            if (this != &other) {
                this->~SharedPtr();

                m_ptr = other.m_ptr;
                m_ctl = other.m_ctl;
                if (m_ctl) {
                    m_ctl->strongCount.fetch_add(1, std::memory_order_acq_rel);
                }
            }
            return *this;
        }

        SharedPtr& operator=(SharedPtr&& other) noexcept {
            SharedPtr(std::move(other)).swap(*this);
            return *this;
        }

        [[nodiscard]] constexpr element_type* get() const noexcept { return m_ptr; }
        [[nodiscard]] constexpr element_type& operator*() const noexcept { return *m_ptr; }
        [[nodiscard]] constexpr element_type* operator->() const noexcept { return m_ptr; }
        [[nodiscard]] constexpr operator bool() const noexcept { return m_ptr != nullptr; }
        [[nodiscard]] constexpr long strongCount() const noexcept {
            return m_ctl ? m_ctl->strongCount.load(std::memory_order_acquire) : 0;
        }

        constexpr void reset() noexcept { SharedPtr().swap(*this); }

        template<typename U = T>
        constexpr void reset(U* ptr = nullptr) {
            SharedPtr(ptr).swap(*this);
        }

        constexpr void swap(SharedPtr& other) noexcept {
            std::swap(m_ptr, other.m_ptr);
            std::swap(m_ctl, other.m_ctl);
        }

    private:
        T* m_ptr{nullptr};
        IControlBlockBase* m_ctl{nullptr};

        constexpr SharedPtr(T* ptr, IControlBlockBase* ctl) noexcept : m_ptr(ptr), m_ctl(ctl) {
            if (m_ctl) {
                m_ctl->strongCount.fetch_add(1, std::memory_order_acq_rel);
            }
        }

        constexpr void release() noexcept {
            std::println("SharedPtr::release() - enter");
            if (m_ctl) {
                auto oldCount = m_ctl->strongCount.fetch_sub(1, std::memory_order_acq_rel);
                std::println("SharedPtr::release() - strongCount decreased from {} to {}",
                             oldCount,
                             oldCount - 1);

                if (oldCount == 1) {
                    std::println("SharedPtr::release() - last strong reference, destroying object");
                    m_ctl->destroyObject();

                    auto weakCount = m_ctl->weakCount.load(std::memory_order_acquire);
                    std::println("SharedPtr::release() - weakCount={}", weakCount);

                    if (weakCount == 0) {
                        std::println("SharedPtr::release() - no weak references, destroying block");
                        m_ctl->destroyBlock();
                    }
                }
            }
            std::println("SharedPtr::release() - exit");
        }

        template<typename U, typename... Args>
        friend constexpr SharedPtr<U> makeShared(Args&&... args);

        template<typename U, typename Alloc, typename... Args>
        friend constexpr SharedPtr<U> allocateShared(const Alloc& alloc, Args&&... args);

        friend class WeakPtr<element_type>;
    };

    template<typename T, typename... Args>
    [[nodiscard]] constexpr SharedPtr<T> makeShared(Args&&... args) {
        using Block = ControlBlockDirect<T, std::default_delete<T>, std::allocator<T>>;
        using Alloc = std::allocator<T>;

        Alloc alloc;
        using BlockAlloc = typename std::allocator_traits<Alloc>::template rebind_alloc<Block>;
        BlockAlloc blockAlloc(alloc);

        auto* block = std::allocator_traits<BlockAlloc>::allocate(blockAlloc, 1);
        std::construct_at(block, std::default_delete<T>{}, alloc, std::forward<Args>(args)...);

        return SharedPtr<T>(block->ptr(), block);
    }

    template<typename T, typename Alloc, typename... Args>
    [[nodiscard]] constexpr SharedPtr<T> allocateShared(const Alloc& alloc, Args&&... args) {
        using Block = ControlBlockDirect<T, std::default_delete<T>, Alloc>;
        using BlockAlloc = typename std::allocator_traits<Alloc>::template rebind_alloc<Block>;

        BlockAlloc blockAlloc(alloc);
        auto* block = std::allocator_traits<BlockAlloc>::allocate(blockAlloc, 1);
        std::construct_at(block, std::default_delete<T>{}, alloc, std::forward<Args>(args)...);

        return SharedPtr<T>(block->ptr(), block);
    }

    template<typename T>
    class SharedPtr<T[]> {
    public:
        using element_type = std::remove_extent_t<T>;

        constexpr element_type& operator*() const = delete;
        constexpr element_type* operator->() const = delete;
        constexpr element_type& operator[](ptrdiff_t idx) const { return m_ptr[idx]; }

        constexpr SharedPtr() noexcept = default;
        constexpr SharedPtr(std::nullptr_t) noexcept {}

        template<typename U, typename Deleter = std::default_delete<U[]>,
                 typename Alloc = std::allocator<U>>
        explicit SharedPtr(from_raw_ptr_with_deleter_tag, U* ptr, Deleter d = {}, Alloc alloc = {})
            requires std::same_as<U, element_type>
        {
            if (ptr) {
                using Block = ControlBlockPtr<U[], Deleter, Alloc>;
                using BlockAlloc =
                  typename std::allocator_traits<Alloc>::template rebind_alloc<Block>;
                BlockAlloc blockAlloc(alloc);

                auto* block = std::allocator_traits<BlockAlloc>::allocate(blockAlloc, 1);
                std::construct_at(block, ptr, std::move(d), alloc);

                m_ptr = ptr;
                m_ctl = block;
                m_ctl->strongCount.fetch_add(1, std::memory_order_acq_rel);
            }
        }

        constexpr SharedPtr(const SharedPtr& other) noexcept :
            m_ptr(other.m_ptr), m_ctl(other.m_ctl) {
            if (m_ctl) {
                m_ctl->strongCount.fetch_add(1, std::memory_order_acq_rel);
            }
        }

        constexpr SharedPtr(SharedPtr&& other) noexcept :
            m_ptr(std::exchange(other.m_ptr, nullptr)), m_ctl(std::exchange(other.m_ctl, nullptr)) {
        }


        ~SharedPtr() { release(); }
        constexpr SharedPtr& operator=(const SharedPtr& other) noexcept {
            SharedPtr(other).swap(*this);
            return *this;
        }

        constexpr SharedPtr& operator=(SharedPtr&& other) noexcept {
            SharedPtr(std::move(other)).swap(*this);
            return *this;
        }

        constexpr void reset() noexcept { SharedPtr().swap(*this); }

        template<typename U = element_type>
        constexpr void reset(U* ptr = nullptr)
            requires std::same_as<U, element_type>
        {
            SharedPtr(ptr).swap(*this);
        }

        constexpr void swap(SharedPtr& other) noexcept {
            std::swap(m_ptr, other.m_ptr);
            std::swap(m_ctl, other.m_ctl);
        }

        [[nodiscard]] constexpr element_type* get() const noexcept { return m_ptr; }
        [[nodiscard]] constexpr operator bool() const noexcept { return m_ptr != nullptr; }
        [[nodiscard]] constexpr long strongCount() const noexcept {
            return m_ctl ? m_ctl->strongCount.load(std::memory_order_acquire) : 0;
        }

        template<typename Deleter>
        [[nodiscard]] constexpr Deleter* deleter() const noexcept {
            return m_ctl ? static_cast<Deleter*>(m_ctl->deleter(typeid(Deleter))) : nullptr;
        }

        // constexpr void wait() const noexcept
        //     requires(__cpp_lib_atomic_wait >= 201907L)
        // {
        //     if (m_ctl) {
        //         m_ctl->strongCount.wait(0, std::memory_order_acquire);
        //     }
        // }

    private:
        element_type* m_ptr{nullptr};
        IControlBlockBase* m_ctl{nullptr};

        constexpr SharedPtr(element_type* ptr, IControlBlockBase* ctl) noexcept :
            m_ptr(ptr), m_ctl(ctl) {
            if (m_ctl) {
                m_ctl->strongCount.fetch_add(1, std::memory_order_acq_rel);
            }
        }

        constexpr void release() noexcept {
            if (m_ctl && m_ctl->strongCount.fetch_sub(1, std::memory_order_acq_rel) == 1) {
                m_ctl->destroyObject();
                if (m_ctl->weakCount.load(std::memory_order_acquire) == 0) {
                    m_ctl->destroyBlock();
                }
            }
        }

        template<typename U>
        friend constexpr SharedPtr<U[]> makeSharedArray(size_t size);

        template<typename U, typename Alloc>
        friend constexpr SharedPtr<U[]> allocateSharedArray(const Alloc& alloc, size_t size);

        friend class WeakPtr<element_type>;
    };

    template<typename A, typename Alloc>
    struct ArrayDeleter {
        Alloc alloc;
        size_t size;

        void operator()(A* ptr) {
            for (size_t i = 0; i < size; ++i) {
                std::destroy_at(ptr + i);
            }
            std::allocator_traits<Alloc>::deallocate(alloc, ptr, size);
        }
    };

    template<typename T>
    [[nodiscard]] constexpr SharedPtr<T[]> makeSharedArray(size_t size) {
        if (size == 0) {
            return {};
        }

        using Alloc = std::allocator<T>;
        using Deleter = ArrayDeleter<T, Alloc>;
        using Block = ControlBlockPtr<T[], Deleter, Alloc>;

        Alloc alloc;
        T* ptr = std::allocator_traits<Alloc>::allocate(alloc, size);
        size_t constructed = 0;

        try {
            for (; constructed < size; ++constructed) {
                std::construct_at(ptr + constructed);
            }

            Deleter deleter{alloc, size};

            using BlockAlloc = typename std::allocator_traits<Alloc>::template rebind_alloc<Block>;
            BlockAlloc blockAlloc(alloc);
            auto* block = blockAlloc.allocate(1);

            try {
                std::construct_at(block, ptr, deleter, alloc);
                return SharedPtr<T[]>(ptr, static_cast<IControlBlockBase*>(block));
            } catch (...) {
                blockAlloc.deallocate(block, 1);
                throw;
            }

        } catch (...) {
            for (size_t i = 0; i < constructed; ++i) {
                std::destroy_at(ptr + i);
            }
            std::allocator_traits<Alloc>::deallocate(alloc, ptr, size);
            throw;
        }
    }

    template<typename T, typename Alloc>
    [[nodiscard]] constexpr SharedPtr<T[]> allocateSharedArray(const Alloc& alloc, size_t size) {
        if (size == 0) {
            return {};
        }

        using ElementAlloc = typename std::allocator_traits<Alloc>::template rebind_alloc<T>;
        using Deleter = ArrayDeleter<T, ElementAlloc>;
        using Block = ControlBlockPtr<T[], Deleter, Alloc>;

        ElementAlloc elementAlloc(alloc);
        T* ptr = std::allocator_traits<ElementAlloc>::allocate(elementAlloc, size);
        size_t constructed = 0;

        try {
            for (; constructed < size; ++constructed) {
                std::construct_at(ptr + constructed);
            }

            Deleter deleter{elementAlloc, size};
            using BlockAlloc = typename std::allocator_traits<Alloc>::template rebind_alloc<Block>;
            BlockAlloc blockAlloc(alloc);
            auto* block = blockAlloc.allocate(1);

            try {
                std::construct_at(block, ptr, deleter, alloc);
                return SharedPtr<T[]>(ptr, static_cast<IControlBlockBase*>(block));
            } catch (...) {
                blockAlloc.deallocate(block, 1);
                throw;
            }

        } catch (...) {
            for (size_t i = 0; i < constructed; ++i) {
                std::destroy_at(ptr + i);
            }
            std::allocator_traits<ElementAlloc>::deallocate(elementAlloc, ptr, size);
            throw;
        }
    }

    template<typename T>
    class WeakPtr {
    public:
        using element_type = typename SharedPtr<T>::element_type;

        constexpr WeakPtr() noexcept = default;

        template<typename U>
        constexpr WeakPtr(const SharedPtr<U>& other) noexcept
            requires std::convertible_to<U*, element_type*>
            : m_ptr(other.m_ptr), m_ctl(other.m_ctl) {
            if (m_ctl) {
                m_ctl->weakCount.fetch_add(1, std::memory_order_acq_rel);
            }
        }

        constexpr WeakPtr(const WeakPtr& other) noexcept : m_ptr(other.m_ptr), m_ctl(other.m_ctl) {
            if (m_ctl) {
                m_ctl->weakCount.fetch_add(1, std::memory_order_acq_rel);
            }
        }

        constexpr WeakPtr(WeakPtr&& other) noexcept :
            m_ptr(std::exchange(other.m_ptr, nullptr)), m_ctl(std::exchange(other.m_ctl, nullptr)) {
        }

        ~WeakPtr() { release(); }
        constexpr WeakPtr& operator=(const WeakPtr& other) noexcept {
            WeakPtr(other).swap(*this);
            return *this;
        }

        constexpr WeakPtr& operator=(WeakPtr&& other) noexcept {
            WeakPtr(std::move(other)).swap(*this);
            return *this;
        }

        [[nodiscard]] constexpr SharedPtr<T> lock() const noexcept {
            SharedPtr<T> result;
            if (m_ctl) {
                auto count = m_ctl->strongCount.load(std::memory_order_acquire);
                while (true) {
                    if (count == 0) {
                        return {};
                    }
                    if (m_ctl->strongCount.compare_exchange_weak(
                          count, count + 1, std::memory_order_acq_rel, std::memory_order_relaxed)) {
                        result.m_ptr = m_ptr;
                        result.m_ctl = m_ctl;
                        return result;
                    }
                }
            }
            return result;
        }

        [[nodiscard]] constexpr long strongCount() const noexcept {
            return m_ctl ? m_ctl->strongCount.load(std::memory_order_acquire) : 0;
        }

        [[nodiscard]] constexpr bool expired() const noexcept { return strongCount() == 0; }
        constexpr void swap(WeakPtr& other) noexcept {
            std::swap(m_ptr, other.m_ptr);
            std::swap(m_ctl, other.m_ctl);
        }

        // constexpr void wait() const noexcept
        //     requires(__cpp_lib_atomic_wait >= 201907L)
        // {
        //     if (m_ctl) {
        //         m_ctl->strongCount.wait(0, std::memory_order_acquire);
        //     }
        // }

    private:
        element_type* m_ptr{nullptr};
        IControlBlockBase* m_ctl{nullptr};

        constexpr void release() noexcept {
            if (m_ctl && m_ctl->weakCount.fetch_sub(1, std::memory_order_release) == 1) {
                if (m_ctl->strongCount.load(std::memory_order_acquire) == 0) {
                    m_ctl->destroyBlock();
                }
            }
        }
    };

    template<typename T>
    class WeakPtr<T[]> {
    public:
        using element_type = std::remove_extent_t<T>;

        constexpr element_type& operator*() const = delete;
        constexpr element_type* operator->() const = delete;

        constexpr WeakPtr() noexcept = default;
        constexpr WeakPtr(const SharedPtr<T[]>& other) noexcept :
            m_ptr(other.m_ptr), m_ctl(other.m_ctl) {
            if (m_ctl) {
                m_ctl->weakCount.fetch_add(1, std::memory_order_acq_rel);
            }
        }

        constexpr WeakPtr(const WeakPtr& other) noexcept : m_ptr(other.m_ptr), m_ctl(other.m_ctl) {
            if (m_ctl) {
                m_ctl->weakCount.fetch_add(1, std::memory_order_acq_rel);
            }
        }

        constexpr WeakPtr(WeakPtr&& other) noexcept :
            m_ptr(std::exchange(other.m_ptr, nullptr)), m_ctl(std::exchange(other.m_ctl, nullptr)) {
        }


        ~WeakPtr() { release(); }
        constexpr WeakPtr& operator=(const WeakPtr& other) noexcept {
            WeakPtr(other).swap(*this);
            return *this;
        }

        constexpr WeakPtr& operator=(WeakPtr&& other) noexcept {
            WeakPtr(std::move(other)).swap(*this);
            return *this;
        }

        [[nodiscard]] constexpr SharedPtr<T[]> lock() const noexcept {
            SharedPtr<T[]> result;
            if (m_ctl) {
                auto count = m_ctl->strongCount.load(std::memory_order_acquire);
                do {
                    if (count == 0) {
                        break;
                    }
                } while (!m_ctl->strongCount.compare_exchange_weak(
                  count, count + 1, std::memory_order_acq_rel, std::memory_order_relaxed));

                if (count != 0) {
                    result.m_ptr = m_ptr;
                    result.m_ctl = m_ctl;
                }
            }
            return result;
        }

        [[nodiscard]] constexpr long strongCount() const noexcept {
            return m_ctl ? m_ctl->strongCount.load(std::memory_order_acquire) : 0;
        }

        [[nodiscard]] constexpr bool expired() const noexcept { return strongCount() == 0; }
        constexpr void swap(WeakPtr& other) noexcept {
            std::swap(m_ptr, other.m_ptr);
            std::swap(m_ctl, other.m_ctl);
        }

    private:
        element_type* m_ptr{nullptr};
        IControlBlockBase* m_ctl{nullptr};

        constexpr void release() noexcept {
            if (m_ctl && m_ctl->weakCount.fetch_sub(1, std::memory_order_release) == 1) {
                if (m_ctl->strongCount.load(std::memory_order_acquire) == 0) {
                    m_ctl->destroyBlock();
                }
            }
        }
    };
} // namespace sp
