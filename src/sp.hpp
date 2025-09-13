#pragma once

#include <atomic>
#include <cstddef>
#include <memory>
#include <type_traits>
#include <typeinfo>
#include <utility>

// #define SP_DEBUG 1

#ifdef SP_DEBUG
#include <print>
#define SP_LOG(...) std::println(__VA_ARGS__)
#else
#define SP_LOG(...) (void) 0
#endif

namespace sp {
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
            std::atomic_size_t strongCnt{0};  ///< Strong reference count
            std::atomic_size_t weakCnt{0};  ///< Weak reference count

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
            constexpr explicit ControlBlockDirect(Deleter d, Alloc alloc, Args&&... args)
              : m_deleter(std::move(d)), m_alloc(std::move(alloc)) {
                std::construct_at(ptr(), std::forward<Args>(args)...);
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
            // clang-format off
            alignas(T) std::array<std::byte, sizeof(T)> m_storage;  ///< Storage for the managed object
            // clang-format on

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
            constexpr explicit ControlBlockPtr(T* ptr, Deleter d, Alloc alloc)
              : m_ptr(ptr), m_deleter(std::move(d)), m_alloc(std::move(alloc)) {}

            /**
             * @brief Destroy the managed object
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
            constexpr explicit ControlBlockPtr(T* ptr, Deleter d, Alloc alloc)
              : m_ptr(ptr), m_deleter(std::move(d)), m_alloc(std::move(alloc)) {}

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
        inline constexpr void incr_ref(IControlBlockBase* ctl,
                                       std::atomic_size_t& counter) noexcept {
            if (ctl) {
                counter.fetch_add(1, std::memory_order_acq_rel);
            }
        }

        /**
         * @brief Increment strong reference count
         * @param ctl Control block pointer
         */
        inline constexpr void incr_strong_ref(IControlBlockBase* ctl) noexcept {
            if (ctl) {
                incr_ref(ctl, ctl->strongCnt);
            }
        }

        /**
         * @brief Increment weak reference count
         * @param ctl Control block pointer
         */
        inline constexpr void incr_weak_ref(IControlBlockBase* ctl) noexcept {
            if (ctl) {
                incr_ref(ctl, ctl->weakCnt);
            }
        }

        /**
         * @brief Release shared reference
         * @param ctl Control block pointer
         */
        inline constexpr void release_shared_ref(IControlBlockBase* ctl) noexcept {
            if (!ctl) {
                return;
            }

            auto oldCount = ctl->strongCnt.fetch_sub(1, std::memory_order_acq_rel);
            if (oldCount == 1) {
                ctl->destroy_object();
                if (ctl->weakCnt.load(std::memory_order_acquire) == 0) {
                    ctl->destroy_block();
                }
            }
        }

        /**
         * @brief Release weak reference
         * @param ctl Control block pointer
         */
        inline constexpr void release_weak_ref(IControlBlockBase* ctl) noexcept {
            if (ctl && ctl->weakCnt.fetch_sub(1, std::memory_order_release) == 1) {
                if (ctl->strongCnt.load(std::memory_order_acquire) == 0) {
                    ctl->destroy_block();
                }
            }
        }

        template<typename T, typename U, typename Deleter, typename Alloc>
        constexpr IControlBlockBase* create_ctl_block_array(U* ptr, Deleter&& d, Alloc&& alloc) {
            if (!ptr) {
                return nullptr;
            }

            using RawAlloc = std::remove_cvref_t<Alloc>;
            using RawDeleter = std::remove_cvref_t<Deleter>;
            using Block = ControlBlockPtr<T[], RawDeleter, RawAlloc>;
            using BlockAlloc =
                typename std::allocator_traits<RawAlloc>::template rebind_alloc<Block>;

            BlockAlloc blockAlloc(std::forward<Alloc>(alloc));
            auto* block = blockAlloc.allocate(1);

            try {
                std::construct_at(block, ptr, std::forward<Deleter>(d), std::forward<Alloc>(alloc));
                return block;
            } catch (...) {
                blockAlloc.deallocate(block, 1);
                throw;
            }
        }

        template<typename T, typename U, typename Deleter, typename Alloc>
        constexpr IControlBlockBase* create_ctl_block_single(U* ptr, Deleter&& d, Alloc&& alloc) {
            if (!ptr) {
                return nullptr;
            }

            using RawAlloc = std::remove_cvref_t<Alloc>;
            using RawDeleter = std::remove_cvref_t<Deleter>;
            using Block = ControlBlockPtr<T, RawDeleter, RawAlloc>;
            using BlockAlloc =
                typename std::allocator_traits<RawAlloc>::template rebind_alloc<Block>;

            BlockAlloc blockAlloc(std::forward<Alloc>(alloc));
            auto* block = blockAlloc.allocate(1);

            try {
                std::construct_at(block, ptr, std::forward<Deleter>(d), std::forward<Alloc>(alloc));
                return block;
            } catch (...) {
                blockAlloc.deallocate(block, 1);
                throw;
            }
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
                auto count = ctl->strongCnt.load(std::memory_order_acquire);
                while (count != 0) {
                    if (ctl->strongCnt.compare_exchange_weak(count,
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

            ElementAlloc elementAlloc(alloc);
            T* ptr = std::allocator_traits<ElementAlloc>::allocate(elementAlloc, size);
            size_t constructed = 0;

            try {
                std::uninitialized_default_construct_n(ptr, size);
                constructed = size;
                Deleter deleter{elementAlloc, size};

                auto* block = detail::create_ctl_block_array<T>(ptr, deleter, alloc);
                return SharedPtr<T[]>(ptr, block);
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

        template<typename From, typename To>
        concept ConvertibleToPtr = std::convertible_to<From*, To*>;

        template<typename T>
        concept NotArray = !std::is_array_v<T>;

        template<class D, class U>
        concept DeleterFor = requires(D d, U* u) {
            { d(u) } -> std::same_as<void>;
        };

        template<class A, class U>
        concept AllocatorFor =
            requires(A a) { typename std::allocator_traits<A>::template rebind_alloc<U>; };
    }  // namespace detail

    template<typename T, bool IsArray>
    class SharedPtrBase {
    public:
        using element_type = std::remove_extent_t<T>;

        constexpr SharedPtrBase() noexcept = default;
        constexpr SharedPtrBase(std::nullptr_t) noexcept {}

        ~SharedPtrBase() { detail::release_shared_ref(m_ctl); }

        constexpr SharedPtrBase(const SharedPtrBase& other) noexcept
          : m_ptr(other.m_ptr), m_ctl(other.m_ctl) {
            detail::incr_strong_ref(m_ctl);
        }

        constexpr SharedPtrBase(SharedPtrBase&& other) noexcept
          : m_ptr(std::exchange(other.m_ptr, nullptr)),
            m_ctl(std::exchange(other.m_ctl, nullptr)) {}

        SharedPtrBase& operator=(const SharedPtrBase& other) noexcept {
            SharedPtrBase(other).swap(*this);
            return *this;
        }
        SharedPtrBase& operator=(SharedPtrBase&& other) noexcept {
            SharedPtrBase(std::move(other)).swap(*this);
            return *this;
        }

        [[nodiscard]] constexpr element_type* get_ptr() const noexcept { return m_ptr; }
        [[nodiscard]] constexpr detail::IControlBlockBase* get_ctl() const noexcept {
            return m_ctl;
        }

        [[nodiscard]] constexpr element_type* get() const noexcept { return m_ptr; }
        [[nodiscard]] constexpr operator bool() const noexcept { return m_ptr != nullptr; }
        [[nodiscard]] constexpr size_t strong_count() const noexcept {
            return m_ctl ? m_ctl->strongCnt.load(std::memory_order_acquire) : 0;
        }

        constexpr void reset() noexcept { SharedPtrBase().swap(*this); }
        constexpr void swap(SharedPtrBase& other) noexcept {
            std::swap(m_ptr, other.m_ptr);
            std::swap(m_ctl, other.m_ctl);
        }

        template<typename Deleter>
        [[nodiscard]] constexpr Deleter* deleter() const noexcept {
            return this->m_ctl ? static_cast<Deleter*>(this->m_ctl->deleter(typeid(Deleter)))
                               : nullptr;
        }

        template<typename U, bool UIsArray>
        friend class WeakPtrBase;

        template<typename U>
        friend constexpr SharedPtr<U> detail::weak_ptr_lock_impl(
            U* ptr, detail::IControlBlockBase* ctl) noexcept;

    protected:
        constexpr SharedPtrBase(element_type* p, detail::IControlBlockBase* c) noexcept
          : m_ptr(p), m_ctl(c) {
            detail::incr_strong_ref(m_ctl);
        }

        element_type* m_ptr{nullptr};
        detail::IControlBlockBase* m_ctl{nullptr};
    };

    /**
     * @brief Shared pointer class with reference counting
     * @tparam T Type of the managed object
     */
    template<typename T>
    class SharedPtr : public SharedPtrBase<T, false> {
    private:
        using Base = SharedPtrBase<T, false>;

    public:
        using Base::Base;
        using element_type = typename Base::element_type;

        /// @brief Construct from raw pointer
        template<typename U>
            requires detail::ConvertibleToPtr<U*, element_type*> && detail::NotArray<U>
        explicit SharedPtr(U* ptr)
          : SharedPtr(ptr, std::default_delete<U>{}, std::allocator<U>{}) {}

        /// @brief Construct from raw pointer with deleter
        template<typename U, typename Deleter>
            requires detail::ConvertibleToPtr<U*, element_type*> && detail::DeleterFor<Deleter, U>
        SharedPtr(U* ptr, Deleter d) : SharedPtr(ptr, std::move(d), std::allocator<U>{}) {}

        /// @brief Construct from raw pointer with deleter and allocator
        template<typename U, typename Deleter, typename Alloc>
            requires detail::ConvertibleToPtr<U*, element_type*> &&
                     detail::DeleterFor<Deleter, U> && detail::AllocatorFor<Alloc, U>
        SharedPtr(U* ptr, Deleter d, Alloc alloc) {
            this->m_ctl = detail::create_ctl_block_single<T>(ptr, std::move(d), std::move(alloc));
            this->m_ptr = ptr;
            detail::incr_strong_ref(this->m_ctl);
        }

        template<typename U>
            requires detail::ConvertibleToPtr<U, element_type> && detail::NotArray<U>
        SharedPtr(const SharedPtr<U>& other) noexcept : Base(other.get_ptr(), other.get_ctl()) {}

        template<typename U>
            requires detail::ConvertibleToPtr<U, element_type> && detail::NotArray<U>
        SharedPtr(SharedPtr<U>&& other) noexcept : Base(other.get_ptr(), other.get_ctl()) {
            other.m_ptr = nullptr;
            other.m_ctl = nullptr;
        }

        /**
         * @brief Dereference operator
         * @return Reference to the managed object
         */
        [[nodiscard]] constexpr element_type& operator*() const noexcept { return *this->m_ptr; }

        /**
         * @brief Member access operator
         * @return Pointer to the managed object
         */
        [[nodiscard]] constexpr element_type* operator->() const noexcept { return this->m_ptr; }

        /**
         * @brief Reset the SharedPtr to manage a new object
         * @tparam U Type of the new object (defaults to T)
         * @param ptr Pointer to the new object to manage
         */
        template<typename U = T>
        constexpr void reset(U* ptr = nullptr) {
            SharedPtr(ptr).swap(*this);
        }

    private:
        template<typename U, typename... Args>
        friend constexpr SharedPtr<U> detail::make_shared_impl(Args&&... args);

        template<typename U, typename Alloc, typename... Args>
        friend constexpr SharedPtr<U> detail::alloc_shared_impl(const Alloc& alloc, Args&&... args);
    };

    /**
     * @brief Shared pointer specialization for arrays
     * @tparam T Array type
     */
    template<typename T>
    class SharedPtr<T[]> : public SharedPtrBase<T[], true> {
    private:
        using Base = SharedPtrBase<T[], true>;

    public:
        using Base::Base;
        using element_type = typename Base::element_type;  ///< Type of array elements

        constexpr element_type& operator*() const = delete;
        constexpr element_type* operator->() const = delete;

        /**
         * @brief Array subscript operator
         * @param idx Index to access
         * @return Reference to the element at index
         */
        constexpr element_type& operator[](ptrdiff_t idx) const { return this->m_ptr[idx]; }

        /**
         * @brief Construct from raw pointer with deleter and allocator
         * @tparam U Array element type (must be same as T)
         * @tparam Deleter Deleter type (defaults to std::default_delete<U[]>)
         * @tparam Alloc Allocator type (defaults to std::allocator<U>)
         * @param ptr Raw pointer to manage
         * @param d Deleter to use
         * @param alloc Allocator to use
         */
        template<typename U, typename Deleter = std::default_delete<U[]>,
                 typename Alloc = std::allocator<U>>
        explicit SharedPtr(U* ptr, Deleter d = {}, Alloc alloc = {})
            requires std::same_as<U, element_type>
        {
            if (ptr) {
                this->m_ctl = detail::create_ctl_block_array<element_type>(
                    ptr, std::move(d), std::move(alloc));

                this->m_ptr = ptr;
                detail::incr_strong_ref(this->m_ctl);
            }
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
            SharedPtr(ptr).swap(*this);
        }

    private:
        template<typename U>
        friend constexpr SharedPtr<U[]> detail::make_shared_array_impl(size_t size);

        template<typename U, typename Alloc>
        friend constexpr SharedPtr<U[]> detail::alloc_shared_array_impl(const Alloc& alloc,
                                                                        size_t size);
    };

    template<typename T, bool IsArray>
    class WeakPtrBase {
    public:
        using element_type = std::remove_extent_t<T>;

        constexpr WeakPtrBase() noexcept = default;
        constexpr WeakPtrBase(std::nullptr_t) noexcept {}

        ~WeakPtrBase() { detail::release_weak_ref(m_ctl); }

        constexpr WeakPtrBase(const WeakPtrBase& other) noexcept
          : m_ptr(other.m_ptr), m_ctl(other.m_ctl) {
            detail::incr_weak_ref(m_ctl);
        }

        constexpr WeakPtrBase(WeakPtrBase&& other) noexcept
          : m_ptr(std::exchange(other.m_ptr, nullptr)),
            m_ctl(std::exchange(other.m_ctl, nullptr)) {}

        WeakPtrBase& operator=(const WeakPtrBase& other) noexcept {
            WeakPtrBase(other).swap(*this);
            return *this;
        }

        WeakPtrBase& operator=(WeakPtrBase&& other) noexcept {
            WeakPtrBase(std::move(other)).swap(*this);
            return *this;
        }

        [[nodiscard]] constexpr size_t strong_count() const noexcept {
            return m_ctl ? m_ctl->strongCnt.load(std::memory_order_acquire) : 0;
        }

        [[nodiscard]] constexpr size_t weak_count() const noexcept {
            return m_ctl ? m_ctl->weakCnt.load(std::memory_order_acquire) : 0;
        }

        [[nodiscard]] constexpr bool expired() const noexcept { return strong_count() == 0; }

        constexpr void reset() noexcept { WeakPtrBase().swap(*this); }
        constexpr void swap(WeakPtrBase& other) noexcept {
            std::swap(m_ptr, other.m_ptr);
            std::swap(m_ctl, other.m_ctl);
        }

        [[nodiscard]] constexpr SharedPtr<T> lock() const noexcept {
            return detail::weak_ptr_lock_impl(m_ptr, m_ctl);
        }

    protected:
        template<typename Shared>
        constexpr WeakPtrBase(const Shared& sp) noexcept : m_ptr(sp.m_ptr), m_ctl(sp.m_ctl) {
            detail::incr_weak_ref(m_ctl);
        }

        element_type* m_ptr{nullptr};
        detail::IControlBlockBase* m_ctl{nullptr};
    };

    /**
     * @brief Weak pointer class
     * @tparam T Type of the managed object
     */
    template<typename T>
    class WeakPtr : public WeakPtrBase<T, false> {
    private:
        using Base = WeakPtrBase<T, false>;

    public:
        using Base::Base;
        using element_type = typename SharedPtr<T>::element_type;

        template<typename U>
        constexpr WeakPtr(const SharedPtr<U>& other) noexcept
            requires detail::ConvertibleToPtr<U*, element_type*>
          : Base(other) {}
    };

    /**
     * @brief Weak pointer specialization for arrays
     * @tparam T Array type
     */
    template<typename T>
    class WeakPtr<T[]> : public WeakPtrBase<T[], true> {
        using Base = WeakPtrBase<T[], true>;

    public:
        using Base::Base;
        using element_type = typename Base::element_type;

        constexpr WeakPtr() noexcept = default;
        constexpr WeakPtr(const SharedPtr<T[]>& other) noexcept : Base(other) {}

        element_type& operator*() const = delete;
        element_type* operator->() const = delete;

        constexpr element_type& operator[](ptrdiff_t idx) const { return this->m_ptr[idx]; }
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
        return detail::make_shared_impl<T>(std::forward<Args>(args)...);
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
        return detail::alloc_shared_impl<T>(alloc, std::forward<Args>(args)...);
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
