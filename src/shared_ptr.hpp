#pragma once

#include <atomic>
#include <cstddef>
#include <memory>
#include <type_traits>
#include <typeinfo>
#include <utility>

namespace sp {
    template<typename T>
    class WeakPtr;

    class IControlBlockBase {
    public:
        std::atomic_int strongCount{1};
        std::atomic_int weakCount{0};

        virtual ~IControlBlockBase() = default;
        virtual void destroyObject() = 0;
        virtual void destroyBlock() = 0;
        virtual void* getDeleter(const std::type_info&) const noexcept = 0;
    };

    template<typename T, typename Deleter = std::default_delete<T>, typename Alloc = std::allocator<T>>
    class ControlBlockDirect : public IControlBlockBase {
    public:
        template<typename... Args>
        explicit ControlBlockDirect(Deleter d, Alloc alloc, Args&&... args) :
            m_deleter(std::move(d)), m_alloc(std::move(alloc)) {
            std::allocator_traits<T>::construct(m_alloc, getPtr(), std::forward<Args>(args)...);
        }

        void destroyObject() override { std::allocator_traits<T>::destroy(m_alloc, getPtr()); }
        void* getDeleter(const std::type_info& type) const noexcept override {
            if (type == typeid(Deleter)) {
                return const_cast<Deleter*>(&m_deleter);
            }
            if (type == typeid(Alloc)) {
                return const_cast<Alloc*>(&m_alloc);
            }
            return nullptr;
        }

        void destroyBlock() override {
            using BlockAlloc = std::allocator_traits<Alloc>::template rebind_alloc<ControlBlockDirect>;
            BlockAlloc blockAlloc;
            std::allocator_traits<BlockAlloc>::deallocate(blockAlloc, this, 1);
        }

        T* getPtr() noexcept { return reinterpret_cast<T*>(&m_storage); }

    private:
        alignas(T) std::byte m_storage[sizeof(T)];
        [[no_unique_address]] Deleter m_deleter;
        [[no_unique_address]] Alloc m_alloc;
    };

    template<typename T, typename Deleter = std::default_delete<T>, typename Alloc = std::allocator<T>>
    class ControlBlockPtr : public IControlBlockBase {
    public:
        explicit ControlBlockPtr(T* ptr, Deleter d, Alloc alloc) :
            m_ptr(ptr), m_deleter(std::move(d)), m_alloc(std::move(alloc)) {}

        void destroyObject() override { m_deleter(m_ptr); }
        void* getDeleter(const std::type_info& type) const noexcept override {
            if (type == typeid(Deleter)) {
                return const_cast<Deleter*>(&m_deleter);
            }
            if (type == typeid(Alloc)) {
                return const_cast<Alloc*>(&m_alloc);
            }
            return nullptr;
        }

        void destroyBlock() override {
            using BlockAlloc = typename std::allocator_traits<Alloc>::template rebind_alloc<ControlBlockPtr>;
            BlockAlloc blockAlloc(m_alloc);
            std::allocator_traits<BlockAlloc>::deallocate(blockAlloc, this, 1);
        }

        T* getPtr() noexcept { return m_ptr; }

    private:
        T* m_ptr;
        [[no_unique_address]] Deleter m_deleter;
        [[no_unique_address]] Alloc m_alloc;
    };

    template<typename T>
    class SharedPtr {
    public:
        using elemType = std::remove_extent_t<T>;

        constexpr SharedPtr() noexcept = default;
        constexpr SharedPtr(std::nullptr_t) noexcept {}

        template<typename U>
        explicit SharedPtr(U* ptr)
            requires std::derived_from<U, elemType> || std::same_as<U, elemType>
        {
            if (ptr) {
                using DefaultDeleter = std::default_delete<U>;
                using DefaultAlloc = std::allocator<U>;
                using Block = ControlBlockPtr<U, DefaultDeleter, DefaultAlloc>;

                auto alloc = DefaultAlloc();
                using BlockAlloc = typename std::allocator_traits<DefaultAlloc>::template rebind_alloc<Block>;
                BlockAlloc blockAlloc(alloc);

                auto* block = std::allocator_traits<BlockAlloc>::allocate(blockAlloc, 1);
                std::allocator_traits<BlockAlloc>::construct(blockAlloc, block, ptr, DefaultDeleter(), alloc);

                m_ptr = ptr;
                m_ctl = block;
            }
        }

        template<typename U, typename Deleter>
        SharedPtr(U* ptr, Deleter d)
            requires std::derived_from<U, elemType> || std::same_as<U, elemType>
        {
            if (ptr) {
                using DefaultAlloc = std::allocator<U>;
                using Block = ControlBlockPtr<U, Deleter, DefaultAlloc>;

                auto alloc = DefaultAlloc();
                using BlockAlloc = typename std::allocator_traits<DefaultAlloc>::template rebind_alloc<Block>;
                BlockAlloc blockAlloc(alloc);

                auto* block = std::allocator_traits<BlockAlloc>::allocate(blockAlloc, 1);
                std::allocator_traits<BlockAlloc>::construct(blockAlloc, block, ptr, std::move(d), alloc);

                m_ptr = ptr;
                m_ctl = block;
            }
        }

