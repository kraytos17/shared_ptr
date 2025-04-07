#pragma once

#include <atomic>
#include <cstddef>
#include <memory>
#include <print>
#include <type_traits>
#include <typeinfo>
#include <utility>

namespace sp {
    // Public tags for disambiguating constructors
    struct from_raw_ptr_tag {};
    struct from_raw_ptr_with_deleter_tag {};
    struct from_raw_ptr_with_deleter_alloc_tag {};
    struct with_defaults_tag {};

    inline constexpr from_raw_ptr_tag from_raw_ptr{};
    inline constexpr from_raw_ptr_with_deleter_tag from_raw_ptr_with_deleter{};
    inline constexpr from_raw_ptr_with_deleter_alloc_tag from_raw_ptr_with_deleter_alloc{};
    inline constexpr with_defaults_tag with_defaults{};

    template<typename T>
    class SharedPtr;

    template<typename T>
    class WeakPtr;

    namespace detail {
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
                return std::assume_aligned<alignof(T)>(
                    std::launder(reinterpret_cast<T*>(&m_storage)));
            }

            constexpr const T* ptr() const noexcept {
                return std::assume_aligned<alignof(T)>(
                    std::launder(reinterpret_cast<T*>(&m_storage)));
            }

            constexpr void destroyBlock() override {
                using BlockAlloc = typename std::allocator_traits<Alloc>::template rebind_alloc<
                    ControlBlockDirect>;
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

        inline constexpr void incrementRef(IControlBlockBase* ctl,
                                           std::atomic_long& counter) noexcept {
            if (ctl) {
                counter.fetch_add(1, std::memory_order_acq_rel);
            }
        }

        inline constexpr void incrementStrongRef(IControlBlockBase* ctl) noexcept {
            incrementRef(ctl, ctl->strongCount);
        }

        inline constexpr void incrementWeakRef(IControlBlockBase* ctl) noexcept {
            incrementRef(ctl, ctl->weakCount);
        }

        inline constexpr void releaseSharedRef(IControlBlockBase* ctl) noexcept {
            if (!ctl) {
                return;
            }

            auto oldCount = ctl->strongCount.fetch_sub(1, std::memory_order_acq_rel);
            if (oldCount == 1) {
                ctl->destroyObject();
                if (ctl->weakCount.load(std::memory_order_acquire) == 0) {
                    ctl->destroyBlock();
                }
            }
        }

        inline constexpr void releaseWeakRef(IControlBlockBase* ctl) noexcept {
            if (ctl && ctl->weakCount.fetch_sub(1, std::memory_order_release) == 1) {
                if (ctl->strongCount.load(std::memory_order_acquire) == 0) {
                    ctl->destroyBlock();
                }
            }
        }

        template<typename T, typename U, typename Deleter, typename Alloc>
        constexpr IControlBlockBase* createControlBlock(U* ptr, Deleter&& d, Alloc&& alloc) {
            if (!ptr) {
                return nullptr;
            }

            using Block = std::conditional_t<
                std::is_array_v<T>,
                ControlBlockPtr<T[], std::remove_cv_t<Deleter>, std::remove_cv_t<Alloc>>,
                ControlBlockPtr<T, std::remove_cv_t<Deleter>, std::remove_cv_t<Alloc>>>;

            using BlockAlloc = typename std::allocator_traits<
                std::remove_cv_t<Alloc>>::template rebind_alloc<Block>;
            BlockAlloc blockAlloc(std::forward<Alloc>(alloc));

            auto* block = blockAlloc.allocate(1);
            std::construct_at(block, ptr, std::forward<Deleter>(d), std::forward<Alloc>(alloc));
            return block;
        }

        template<typename T>
        constexpr SharedPtr<T> weakPtrLockImpl(T* ptr, IControlBlockBase* ctl) noexcept {
            SharedPtr<T> result;
            if (ctl) {
                auto count = ctl->strongCount.load(std::memory_order_acquire);
                while (count != 0) {
                    // clang-format off
                    if (ctl->strongCount.compare_exchange_weak(count,
                                            count + 1,
                                            std::memory_order_acq_rel,
                                            std::memory_order_relaxed)) {
                        // clang-format on
                        result.m_ptr = ptr;
                        result.m_ctl = ctl;
                        break;
                    }
                }
            }
            return result;
        }

        template<typename A, typename Alloc>
        struct ArrayDeleter {
            Alloc alloc;
            size_t size;

            constexpr void operator()(A* ptr) noexcept {
                std::destroy_n(ptr, size);
                std::allocator_traits<Alloc>::deallocate(alloc, ptr, size);
            }
        };

        template<typename T, typename Alloc, typename... Args>
        [[nodiscard]] constexpr SharedPtr<T> allocSharedImpl(const Alloc& alloc, Args&&... args) {
            using Block = ControlBlockDirect<T, std::default_delete<T>, Alloc>;
            using BlockAlloc = typename std::allocator_traits<Alloc>::template rebind_alloc<Block>;

            BlockAlloc blockAlloc(alloc);
            auto* block = std::allocator_traits<BlockAlloc>::allocate(blockAlloc, 1);
            std::construct_at(block, std::default_delete<T>{}, alloc, std::forward<Args>(args)...);

            return SharedPtr<T>(block->ptr(), block);
        }

        template<typename T, typename... Args>
        [[nodiscard]] constexpr SharedPtr<T> makeSharedImpl(Args&&... args) {
            return allocSharedImpl<T>(std::allocator<T>{}, std::forward<Args>(args)...);
        }

        template<typename T, typename Alloc>
        [[nodiscard]] constexpr SharedPtr<T[]> allocSharedArrayImpl(const Alloc& alloc,
                                                                    size_t size) {
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
                std::uninitialized_default_construct_n(ptr, size);
                Deleter deleter{elementAlloc, size};

                using BlockAlloc =
                    typename std::allocator_traits<Alloc>::template rebind_alloc<Block>;
                BlockAlloc blockAlloc(alloc);
                auto* block = blockAlloc.allocate(1);

                try {
                    std::construct_at(block, ptr, deleter, alloc);
                    return SharedPtr<T[]>(ptr, block);
                } catch (...) {
                    blockAlloc.deallocate(block, 1);
                    throw;
                }
            } catch (...) {
                std::destroy_n(ptr, constructed);
                std::allocator_traits<ElementAlloc>::deallocate(elementAlloc, ptr, size);
                throw;
            }
        }

