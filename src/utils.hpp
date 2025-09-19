#pragma once

#include <array>
#include <atomic>
#include <format>
#include <functional>
#include <print>
#include <source_location>
#include <sstream>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

namespace testing {
    namespace detail {
        template<typename T>
        concept Formattable = requires(const T& value) {
            { std::format("{}", value) } -> std::convertible_to<std::string>;
        };

        template<typename T>
        concept Streamable = requires(const T& value, std::ostream& os) {
            { os << value } -> std::same_as<std::ostream&>;
        };

        template<typename T>
        [[nodiscard]] auto format_value(const T& value) -> std::string {
            if constexpr (std::is_pointer_v<std::remove_cvref_t<T>>) {
                if (value == nullptr) {
                    return "nullptr";
                }
                return std::format("0x{:016x}", reinterpret_cast<uintptr_t>(value));
            } else if constexpr (Formattable<T>) {
                return std::format("{}", value);
            } else {
                std::ostringstream oss;
                if constexpr (Streamable<T>) {
                    oss << value;
                    return oss.str();
                }
                return "[unformattable]";
            }
        }

        template<size_t N>
        struct FixedString {
            std::array<char, N> chars{};
            consteval FixedString(const char (&str)[N]) { std::copy_n(str, N, chars.data()); }
            constexpr operator std::string_view() const { return {chars.data(), N - 1}; }
            [[nodiscard]] constexpr std::string_view view() const { return {chars.data(), N - 1}; }
        };
    }  // namespace detail

    [[noreturn]] inline void fail(std::string_view message, const std::source_location& loc =
                                                                std::source_location::current()) {
        std::println(
            "\n\033[31mAssertion failed!\033[0m\n  Location: {}:{}\n  Function: {}\n  Message : {}",
            loc.file_name(),
            loc.line(),
            loc.function_name(),
            message);

        std::terminate();
    }

    template<detail::FixedString Expr>
    consteval void constexpr_assert(bool condition) {
        if (!condition) {
            throw std::logic_error(
                std::format("Compile-time assertion failed: {}", Expr.view()).c_str());
        }
    }

    template<detail::FixedString Expr>
    void assert_that(bool condition,
                     const std::source_location& loc = std::source_location::current()) {
        if (!condition) {
            fail(std::format("Assertion failed: {}", Expr.view()), loc);
        }
    }

    template<detail::FixedString Expr>
    void assert_that(bool condition, const std::string& info,
                     const std::source_location& loc = std::source_location::current()) {
        if (!condition) {
            fail(std::format("Assertion failed: {}\n  Info: {}", Expr.view(), info), loc);
        }
    }

    template<detail::FixedString Message, typename Lhs, typename Rhs>
    void assert_eq(Lhs&& lhs, Rhs&& rhs,
                   const std::source_location& loc = std::source_location::current()) {
        if (!(lhs == rhs)) {
            // clang-format off
            fail(std::format("Assertion failed: {}\n  Values: {} != {}",
                             Message.view(),
                             detail::format_value(std::forward<Lhs>(lhs)),
                             detail::format_value(std::forward<Rhs>(rhs))),
                             loc);
            // clang-format on
        }
    }

    template<detail::FixedString LhsExpr, detail::FixedString RhsExpr, typename Lhs, typename Rhs>
    void assert_eq(Lhs&& lhs, Rhs&& rhs,
                   const std::source_location& loc = std::source_location::current()) {
        if (!(lhs == rhs)) {
            // clang-format off
            fail(std::format("Assertion failed: {} == {}\n  Values: {} != {}",
                            LhsExpr.view(),
                            RhsExpr.view(),
                            detail::format_value(std::forward<Lhs>(lhs)),
                            detail::format_value(std::forward<Rhs>(rhs))),
                            loc);
            // clang-format on
        }
    }

    template<detail::FixedString LhsExpr, detail::FixedString RhsExpr, typename Lhs, typename Rhs>
    void assert_ne(Lhs&& lhs, Rhs&& rhs,
                   const std::source_location& loc = std::source_location::current()) {
        if (!(lhs != rhs)) {
            // clang-format off
            fail(std::format("Assertion failed: {} != {}\n  Both values: {}",
                            LhsExpr.view(),
                            RhsExpr.view(),
                            detail::format_value(std::forward<Lhs>(lhs))),
                            loc);
            // clang-format on
        }
    }

    struct TestCase {
        std::string_view name;
        std::function<void()> func;
        std::source_location location;
    };

    inline auto& test_registry() {
        static std::vector<TestCase> registry;
        return registry;
    }

    template<detail::FixedString Name, auto Func>
    struct TestRegistrar {
        TestRegistrar(const std::source_location& loc = std::source_location::current()) {
            test_registry().emplace_back(
                Name.view(),
                [] {
                    if constexpr (std::is_invocable_v<decltype(Func)>) {
                        try {
                            Func();
                        } catch (const std::exception& e) {
                            fail(std::format("Test threw exception: {}", e.what()));
                        } catch (...) {
                            fail("Test threw unknown exception");
                        }
                    }
                },
                loc);
        }
    };

    struct AllocationTracker {
        static inline std::atomic_int allocations{0};
        static inline std::atomic_int deallocations{0};

        static void reset() {
            allocations = 0;
            deallocations = 0;
        }

        static void check_balanced() {
            if (allocations != deallocations) {
                fail(std::format("Memory leak detected!\n  Allocations  : {}\n  Deallocations: {}",
                                 allocations.load(),
                                 deallocations.load()));
            }
        }

        [[nodiscard]] inline static void* operator new(size_t size) {
            allocations++;
            return std::malloc(size);
        }

        inline static void operator delete(void* ptr) noexcept {
            deallocations++;
            std::free(ptr);
        }

        inline static void operator delete(void* ptr, size_t) noexcept {
            deallocations++;
            std::free(ptr);
        }
    };
}  // namespace testing

#define TEST_CASE(name)                                                          \
    void name();                                                                 \
    [[maybe_unused]]                                                             \
    inline const auto _register_##name = testing::TestRegistrar<#name, &name>{}; \
    void name()