        template<typename U, typename Deleter, typename Alloc>
        SharedPtr(U* ptr, Deleter d, Alloc alloc)
            requires std::derived_from<U, elemType> || std::same_as<U, elemType>
        {
            if (ptr) {
                using Block = ControlBlockPtr<U, Deleter, Alloc>;
                using BlockAlloc = typename std::allocator_traits<Alloc>::template rebind_alloc<Block>;
                BlockAlloc blockAlloc(alloc);

                auto* block = std::allocator_traits<BlockAlloc>::allocate(blockAlloc, 1);
                std::allocator_traits<BlockAlloc>::construct(blockAlloc, block, ptr, std::move(d), alloc);

                m_ptr = ptr;
                m_ctl = block;
            }
        }

        ~SharedPtr() {
            if (m_ctl && m_ctl->strongCount.fetch_sub(1, std::memory_order_acq_rel) == 1) {
                m_ctl->destroyObject();
                if (m_ctl->weakCount.load(std::memory_order_acquire) == 0) {
                    m_ctl->destroyBlock();
                }
            }
        }

        SharedPtr(const SharedPtr& other) noexcept { copyConstructFrom(other); }
        SharedPtr(SharedPtr&& other) noexcept { moveContructFrom(std::move(other)); }

        template<typename U>
        SharedPtr(const SharedPtr<U>& other) noexcept
            requires std::derived_from<U, elemType>
        {
            copyConstructFrom(other);
        }

        template<typename U>
        SharedPtr(SharedPtr<U>&& other) noexcept
            requires std::derived_from<U, elemType>
        {
            moveConstructFrom(std::move(other));
        }

        template<typename U>
        SharedPtr(const SharedPtr<U>& other, elemType* ptr) noexcept
            requires std::derived_from<U, elemType>
        {
            m_ptr = ptr;
            m_ctl = other.m_ctl;
            if (m_ctl) {
                m_ctl->strongCount.fetch_add(1, std::memory_order_relaxed);
            }
        }

        SharedPtr& operator=(const SharedPtr& other) noexcept {
            if (this != &other) {
                SharedPtr(other).swap(*this);
            }
            return *this;
        }

        SharedPtr& operator=(SharedPtr&& other) noexcept {
            if (this != &other) {
                SharedPtr(std::move(other)).swap(*this);
            }
            return *this;
        }

        void reset() noexcept { SharedPtr().swap(*this); }

        template<typename U>
        void reset(U* ptr)
            requires std::derived_from<U, elemType> || std::same_as<U, elemType>
        {
            SharedPtr(ptr).swap(*this);
        }

        template<typename U, typename Deleter>
        void reset(U* ptr, Deleter d)
            requires std::derived_from<U, elemType> || std::same_as<U, elemType>
        {
            SharedPtr(ptr, std::move(d)).swap(*this);
        }

        template<typename U, typename Deleter, typename Alloc>
        void reset(U* ptr, Deleter d, Alloc alloc)
            requires std::derived_from<U, elemType> || std::same_as<U, elemType>
        {
            SharedPtr(ptr, std::move(d), alloc).swap(*this);
        }

        void swap(SharedPtr& other) noexcept {
            std::swap(m_ptr, other.m_ptr);
            std::swap(m_ctl, other.m_ctl);
        }

        elemType* get() const noexcept { return m_ptr; }
        elemType& operator*() const noexcept { return *m_ptr; }
        elemType* operator->() const noexcept { return m_ptr; }
        explicit operator bool() const noexcept { return m_ptr != nullptr; }
        long getStrongCount() const noexcept { return m_ctl ? m_ctl->strongCount.load(std::memory_order_acquire) : 0; }

        template<typename Deleter>
        Deleter* get_deleter() const noexcept {
            if (!m_ctl) {
                return nullptr;
            }
            return static_cast<Deleter*>(m_ctl->getDeleter(typeid(Deleter)));
        }

    private:
        T* m_ptr{nullptr};
        IControlBlockBase* m_ctl{nullptr};

        template<typename U>
        void copyConstructFrom(const SharedPtr<U>& other) noexcept {
            m_ptr = other.m_ptr;
            m_ctl = other.m_ctl;
            if (m_ctl) {
                m_ctl->strongCount.fetch_add(1, std::memory_order_relaxed);
            }
        }

        template<typename U>
        void moveContructFrom(SharedPtr<U>&& other) noexcept {
            m_ptr = other.m_ptr;
            m_ctl = other.m_ctl;
            other.m_ptr = nullptr;
            other.m_ctl = nullptr;
        }

        SharedPtr(T* ptr, IControlBlockBase* ctl) : m_ptr(ptr), m_ctl(ctl) {}