        template<typename T>
        [[nodiscard]] constexpr SharedPtr<T[]> makeSharedArrayImpl(size_t size) {
            return allocSharedArrayImpl<T>(std::allocator<T>{}, size);
        }
    } // namespace detail

    template<typename T>
    class SharedPtr {
    public:
        using element_type = std::remove_extent_t<T>;

        constexpr SharedPtr() noexcept = default;
        constexpr SharedPtr(std::nullptr_t) noexcept {}

        template<typename U>
            requires std::convertible_to<U*, element_type*> && (!std::is_array_v<U>)
        explicit SharedPtr(from_raw_ptr_tag, U* ptr) :
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
                m_ctl = detail::createControlBlock<T>(ptr, std::move(d), std::move(alloc));
                m_ptr = ptr;
                std::println("  - SharedPtr initialized:");
                std::println("    - stored ptr: {}", static_cast<void*>(m_ptr));
                std::println("    - control block: {}", static_cast<void*>(m_ctl));
                std::println("    - initial strongCount: {}", m_ctl->strongCount.load());
                detail::incrementStrongRef(m_ctl);
            } else {
                std::println("  - nullptr input, creating empty SharedPtr");
            }
        }

        ~SharedPtr() {
            std::println("~SharedPtr() - ptr={}, ctl={}, strongCount={}",
                         static_cast<void*>(m_ptr),
                         static_cast<void*>(m_ctl),
                         m_ctl ? m_ctl->strongCount.load() : 0);
            detail::releaseSharedRef(m_ctl);
        }

        constexpr SharedPtr(const SharedPtr& other) noexcept :
            m_ptr(other.m_ptr), m_ctl(other.m_ctl) {
            std::println("SharedPtr copy constructor - incrementing ref count");
            detail::incrementStrongRef(m_ctl);
        }

        constexpr SharedPtr(SharedPtr&& other) noexcept :
            m_ptr(std::exchange(other.m_ptr, nullptr)), m_ctl(std::exchange(other.m_ctl, nullptr)) {
            std::println("SharedPtr move constructor");
        }

        SharedPtr& operator=(const SharedPtr& other) noexcept {
            std::println("SharedPtr copy assignment");
            if (this != &other) {
                detail::releaseSharedRef(m_ctl);
                m_ptr = other.m_ptr;
                m_ctl = other.m_ctl;
                detail::incrementStrongRef(m_ctl);
            }
            return *this;
        }

        SharedPtr& operator=(SharedPtr&& other) noexcept {
            std::println("SharedPtr move assignment");
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

        constexpr void reset() noexcept {
            std::println("SharedPtr::reset()");
            SharedPtr().swap(*this);
        }

        template<typename U = T>
        constexpr void reset(U* ptr = nullptr) {
            std::println("SharedPtr::reset(U*)");
            SharedPtr(ptr).swap(*this);
        }

        constexpr void swap(SharedPtr& other) noexcept {
            std::println("SharedPtr::swap()");
            std::swap(m_ptr, other.m_ptr);
            std::swap(m_ctl, other.m_ctl);
        }

    private:
        T* m_ptr{nullptr};
        detail::IControlBlockBase* m_ctl{nullptr};

        constexpr SharedPtr(T* ptr, detail::IControlBlockBase* ctl) noexcept :
            m_ptr(ptr), m_ctl(ctl) {
            std::println("SharedPtr private constructor");
            detail::incrementStrongRef(m_ctl);
        }

        template<typename U, typename... Args>
        friend constexpr SharedPtr<U> detail::makeSharedImpl(Args&&... args);

        template<typename U, typename Alloc, typename... Args>
        friend constexpr SharedPtr<U> detail::allocSharedImpl(const Alloc& alloc, Args&&... args);

        template<typename U>
        friend constexpr SharedPtr<U>
        detail::weakPtrLockImpl(U* ptr, detail::IControlBlockBase* ctl) noexcept;

        friend class WeakPtr<element_type>;
    };

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
            std::println("\nSharedPtr<T[]>(from_raw_ptr_with_deleter_tag) constructor:");
            std::println("  - raw ptr: {}", static_cast<void*>(ptr));
            std::println("  - deleter type: {}", typeid(Deleter).name());

            if (ptr) {
                m_ctl = detail::createControlBlock<T[]>(ptr, std::move(d), std::move(alloc));
                m_ptr = ptr;
                std::println("  - SharedPtr<T[]> initialized:");
                std::println("    - stored ptr: {}", static_cast<void*>(m_ptr));
                std::println("    - control block: {}", static_cast<void*>(m_ctl));
                detail::incrementStrongRef(m_ctl);
            }
        }

        ~SharedPtr() {
            std::println("~SharedPtr<T[]>() - ptr={}, ctl={}, strongCount={}",
                         static_cast<void*>(m_ptr),
                         static_cast<void*>(m_ctl),
                         m_ctl ? m_ctl->strongCount.load() : 0);
            detail::releaseSharedRef(m_ctl);
        }

        constexpr SharedPtr(const SharedPtr& other) noexcept :
            m_ptr(other.m_ptr), m_ctl(other.m_ctl) {
            std::println("SharedPtr<T[]> copy constructor");
            detail::incrementStrongRef(m_ctl);
        }

        constexpr SharedPtr(SharedPtr&& other) noexcept :
            m_ptr(std::exchange(other.m_ptr, nullptr)), m_ctl(std::exchange(other.m_ctl, nullptr)) {
            std::println("SharedPtr<T[]> move constructor");
        }

        constexpr SharedPtr& operator=(const SharedPtr& other) noexcept {
            std::println("SharedPtr<T[]> copy assignment");
            SharedPtr(other).swap(*this);
            return *this;
        }

        constexpr SharedPtr& operator=(SharedPtr&& other) noexcept {
            std::println("SharedPtr<T[]> move assignment");
            SharedPtr(std::move(other)).swap(*this);
            return *this;
        }

        constexpr void reset() noexcept {
            std::println("SharedPtr<T[]>::reset()");
            SharedPtr().swap(*this);
        }

        template<typename U = element_type>
        constexpr void reset(U* ptr = nullptr)
            requires std::same_as<U, element_type>
        {
            std::println("SharedPtr<T[]>::reset(U*)");
            SharedPtr(ptr).swap(*this);
        }

        constexpr void swap(SharedPtr& other) noexcept {
            std::println("SharedPtr<T[]>::swap()");
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

    private:
        element_type* m_ptr{nullptr};
        detail::IControlBlockBase* m_ctl{nullptr};

        constexpr SharedPtr(element_type* ptr, detail::IControlBlockBase* ctl) noexcept :
            m_ptr(ptr), m_ctl(ctl) {
            std::println("SharedPtr<T[]> private constructor");
            detail::incrementStrongRef(m_ctl);
        }

        template<typename U>
        friend constexpr SharedPtr<U[]> detail::makeSharedArrayImpl(size_t size);

        template<typename U, typename Alloc>
        friend constexpr SharedPtr<U[]> detail::allocSharedArrayImpl(const Alloc& alloc,
                                                                     size_t size);

        friend class WeakPtr<element_type>;
    };

    template<typename T>
    class WeakPtr {
    public:
        using element_type = typename SharedPtr<T>::element_type;

        constexpr WeakPtr() noexcept = default;

        template<typename U>
        constexpr WeakPtr(const SharedPtr<U>& other) noexcept
            requires std::convertible_to<U*, element_type*>
            : m_ptr(other.m_ptr), m_ctl(other.m_ctl) {
            std::println("WeakPtr constructor from SharedPtr");
            detail::incrementWeakRef(m_ctl);
        }

        constexpr WeakPtr(const WeakPtr& other) noexcept : m_ptr(other.m_ptr), m_ctl(other.m_ctl) {
            std::println("WeakPtr copy constructor");
            detail::incrementWeakRef(m_ctl);
        }

        constexpr WeakPtr(WeakPtr&& other) noexcept :
            m_ptr(std::exchange(other.m_ptr, nullptr)), m_ctl(std::exchange(other.m_ctl, nullptr)) {
            std::println("WeakPtr move constructor");
        }

        ~WeakPtr() {
            std::println("~WeakPtr() - ptr={}, ctl={}, weakCount={}",
                         static_cast<void*>(m_ptr),
                         static_cast<void*>(m_ctl),
                         m_ctl ? m_ctl->weakCount.load() : 0);
            detail::releaseWeakRef(m_ctl);
        }

        constexpr WeakPtr& operator=(const WeakPtr& other) noexcept {
            std::println("WeakPtr copy assignment");
            WeakPtr(other).swap(*this);
            return *this;
        }

        constexpr WeakPtr& operator=(WeakPtr&& other) noexcept {
            std::println("WeakPtr move assignment");
            WeakPtr(std::move(other)).swap(*this);
            return *this;
        }

        [[nodiscard]] constexpr SharedPtr<T> lock() const noexcept {
            std::println("WeakPtr::lock() attempt");
            auto result = detail::weakPtrLockImpl(m_ptr, m_ctl);
            std::println("WeakPtr::lock() result - {}", result ? "success" : "failed");
            return result;
        }

        [[nodiscard]] constexpr long strongCount() const noexcept {
            auto count = m_ctl ? m_ctl->strongCount.load(std::memory_order_acquire) : 0;
            std::println("WeakPtr::strongCount() = {}", count);
            return count;
        }

        [[nodiscard]] constexpr bool expired() const noexcept {
            bool exp = strongCount() == 0;
            std::println("WeakPtr::expired() = {}", exp);
            return exp;
        }

        constexpr void swap(WeakPtr& other) noexcept {
            std::println("WeakPtr::swap()");
            std::swap(m_ptr, other.m_ptr);
            std::swap(m_ctl, other.m_ctl);
        }

    private:
        element_type* m_ptr{nullptr};
        detail::IControlBlockBase* m_ctl{nullptr};
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
            std::println("WeakPtr<T[]> constructor from SharedPtr<T[]>");
            detail::incrementWeakRef(m_ctl);
        }

        constexpr WeakPtr(const WeakPtr& other) noexcept : m_ptr(other.m_ptr), m_ctl(other.m_ctl) {
            std::println("WeakPtr<T[]> copy constructor");
            detail::incrementWeakRef(m_ctl);
        }

        constexpr WeakPtr(WeakPtr&& other) noexcept :
            m_ptr(std::exchange(other.m_ptr, nullptr)), m_ctl(std::exchange(other.m_ctl, nullptr)) {
            std::println("WeakPtr<T[]> move constructor");
        }

        ~WeakPtr() {
            std::println("~WeakPtr<T[]>() - ptr={}, ctl={}, weakCount={}",
                         static_cast<void*>(m_ptr),
                         static_cast<void*>(m_ctl),
                         m_ctl ? m_ctl->weakCount.load() : 0);
            detail::releaseWeakRef(m_ctl);
        }

        constexpr WeakPtr& operator=(const WeakPtr& other) noexcept {
            std::println("WeakPtr<T[]> copy assignment");
            WeakPtr(other).swap(*this);
            return *this;
        }

        constexpr WeakPtr& operator=(WeakPtr&& other) noexcept {
            std::println("WeakPtr<T[]> move assignment");
            WeakPtr(std::move(other)).swap(*this);
            return *this;
        }

        [[nodiscard]] constexpr SharedPtr<T[]> lock() const noexcept {
            std::println("WeakPtr<T[]>::lock() attempt");
            auto result = detail::weakPtrLockImpl(m_ptr, m_ctl);
            std::println("WeakPtr<T[]>::lock() result - {}", result ? "success" : "failed");
            return result;
        }

        [[nodiscard]] constexpr long strongCount() const noexcept {
            auto count = m_ctl ? m_ctl->strongCount.load(std::memory_order_acquire) : 0;
            std::println("WeakPtr<T[]>::strongCount() = {}", count);
            return count;
        }

        [[nodiscard]] constexpr bool expired() const noexcept {
            bool exp = strongCount() == 0;
            std::println("WeakPtr<T[]>::expired() = {}", exp);
            return exp;
        }

        constexpr void swap(WeakPtr& other) noexcept {
            std::println("WeakPtr<T[]>::swap()");
            std::swap(m_ptr, other.m_ptr);
            std::swap(m_ctl, other.m_ctl);
        }

    private:
        element_type* m_ptr{nullptr};
        detail::IControlBlockBase* m_ctl{nullptr};
    };

    template<typename T, typename... Args>
    [[nodiscard]] constexpr SharedPtr<T> makeShared(Args&&... args) {
        return detail::makeSharedImpl<T>(args...);
    }

    template<typename T, typename Alloc, typename... Args>
    [[nodiscard]] constexpr SharedPtr<T> allocateShared(const Alloc& alloc, Args&&... args) {
        return detail::allocSharedImpl<T>(alloc, args...);
    }

    template<typename T>
    [[nodiscard]] constexpr SharedPtr<T[]> makeSharedArray(size_t size) {
        return detail::makeSharedArrayImpl<T>(size);
    }

    template<typename T, typename Alloc>
    [[nodiscard]] constexpr SharedPtr<T[]> allocateSharedArray(const Alloc& alloc, size_t size) {
        return detail::allocSharedArrayImpl<T>(alloc, size);
    }
} // namespace sp
