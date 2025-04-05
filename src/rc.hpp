#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>

namespace rc {
    template<typename T>
    class ControlBlock {
    public:
        template<typename... Args>
        explicit ControlBlock(Args&&... args) {
            std::construct_at(ptr(), std::forward<Args>(args)...);
        }

        constexpr T* ptr() noexcept {
            return std::assume_aligned<alignof(T)>(std::launder(reinterpret_cast<T*>(&m_storage)));
        }

        constexpr const T* ptr() const noexcept {
            return std::assume_aligned<alignof(T)>(std::launder(reinterpret_cast<const T*>(&m_storage)));
        }

        constexpr void destroy() noexcept { std::destroy_at(ptr()); }

    private:
        uint64_t m_strongCount{1};
        uint64_t m_weakCount{0};
        alignas(T) std::byte m_storage[sizeof(T)];
    };
    
    template<typename T>
    class Rc {
    public:
    private:
        
    };
} // namespace rc
