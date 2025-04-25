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

    /// @brief Tag type for constructing from raw pointer
    struct from_raw_ptr_tag {};
    /// @brief Tag type for constructing from raw pointer with deleter
    struct from_raw_ptr_with_deleter_tag {};
    /// @brief Tag type for constructing from raw pointer with deleter and allocator
    struct from_raw_ptr_with_deleter_alloc_tag {};
    /// @brief Tag type for constructing with default parameters
    struct with_defaults_tag {};

    /// @brief Tag instances for constructor disambiguation
    inline constexpr from_raw_ptr_tag from_raw_ptr{};
    inline constexpr from_raw_ptr_with_deleter_tag from_raw_ptr_with_deleter{};
    inline constexpr from_raw_ptr_with_deleter_alloc_tag from_raw_ptr_with_deleter_alloc{};
    inline constexpr with_defaults_tag with_defaults{};

    /// @brief Forward declaration of SharedPtr
    template<typename T>
    class SharedPtr;

    /// @brief Forward declaration of WeakPtr
    template<typename T>
    class WeakPtr;

    namespace detail {
        /**
         * @brief Base interface for control blocks
         * @details Manages reference counting and object lifetime
         */
        class IControlBlockBase {
        public:
            std::atomic_long strongCount{0};  ///< Strong reference count
            std::atomic_long weakCount{0};  ///< Weak reference count

            virtual ~IControlBlockBase() = default;

            /**
             * @brief Destroy the managed object
             */
            constexpr virtual void destroy_object() = 0;

            /**
             * @brief Destroy the control block itself
             */
            constexpr virtual void destroy_block() = 0;

            /**
             * @brief Get the deleter or allocator of a specific type
             * @param type Type info of the requested deleter/allocator
             * @return Pointer to the deleter/allocator or nullptr if not found
             */
            constexpr virtual void* deleter(const std::type_info& type) const noexcept = 0;
        };

        /**
         * @brief Control block for direct object storage (make_shared optimization)
         * @tparam T Managed type
         * @tparam Deleter Deleter type (defaults to std::default_delete<T>)
         * @tparam Alloc Allocator type (defaults to std::allocator<T>)
         */
        template<typename T, typename Deleter = std::default_delete<T>,
                 typename Alloc = std::allocator<T>>
        class ControlBlockDirect : public IControlBlockBase {
        public:
            /**
             * @brief Construct a new ControlBlockDirect object
             * @param d Deleter to use
             * @param alloc Allocator to use
             * @param args Arguments to forward to T's constructor
             */
            template<typename... Args>
            constexpr explicit ControlBlockDirect(Deleter d, Alloc alloc, Args&&... args) :
                m_deleter(std::move(d)), m_alloc(std::move(alloc)) {
                ::new (static_cast<void*>(ptr())) T(std::forward<Args>(args)...);
            }

            ~ControlBlockDirect() = default;

            /**
             * @brief Destroy the managed object
             */
            constexpr void destroy_object() override { std::destroy_at(ptr()); }

            /**
             * @brief Get pointer to the managed object
             * @return Pointer to the managed object
             */
            constexpr T* ptr() noexcept {
                return std::assume_aligned<alignof(T)>(
                    std::launder(reinterpret_cast<T*>(&m_storage)));
            }

            /**
             * @brief Get const pointer to the managed object
             * @return Const pointer to the managed object
             */
            constexpr const T* ptr() const noexcept {
                return std::assume_aligned<alignof(T)>(
                    std::launder(reinterpret_cast<T*>(&m_storage)));
            }

            /**
             * @brief Destroy the control block
             */
            constexpr void destroy_block() override {
                using BlockAlloc = typename std::allocator_traits<Alloc>::template rebind_alloc<
                    ControlBlockDirect>;
                BlockAlloc blockAlloc(m_alloc);
                std::allocator_traits<BlockAlloc>::deallocate(blockAlloc, this, 1);
            }

            /**
             * @brief Get the deleter or allocator
             * @param type Type info of the requested component
             * @return Pointer to the component or nullptr if not found
             */
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
            alignas(T) std::byte m_storage[sizeof(T)];  ///< Storage for the managed object
            [[no_unique_address]] Deleter m_deleter;  ///< The deleter object
            [[no_unique_address]] Alloc m_alloc;  ///< The allocator object
        };

        /**
         * @brief Control block for pointer-based storage
         * @tparam T Managed type
         * @tparam Deleter Deleter type (defaults to std::default_delete<T>)
         * @tparam Alloc Allocator type (defaults to std::allocator<T>)
         */
        template<typename T, typename Deleter = std::default_delete<T>,
                 typename Alloc = std::allocator<T>>
        class ControlBlockPtr : public IControlBlockBase {
        public:
            /**
             * @brief Construct a new ControlBlockPtr object
             * @param ptr Pointer to the managed object
             * @param d Deleter to use
             * @param alloc Allocator to use
             */
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

            /**
             * @brief Destroy the managed object
             */
            constexpr void destroy_object() override {
                std::println("\nControlBlockPtr::destroy_object():");
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

            /**
             * @brief Get the deleter or allocator
             * @param type Type info of the requested component
             * @return Pointer to the component or nullptr if not found
             */
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

            /**
             * @brief Destroy the control block
             */
            constexpr void destroy_block() override {
                std::println("ControlBlockPtr::destroy_block() - ptr={}",
                             static_cast<void*>(m_ptr));
                using BlockAlloc =
                    typename std::allocator_traits<Alloc>::template rebind_alloc<ControlBlockPtr>;
                BlockAlloc blockAlloc(m_alloc);
                std::allocator_traits<BlockAlloc>::deallocate(blockAlloc, this, 1);
                std::println("ControlBlockPtr::destroy_block() - completed");
            }

            /**
             * @brief Get pointer to the managed object
             * @return Pointer to the managed object
             */
            constexpr T* ptr() noexcept { return m_ptr; }

            /**
             * @brief Get const pointer to the managed object
             * @return Const pointer to the managed object
             */
            constexpr const T* ptr() const noexcept { return m_ptr; }

        private:
            T* m_ptr;  ///< Pointer to the managed object
            [[no_unique_address]] Deleter m_deleter;  ///< The deleter object
            [[no_unique_address]] Alloc m_alloc;  ///< The allocator object
        };

        /**
         * @brief Control block for pointer-based storage of arrays
         * @tparam T Array type
         * @tparam Deleter Deleter type
         * @tparam Alloc Allocator type
         */
        template<typename T, typename Deleter, typename Alloc>
        class ControlBlockPtr<T[], Deleter, Alloc> : public IControlBlockBase {
        public:
            /**
             * @brief Construct a new ControlBlockPtr object for arrays
             * @param ptr Pointer to the managed array
             * @param d Deleter to use
             * @param alloc Allocator to use
             */
            constexpr explicit ControlBlockPtr(T* ptr, Deleter d, Alloc alloc) :
                m_ptr(ptr), m_deleter(std::move(d)), m_alloc(std::move(alloc)) {}

            /**
             * @brief Destroy the managed array
             */
            constexpr void destroy_object() override { m_deleter(m_ptr); }

            /**
             * @brief Get the deleter or allocator
             * @param type Type info of the requested component
             * @return Pointer to the component or nullptr if not found
             */
            constexpr void* deleter(const std::type_info& type) const noexcept override {
                if (type == typeid(Deleter)) {
                    return const_cast<Deleter*>(&m_deleter);
                }
                if (type == typeid(Alloc)) {
                    return const_cast<Alloc*>(&m_alloc);
                }
                return nullptr;
            }

            /**
             * @brief Destroy the control block
             */
            constexpr void destroy_block() override {
                using BlockAlloc =
                    typename std::allocator_traits<Alloc>::template rebind_alloc<ControlBlockPtr>;
                BlockAlloc blockAlloc(m_alloc);
                std::allocator_traits<BlockAlloc>::deallocate(blockAlloc, this, 1);
            }

            /**
             * @brief Get pointer to the managed array
             * @return Pointer to the managed array
             */
            constexpr T* ptr() noexcept { return m_ptr; }

            /**
             * @brief Get const pointer to the managed array
             * @return Const pointer to the managed array
             */
            constexpr const T* ptr() const noexcept { return m_ptr; }

        private:
            T* m_ptr;  ///< Pointer to the managed array
            [[no_unique_address]] Deleter m_deleter;  ///< The deleter object
            [[no_unique_address]] Alloc m_alloc;  ///< The allocator object
        };

        /**
         * @brief Increment reference count
         * @param ctl Control block pointer
         * @param counter Reference counter to increment
         */
        inline constexpr void incr_ref(IControlBlockBase* ctl, std::atomic_long& counter) noexcept {
            if (ctl) {
                counter.fetch_add(1, std::memory_order_acq_rel);
            }
        }

        /**
         * @brief Increment strong reference count
         * @param ctl Control block pointer
         */
        inline constexpr void incr_strong_ref(IControlBlockBase* ctl) noexcept {
            incr_ref(ctl, ctl->strongCount);
        }

        /**
         * @brief Increment weak reference count
         * @param ctl Control block pointer
         */
        inline constexpr void incr_weak_ref(IControlBlockBase* ctl) noexcept {
            incr_ref(ctl, ctl->weakCount);
        }

        /**
         * @brief Release shared reference
         * @param ctl Control block pointer
         */
        inline constexpr void release_shared_ref(IControlBlockBase* ctl) noexcept {
            if (!ctl) {
                return;
            }

            auto oldCount = ctl->strongCount.fetch_sub(1, std::memory_order_acq_rel);
            if (oldCount == 1) {
                ctl->destroy_object();
                if (ctl->weakCount.load(std::memory_order_acquire) == 0) {
                    ctl->destroy_block();
                }
            }
        }

        /**
         * @brief Release weak reference
         * @param ctl Control block pointer
         */
        inline constexpr void release_weak_ref(IControlBlockBase* ctl) noexcept {
            if (ctl && ctl->weakCount.fetch_sub(1, std::memory_order_release) == 1) {
                if (ctl->strongCount.load(std::memory_order_acquire) == 0) {
                    ctl->destroy_block();
                }
            }
        }

        /**
         * @brief Create a control block for a managed object
         * @tparam T Managed type
         * @tparam U Pointer type (must be convertible to T*)
         * @tparam Deleter Deleter type
         * @tparam Alloc Allocator type
         * @param ptr Pointer to manage
         * @param d Deleter to use
         * @param alloc Allocator to use
         * @return Pointer to the created control block
         */
        template<typename T, typename U, typename Deleter, typename Alloc>
        constexpr IControlBlockBase* create_ctl_block(U* ptr, Deleter&& d, Alloc&& alloc) {
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

        /**
         * @brief Implementation of weak_ptr::lock()
         * @tparam T Managed type
         * @param ptr Pointer to the managed object
         * @param ctl Control block pointer
         * @return SharedPtr<T> if object still exists, empty SharedPtr otherwise
         */
        template<typename T>
        constexpr SharedPtr<T> weak_ptr_lock_impl(T* ptr, IControlBlockBase* ctl) noexcept {
            SharedPtr<T> result;
            if (ctl) {
                auto count = ctl->strongCount.load(std::memory_order_acquire);
                while (count != 0) {
                    if (ctl->strongCount.compare_exchange_weak(count,
                                                               count + 1,
                                                               std::memory_order_acq_rel,
                                                               std::memory_order_relaxed)) {
                        result.m_ptr = ptr;
                        result.m_ctl = ctl;
                        break;
                    }
                }
            }
            return result;
        }

        /**
         * @brief Deleter for arrays
         * @tparam A Array element type
         * @tparam Alloc Allocator type
         */
        template<typename A, typename Alloc>
        struct ArrayDeleter {
            Alloc alloc;  ///< Allocator to use
            size_t size;  ///< Number of elements in array

            /**
             * @brief Delete the array
             * @param ptr Pointer to the array
             */
            constexpr void operator()(A* ptr) noexcept {
                std::destroy_n(ptr, size);
                std::allocator_traits<Alloc>::deallocate(alloc, ptr, size);
            }
        };

        /**
         * @brief Implementation of alloc_shared
         * @tparam T Type to create
         * @tparam Alloc Allocator type
         * @tparam Args Argument types for constructor
         * @param alloc Allocator to use
         * @param args Arguments for constructor
         * @return SharedPtr<T> managing the new object
         */
        template<typename T, typename Alloc, typename... Args>
        [[nodiscard]] constexpr SharedPtr<T> alloc_shared_impl(const Alloc& alloc, Args&&... args) {
            using Block = ControlBlockDirect<T, std::default_delete<T>, Alloc>;
            using BlockAlloc = typename std::allocator_traits<Alloc>::template rebind_alloc<Block>;

            BlockAlloc blockAlloc(alloc);
            auto* block = std::allocator_traits<BlockAlloc>::allocate(blockAlloc, 1);
            std::construct_at(block, std::default_delete<T>{}, alloc, std::forward<Args>(args)...);

            return SharedPtr<T>(block->ptr(), block);
        }

        /**
         * @brief Implementation of make_shared
         * @tparam T Type to create
         * @tparam Args Argument types for constructor
         * @param args Arguments for constructor
         * @return SharedPtr<T> managing the new object
         */
        template<typename T, typename... Args>
        [[nodiscard]] constexpr SharedPtr<T> make_shared_impl(Args&&... args) {
            return alloc_shared_impl<T>(std::allocator<T>{}, std::forward<Args>(args)...);
        }

        /**
         * @brief Implementation of alloc_shared_array
         * @tparam T Array element type
         * @tparam Alloc Allocator type
         * @param alloc Allocator to use
         * @param size Number of elements in array
         * @return SharedPtr<T[]> managing the new array
         */
        template<typename T, typename Alloc>
        [[nodiscard]] constexpr SharedPtr<T[]> alloc_shared_array_impl(const Alloc& alloc,
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

        /**
         * @brief Implementation of make_shared_array
         * @tparam T Array element type
         * @param size Number of elements in array
         * @return SharedPtr<T[]> managing the new array
         */
        template<typename T>
        [[nodiscard]] constexpr SharedPtr<T[]> make_shared_array_impl(size_t size) {
            return alloc_shared_array_impl<T>(std::allocator<T>{}, size);
        }
    }  // namespace detail

    /**
     * @brief Shared pointer class with reference counting
     * @tparam T Type of the managed object
     */
    template<typename T>
    class SharedPtr {
    public:
        using element_type = std::remove_extent_t<T>;  ///< Type of the managed object

        /// @brief Default constructor
        constexpr SharedPtr() noexcept = default;

        /// @brief Construct from nullptr
        constexpr SharedPtr(std::nullptr_t) noexcept {}

        /**
         * @brief Construct from raw pointer
         * @tparam U Type convertible to T
         * @param from_raw_ptr Tag parameter
         * @param ptr Raw pointer to manage
         */
        template<typename U>
            requires std::convertible_to<U*, element_type*> && (!std::is_array_v<U>)
        explicit SharedPtr(from_raw_ptr_tag, U* ptr) :
            SharedPtr(from_raw_ptr_with_deleter, ptr, std::default_delete<U>{},
                      std::allocator<U>{}) {}

        /**
         * @brief Construct from raw pointer with deleter
         * @tparam U Type convertible to T
         * @tparam Deleter Deleter type
         * @param from_raw_ptr_with_deleter Tag parameter
         * @param ptr Raw pointer to manage
         * @param d Deleter to use
         */
        template<typename U, typename Deleter>
        SharedPtr(from_raw_ptr_with_deleter_tag, U* ptr, Deleter d)
            requires std::convertible_to<U*, element_type*>
            : SharedPtr(from_raw_ptr_with_deleter, ptr, std::move(d), std::allocator<U>{}) {}

        /**
         * @brief Construct from raw pointer with deleter and allocator
         * @tparam U Type convertible to T
         * @tparam Deleter Deleter type
         * @tparam Alloc Allocator type
         * @param from_raw_ptr_with_deleter Tag parameter
         * @param ptr Raw pointer to manage
         * @param d Deleter to use
         * @param alloc Allocator to use
         */
        template<typename U, typename Deleter, typename Alloc>
        SharedPtr(from_raw_ptr_with_deleter_tag, U* ptr, Deleter d, Alloc alloc)
            requires std::convertible_to<U*, element_type*> && std::invocable<Deleter&, U*>
        {
            std::println("\nSharedPtr(from_raw_ptr_with_deleter_tag) constructor:");
            std::println("  - raw ptr: {}", static_cast<void*>(ptr));
            std::println("  - deleter type: {}", typeid(Deleter).name());
            std::println("  - incoming deleter address: {}", static_cast<void*>(&d));

            if (ptr) {
                m_ctl = detail::create_ctl_block<T>(ptr, std::move(d), std::move(alloc));
                m_ptr = ptr;
                std::println("  - SharedPtr initialized:");
                std::println("    - stored ptr: {}", static_cast<void*>(m_ptr));
                std::println("    - control block: {}", static_cast<void*>(m_ctl));
                std::println("    - initial strongCount: {}", m_ctl->strongCount.load());
                detail::incr_strong_ref(m_ctl);
            } else {
                std::println("  - nullptr input, creating empty SharedPtr");
            }
        }

        /// @brief Destructor
        ~SharedPtr() {
            std::println("~SharedPtr() - ptr={}, ctl={}, strongCount={}",
                         static_cast<void*>(m_ptr),
                         static_cast<void*>(m_ctl),
                         m_ctl ? m_ctl->strongCount.load() : 0);
            detail::release_shared_ref(m_ctl);
        }

        /**
         * @brief Copy constructor
         * @param other SharedPtr to copy from
         */
        constexpr SharedPtr(const SharedPtr& other) noexcept :
            m_ptr(other.m_ptr), m_ctl(other.m_ctl) {
            std::println("SharedPtr copy constructor - incrementing ref count");
            detail::incr_strong_ref(m_ctl);
        }

        /**
         * @brief Move constructor
         * @param other SharedPtr to move from
         */
        constexpr SharedPtr(SharedPtr&& other) noexcept :
            m_ptr(std::exchange(other.m_ptr, nullptr)), m_ctl(std::exchange(other.m_ctl, nullptr)) {
            std::println("SharedPtr move constructor");
        }

        /**
         * @brief Copy assignment operator
         * @param other SharedPtr to copy from
         * @return Reference to this SharedPtr
         */
        SharedPtr& operator=(const SharedPtr& other) noexcept {
            std::println("SharedPtr copy assignment");
            if (this != &other) {
                detail::release_shared_ref(m_ctl);
                m_ptr = other.m_ptr;
                m_ctl = other.m_ctl;
                detail::incr_strong_ref(m_ctl);
            }
            return *this;
        }

        /**
         * @brief Move assignment operator
         * @param other SharedPtr to move from
         * @return Reference to this SharedPtr
         */
        SharedPtr& operator=(SharedPtr&& other) noexcept {
            std::println("SharedPtr move assignment");
            SharedPtr(std::move(other)).swap(*this);
            return *this;
        }

        /**
         * @brief Get the raw pointer
         * @return Raw pointer to the managed object
         */
        [[nodiscard]] constexpr element_type* get() const noexcept { return m_ptr; }

        /**
         * @brief Dereference operator
         * @return Reference to the managed object
         */
        [[nodiscard]] constexpr element_type& operator*() const noexcept { return *m_ptr; }

        /**
         * @brief Member access operator
         * @return Pointer to the managed object
         */
        [[nodiscard]] constexpr element_type* operator->() const noexcept { return m_ptr; }

        /**
         * @brief Boolean conversion operator
         * @return true if managing an object, false otherwise
         */
        [[nodiscard]] constexpr operator bool() const noexcept { return m_ptr != nullptr; }

        /**
         * @brief Get the strong reference count
         * @return Current strong reference count
         */
        [[nodiscard]] constexpr long strong_count() const noexcept {
            return m_ctl ? m_ctl->strongCount.load(std::memory_order_acquire) : 0;
        }

        /**
         * @brief Reset the SharedPtr to empty state
         */
        constexpr void reset() noexcept {
            std::println("SharedPtr::reset()");
            SharedPtr().swap(*this);
        }

        /**
         * @brief Reset the SharedPtr to manage a new object
         * @tparam U Type of the new object (defaults to T)
         * @param ptr Pointer to the new object to manage
         */
        template<typename U = T>
        constexpr void reset(U* ptr = nullptr) {
            std::println("SharedPtr::reset(U*)");
            SharedPtr(ptr).swap(*this);
        }

        /**
         * @brief Swap contents with another SharedPtr
         * @param other SharedPtr to swap with
         */
        constexpr void swap(SharedPtr& other) noexcept {
            std::println("SharedPtr::swap()");
            std::swap(m_ptr, other.m_ptr);
            std::swap(m_ctl, other.m_ctl);
        }

    private:
        T* m_ptr{nullptr};  ///< Pointer to the managed object
        detail::IControlBlockBase* m_ctl{nullptr};  ///< Control block pointer

        /**
         * @brief Private constructor from pointer and control block
         * @param ptr Pointer to the managed object
         * @param ctl Control block pointer
         */
        constexpr SharedPtr(T* ptr, detail::IControlBlockBase* ctl) noexcept :
            m_ptr(ptr), m_ctl(ctl) {
            std::println("SharedPtr private constructor");
            detail::incr_strong_ref(m_ctl);
        }

        // Friend declarations for make_shared implementations
        template<typename U, typename... Args>
        friend constexpr SharedPtr<U> detail::make_shared_impl(Args&&... args);

        template<typename U, typename Alloc, typename... Args>
        friend constexpr SharedPtr<U> detail::alloc_shared_impl(const Alloc& alloc, Args&&... args);

        template<typename U>
        friend constexpr SharedPtr<U> detail::weak_ptr_lock_impl(
            U* ptr, detail::IControlBlockBase* ctl) noexcept;

        friend class WeakPtr<element_type>;
    };

    /**
     * @brief Shared pointer specialization for arrays
     * @tparam T Array type
     */
    template<typename T>
    class SharedPtr<T[]> {
    public:
        using element_type = std::remove_extent_t<T>;  ///< Type of array elements

        // Disable dereference operators for arrays
        constexpr element_type& operator*() const = delete;
        constexpr element_type* operator->() const = delete;

        /**
         * @brief Array subscript operator
         * @param idx Index to access
         * @return Reference to the element at index
         */
        constexpr element_type& operator[](ptrdiff_t idx) const { return m_ptr[idx]; }

        /// @brief Default constructor
        constexpr SharedPtr() noexcept = default;

        /// @brief Construct from nullptr
        constexpr SharedPtr(std::nullptr_t) noexcept {}

        /**
         * @brief Construct from raw pointer with deleter and allocator
         * @tparam U Array element type (must be same as T)
         * @tparam Deleter Deleter type (defaults to std::default_delete<U[]>)
         * @tparam Alloc Allocator type (defaults to std::allocator<U>)
         * @param from_raw_ptr_with_deleter Tag parameter
         * @param ptr Raw pointer to manage
         * @param d Deleter to use
         * @param alloc Allocator to use
         */
        template<typename U, typename Deleter = std::default_delete<U[]>,
                 typename Alloc = std::allocator<U>>
        explicit SharedPtr(from_raw_ptr_with_deleter_tag, U* ptr, Deleter d = {}, Alloc alloc = {})
            requires std::same_as<U, element_type>
        {
            std::println("\nSharedPtr<T[]>(from_raw_ptr_with_deleter_tag) constructor:");
            std::println("  - raw ptr: {}", static_cast<void*>(ptr));
            std::println("  - deleter type: {}", typeid(Deleter).name());

            if (ptr) {
                m_ctl = detail::create_ctl_block<T[]>(ptr, std::move(d), std::move(alloc));
                m_ptr = ptr;
                std::println("  - SharedPtr<T[]> initialized:");
                std::println("    - stored ptr: {}", static_cast<void*>(m_ptr));
                std::println("    - control block: {}", static_cast<void*>(m_ctl));
                detail::incr_strong_ref(m_ctl);
            }
        }

        /// @brief Destructor
        ~SharedPtr() {
            std::println("~SharedPtr<T[]>() - ptr={}, ctl={}, strongCount={}",
                         static_cast<void*>(m_ptr),
                         static_cast<void*>(m_ctl),
                         m_ctl ? m_ctl->strongCount.load() : 0);
            detail::release_shared_ref(m_ctl);
        }

        /**
         * @brief Copy constructor
         * @param other SharedPtr to copy from
         */
        constexpr SharedPtr(const SharedPtr& other) noexcept :
            m_ptr(other.m_ptr), m_ctl(other.m_ctl) {
            std::println("SharedPtr<T[]> copy constructor");
            detail::incr_strong_ref(m_ctl);
        }

        /**
         * @brief Move constructor
         * @param other SharedPtr to move from
         */
        constexpr SharedPtr(SharedPtr&& other) noexcept :
            m_ptr(std::exchange(other.m_ptr, nullptr)), m_ctl(std::exchange(other.m_ctl, nullptr)) {
            std::println("SharedPtr<T[]> move constructor");
        }

        /**
         * @brief Copy assignment operator
         * @param other SharedPtr to copy from
         * @return Reference to this SharedPtr
         */
        constexpr SharedPtr& operator=(const SharedPtr& other) noexcept {
            std::println("SharedPtr<T[]> copy assignment");
            SharedPtr(other).swap(*this);
            return *this;
        }

        /**
         * @brief Move assignment operator
         * @param other SharedPtr to move from
         * @return Reference to this SharedPtr
         */
        constexpr SharedPtr& operator=(SharedPtr&& other) noexcept {
            std::println("SharedPtr<T[]> move assignment");
            SharedPtr(std::move(other)).swap(*this);
            return *this;
        }

        /**
         * @brief Reset the SharedPtr to empty state
         */
        constexpr void reset() noexcept {
            std::println("SharedPtr<T[]>::reset()");
            SharedPtr().swap(*this);
        }

        /**
         * @brief Reset the SharedPtr to manage a new array
         * @tparam U Array element type (must be same as T)
         * @param ptr Pointer to the new array to manage
         */
        template<typename U = element_type>
        constexpr void reset(U* ptr = nullptr)
            requires std::same_as<U, element_type>
        {
            std::println("SharedPtr<T[]>::reset(U*)");
            SharedPtr(ptr).swap(*this);
        }

        /**
         * @brief Swap contents with another SharedPtr
         * @param other SharedPtr to swap with
         */
        constexpr void swap(SharedPtr& other) noexcept {
            std::println("SharedPtr<T[]>::swap()");
            std::swap(m_ptr, other.m_ptr);
            std::swap(m_ctl, other.m_ctl);
        }

        /**
         * @brief Get the raw pointer
         * @return Raw pointer to the managed array
         */
        [[nodiscard]] constexpr element_type* get() const noexcept { return m_ptr; }

        /**
         * @brief Boolean conversion operator
         * @return true if managing an array, false otherwise
         */
        [[nodiscard]] constexpr operator bool() const noexcept { return m_ptr != nullptr; }

        /**
         * @brief Get the strong reference count
         * @return Current strong reference count
         */
        [[nodiscard]] constexpr long strong_count() const noexcept {
            return m_ctl ? m_ctl->strongCount.load(std::memory_order_acquire) : 0;
        }

        /**
         * @brief Get the deleter
         * @tparam Deleter Type of the deleter
         * @return Pointer to the deleter or nullptr if not found
         */
        template<typename Deleter>
        [[nodiscard]] constexpr Deleter* deleter() const noexcept {
            return m_ctl ? static_cast<Deleter*>(m_ctl->deleter(typeid(Deleter))) : nullptr;
        }

    private:
        element_type* m_ptr{nullptr};  ///< Pointer to the managed array
        detail::IControlBlockBase* m_ctl{nullptr};  ///< Control block pointer

        /**
         * @brief Private constructor from pointer and control block
         * @param ptr Pointer to the managed array
         * @param ctl Control block pointer
         */
        constexpr SharedPtr(element_type* ptr, detail::IControlBlockBase* ctl) noexcept :
            m_ptr(ptr), m_ctl(ctl) {
            std::println("SharedPtr<T[]> private constructor");
            detail::incr_strong_ref(m_ctl);
        }

        // Friend declarations for make_shared_array implementations
        template<typename U>
        friend constexpr SharedPtr<U[]> detail::make_shared_array_impl(size_t size);

        template<typename U, typename Alloc>
        friend constexpr SharedPtr<U[]> detail::alloc_shared_array_impl(const Alloc& alloc,
                                                                        size_t size);

        friend class WeakPtr<element_type>;
    };

    /**
     * @brief Weak pointer class
     * @tparam T Type of the managed object
     */
    template<typename T>
    class WeakPtr {
    public:
        using element_type = typename SharedPtr<T>::element_type;  ///< Type of the managed object

        /// @brief Default constructor
        constexpr WeakPtr() noexcept = default;

        /**
         * @brief Construct from SharedPtr
         * @tparam U Type convertible to T
         * @param other SharedPtr to create WeakPtr from
         */
        template<typename U>
        constexpr WeakPtr(const SharedPtr<U>& other) noexcept
            requires std::convertible_to<U*, element_type*>
            : m_ptr(other.m_ptr), m_ctl(other.m_ctl) {
            std::println("WeakPtr constructor from SharedPtr");
            detail::incr_weak_ref(m_ctl);
        }

        /**
         * @brief Copy constructor
         * @param other WeakPtr to copy from
         */
        constexpr WeakPtr(const WeakPtr& other) noexcept : m_ptr(other.m_ptr), m_ctl(other.m_ctl) {
            std::println("WeakPtr copy constructor");
            detail::incr_weak_ref(m_ctl);
        }

        /**
         * @brief Move constructor
         * @param other WeakPtr to move from
         */
        constexpr WeakPtr(WeakPtr&& other) noexcept :
            m_ptr(std::exchange(other.m_ptr, nullptr)), m_ctl(std::exchange(other.m_ctl, nullptr)) {
            std::println("WeakPtr move constructor");
        }

        /// @brief Destructor
        ~WeakPtr() {
            std::println("~WeakPtr() - ptr={}, ctl={}, weakCount={}",
                         static_cast<void*>(m_ptr),
                         static_cast<void*>(m_ctl),
                         m_ctl ? m_ctl->weakCount.load() : 0);
            detail::release_weak_ref(m_ctl);
        }

        /**
         * @brief Copy assignment operator
         * @param other WeakPtr to copy from
         * @return Reference to this WeakPtr
         */
        constexpr WeakPtr& operator=(const WeakPtr& other) noexcept {
            std::println("WeakPtr copy assignment");
            WeakPtr(other).swap(*this);
            return *this;
        }

        /**
         * @brief Move assignment operator
         * @param other WeakPtr to move from
         * @return Reference to this WeakPtr
         */
        constexpr WeakPtr& operator=(WeakPtr&& other) noexcept {
            std::println("WeakPtr move assignment");
            WeakPtr(std::move(other)).swap(*this);
            return *this;
        }

        /**
         * @brief Attempt to create a SharedPtr from this WeakPtr
         * @return SharedPtr if object still exists, empty SharedPtr otherwise
         */
        [[nodiscard]] constexpr SharedPtr<T> lock() const noexcept {
            std::println("WeakPtr::lock() attempt");
            auto result = detail::weak_ptr_lock_impl(m_ptr, m_ctl);
            std::println("WeakPtr::lock() result - {}", result ? "success" : "failed");
            return result;
        }

        /**
         * @brief Get the strong reference count
         * @return Current strong reference count
         */
        [[nodiscard]] constexpr long strong_count() const noexcept {
            auto count = m_ctl ? m_ctl->strongCount.load(std::memory_order_acquire) : 0;
            std::println("WeakPtr::strong_count() = {}", count);
            return count;
        }

        /**
         * @brief Check if the managed object has been deleted
         * @return true if no more SharedPtrs exist, false otherwise
         */
        [[nodiscard]] constexpr bool expired() const noexcept {
            bool exp = strong_count() == 0;
            std::println("WeakPtr::expired() = {}", exp);
            return exp;
        }

        /**
         * @brief Swap contents with another WeakPtr
         * @param other WeakPtr to swap with
         */
        constexpr void swap(WeakPtr& other) noexcept {
            std::println("WeakPtr::swap()");
            std::swap(m_ptr, other.m_ptr);
            std::swap(m_ctl, other.m_ctl);
        }

    private:
        element_type* m_ptr{nullptr};  ///< Pointer to the managed object
        detail::IControlBlockBase* m_ctl{nullptr};  ///< Control block pointer
    };

    /**
     * @brief Weak pointer specialization for arrays
     * @tparam T Array type
     */
    template<typename T>
    class WeakPtr<T[]> {
    public:
        using element_type = std::remove_extent_t<T>;  ///< Type of array elements

        // Disable dereference operators for arrays
        constexpr element_type& operator*() const = delete;
        constexpr element_type* operator->() const = delete;

        /// @brief Default constructor
        constexpr WeakPtr() noexcept = default;

        /**
         * @brief Construct from SharedPtr<T[]>
         * @param other SharedPtr<T[]> to create WeakPtr from
         */
        constexpr WeakPtr(const SharedPtr<T[]>& other) noexcept :
            m_ptr(other.m_ptr), m_ctl(other.m_ctl) {
            std::println("WeakPtr<T[]> constructor from SharedPtr<T[]>");
            detail::incr_weak_ref(m_ctl);
        }

        /**
         * @brief Copy constructor
         * @param other WeakPtr to copy from
         */
        constexpr WeakPtr(const WeakPtr& other) noexcept : m_ptr(other.m_ptr), m_ctl(other.m_ctl) {
            std::println("WeakPtr<T[]> copy constructor");
            detail::incr_weak_ref(m_ctl);
        }

        /**
         * @brief Move constructor
         * @param other WeakPtr to move from
         */
        constexpr WeakPtr(WeakPtr&& other) noexcept :
            m_ptr(std::exchange(other.m_ptr, nullptr)), m_ctl(std::exchange(other.m_ctl, nullptr)) {
            std::println("WeakPtr<T[]> move constructor");
        }

        /// @brief Destructor
        ~WeakPtr() {
            std::println("~WeakPtr<T[]>() - ptr={}, ctl={}, weakCount={}",
                         static_cast<void*>(m_ptr),
                         static_cast<void*>(m_ctl),
                         m_ctl ? m_ctl->weakCount.load() : 0);
            detail::release_weak_ref(m_ctl);
        }

        /**
         * @brief Copy assignment operator
         * @param other WeakPtr to copy from
         * @return Reference to this WeakPtr
         */
        constexpr WeakPtr& operator=(const WeakPtr& other) noexcept {
            std::println("WeakPtr<T[]> copy assignment");
            WeakPtr(other).swap(*this);
            return *this;
        }

        /**
         * @brief Move assignment operator
         * @param other WeakPtr to move from
         * @return Reference to this WeakPtr
         */
        constexpr WeakPtr& operator=(WeakPtr&& other) noexcept {
            std::println("WeakPtr<T[]> move assignment");
            WeakPtr(std::move(other)).swap(*this);
            return *this;
        }

        /**
         * @brief Attempt to create a SharedPtr from this WeakPtr
         * @return SharedPtr<T[]> if array still exists, empty SharedPtr otherwise
         */
        [[nodiscard]] constexpr SharedPtr<T[]> lock() const noexcept {
            std::println("WeakPtr<T[]>::lock() attempt");
            auto result = detail::weak_ptr_lock_impl(m_ptr, m_ctl);
            std::println("WeakPtr<T[]>::lock() result - {}", result ? "success" : "failed");
            return result;
        }

        /**
         * @brief Get the strong reference count
         * @return Current strong reference count
         */
        [[nodiscard]] constexpr long strong_count() const noexcept {
            auto count = m_ctl ? m_ctl->strongCount.load(std::memory_order_acquire) : 0;
            std::println("WeakPtr<T[]>::strong_count() = {}", count);
            return count;
        }

        /**
         * @brief Check if the managed array has been deleted
         * @return true if no more SharedPtrs exist, false otherwise
         */
        [[nodiscard]] constexpr bool expired() const noexcept {
            bool exp = strong_count() == 0;
            std::println("WeakPtr<T[]>::expired() = {}", exp);
            return exp;
        }

        /**
         * @brief Swap contents with another WeakPtr
         * @param other WeakPtr to swap with
         */
        constexpr void swap(WeakPtr& other) noexcept {
            std::println("WeakPtr<T[]>::swap()");
            std::swap(m_ptr, other.m_ptr);
            std::swap(m_ctl, other.m_ctl);
        }

    private:
        element_type* m_ptr{nullptr};  ///< Pointer to the managed array
        detail::IControlBlockBase* m_ctl{nullptr};  ///< Control block pointer
    };

    /**
     * @brief Create a shared pointer with default allocator
     * @tparam T Type of object to create
     * @tparam Args Argument types for constructor
     * @param args Arguments for constructor
     * @return SharedPtr<T> managing the new object
     */
    template<typename T, typename... Args>
    [[nodiscard]] constexpr SharedPtr<T> make_shared(Args&&... args) {
        return detail::make_shared_impl<T>(args...);
    }

    /**
     * @brief Create a shared pointer with custom allocator
     * @tparam T Type of object to create
     * @tparam Alloc Allocator type
     * @tparam Args Argument types for constructor
     * @param alloc Allocator to use
     * @param args Arguments for constructor
     * @return SharedPtr<T> managing the new object
     */
    template<typename T, typename Alloc, typename... Args>
    [[nodiscard]] constexpr SharedPtr<T> allocated_shared(const Alloc& alloc, Args&&... args) {
        return detail::alloc_shared_impl<T>(alloc, args...);
    }

    /**
     * @brief Create a shared pointer to an array with default allocator
     * @tparam T Array element type
     * @param size Number of elements in array
     * @return SharedPtr<T[]> managing the new array
     */
    template<typename T>
    [[nodiscard]] constexpr SharedPtr<T[]> make_shared_array(size_t size) {
        return detail::make_shared_array_impl<T>(size);
    }

    /**
     * @brief Create a shared pointer to an array with custom allocator
     * @tparam T Array element type
     * @tparam Alloc Allocator type
     * @param alloc Allocator to use
     * @param size Number of elements in array
     * @return SharedPtr<T[]> managing the new array
     */
    template<typename T, typename Alloc>
    [[nodiscard]] constexpr SharedPtr<T[]> allocate_shared_array(const Alloc& alloc, size_t size) {
        return detail::alloc_shared_array_impl<T>(alloc, size);
    }
}  // namespace sp