        template<typename U, typename... Args>
        friend SharedPtr<U> makeShared(Args&&... args);

        template<typename U, typename Alloc, typename... Args>
        friend SharedPtr<U> allocateShared(const Alloc& alloc, Args&&... args);

        friend class WeakPtr<elemType>;
    };

    template<typename T, typename... Args>
    SharedPtr<T> makeShared(Args&&... args) {
        using DefaultAlloc = std::allocator<T>;
        using Block = ControlBlockDirect<T, std::default_delete<T>, DefaultAlloc>;

        DefaultAlloc alloc;
        using BlockAlloc = typename std::allocator_traits<DefaultAlloc>::template rebind_alloc<Block>;
        BlockAlloc blockAlloc(alloc);

        auto* block = std::allocator_traits<BlockAlloc>::allocate(blockAlloc, 1);
        std::allocator_traits<BlockAlloc>::construct(blockAlloc, block, std::default_delete<T>{}, alloc,
                                                     std::forward<Args>(args)...);

        return SharedPtr<T>(block->getPtr(), block);
    }

    template<typename T, typename Alloc, typename... Args>
    SharedPtr<T> allocateShared(const Alloc& alloc, Args&&... args) {
        using Block = ControlBlockDirect<T, std::default_delete<T>, Alloc>;
        using BlockAlloc = typename std::allocator_traits<Alloc>::template rebind_alloc<Block>;

        BlockAlloc blockAlloc(alloc);
        auto* block = std::allocator_traits<BlockAlloc>::allocate(blockAlloc, 1);
        std::allocator_traits<BlockAlloc>::construct(blockAlloc, block, std::default_delete<T>{}, alloc,
                                                     std::forward<Args>(args)...);

        return SharedPtr<T>(block->getPtr(), block);
    }

    template<typename T>
    class WeakPtr {
    public:
        using elemType = typename SharedPtr<T>::elemType;

        constexpr WeakPtr() noexcept = default;
        WeakPtr(const SharedPtr<T>& other) noexcept {
            if (other.m_ctl) {
                m_ptr = other.m_ptr;
                m_ctl = other.m_ctl;
                m_ctl->weakCount.fetch_add(1, std::memory_order_relaxed);
            }
        }

        ~WeakPtr() {
            if (m_ctl && m_ctl->weakCount.fetch_sub(1, std::memory_order_acq_rel) == 1) {
                if (m_ctl->strongCount.load(std::memory_order_acquire) == 0) {
                    m_ctl->destroyBlock();
                }
            }
        }

        WeakPtr(const WeakPtr& other) noexcept {
            if (other.m_ctl) {
                m_ptr = other.m_ptr;
                m_ctl = other.m_ctl;
                m_ctl->weakCount.fetch_add(1, std::memory_order_relaxed);
            }
        }

        WeakPtr(WeakPtr&& other) noexcept {
            m_ptr = other.m_ptr;
            m_ctl = other.m_ctl;
            other.m_ptr = nullptr;
            other.m_ctl = nullptr;
        }

        template<typename U>
        WeakPtr(const WeakPtr<U>& other) noexcept
            requires std::derived_from<U, elemType>
        {
            if (other.m_ctl) {
                m_ptr = other.m_ptr;
                m_ctl = other.m_ctl;
                m_ctl->weakCount.fetch_add(1, std::memory_order_relaxed);
            }
        }

        template<typename U>
        WeakPtr(WeakPtr<U>&& other) noexcept
            requires std::derived_from<U, elemType>
        {
            m_ptr = other.m_ptr;
            m_ctl = other.m_ctl;
            other.m_ptr = nullptr;
            other.m_ctl = nullptr;
        }

        WeakPtr& operator=(const WeakPtr& other) noexcept {
            WeakPtr(other).swap(*this);
            return *this;
        }

        WeakPtr& operator=(WeakPtr&& other) noexcept {
            WeakPtr(std::move(other)).swap(*this);
            return *this;
        }

        void swap(WeakPtr& other) noexcept {
            std::swap(m_ptr, other.m_ptr);
            std::swap(m_ctl, other.m_ctl);
        }

        SharedPtr<T> lock() const noexcept {
            SharedPtr<T> result;
            if (m_ctl) {
                auto count = m_ctl->strongCount.load(std::memory_order_relaxed);
                do {
                    if (count == 0) {
                        break;
                    }
                } while (!m_ctl->strongCount.compare_exchange_weak(count, count + 1, std::memory_order_acquire,
                                                                   std::memory_order_relaxed));

                if (count != 0) {
                    result.m_ptr = m_ptr;
                    result.m_ctl = m_ctl;
                }
            }
            return result;
        }

        long getStrongCount() const noexcept { return m_ctl ? m_ctl->strongCount.load(std::memory_order_acquire) : 0; }
        bool expired() const noexcept { return getStrongCount() == 0; }

    private:
        elemType* m_ptr{nullptr};
        IControlBlockBase* m_ctl{nullptr};
    };
} // namespace sp
