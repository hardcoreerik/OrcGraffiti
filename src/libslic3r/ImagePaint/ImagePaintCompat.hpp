#pragma once
// C++17 compatibility shims.
// std::expected is C++23; std::span is C++20.
// These minimal implementations cover only the subset used by ImagePaint.

#include <variant>
#include <utility>
#include <cstddef>
#include <type_traits>
#include <vector>

namespace Slic3r::ImagePaint {

// ---------------------------------------------------------------------------
// make_unexpected / Expected<T, E>  (std::unexpected / std::expected C++23)
// ---------------------------------------------------------------------------

template<class E>
struct UnexpectedHolder {
    E value;
    explicit UnexpectedHolder(E v) : value(std::move(v)) {}
};

template<class E>
inline UnexpectedHolder<E> make_unexpected(E e)
{
    return UnexpectedHolder<E>{std::move(e)};
}

template<class T, class E>
class Expected {
    std::variant<T, E> m_data;
public:
    // Construct with success value.
    Expected(T val) : m_data(std::in_place_index<0>, std::move(val)) {}  // NOLINT
    // Construct with error.
    Expected(UnexpectedHolder<E> u) : m_data(std::in_place_index<1>, std::move(u.value)) {}  // NOLINT

    bool has_value() const noexcept { return m_data.index() == 0; }
    explicit operator bool() const noexcept { return has_value(); }

    T&       operator*() &       { return std::get<0>(m_data); }
    const T& operator*() const & { return std::get<0>(m_data); }
    T*       operator->()        { return &std::get<0>(m_data); }
    const T* operator->() const  { return &std::get<0>(m_data); }

    T&       value() &        { return std::get<0>(m_data); }
    const T& value() const &  { return std::get<0>(m_data); }

    E&       error() &        { return std::get<1>(m_data); }
    const E& error() const &  { return std::get<1>(m_data); }
};

// ---------------------------------------------------------------------------
// Span<T>  (std::span C++20) — non-owning view, range-for compatible.
// ---------------------------------------------------------------------------

template<class T>
class Span {
    T*          m_data = nullptr;
    std::size_t m_size = 0;
public:
    constexpr Span() = default;
    constexpr Span(T* data, std::size_t size) noexcept : m_data(data), m_size(size) {}

    // Implicit conversion from std::vector<U> where U* is convertible to T*.
    template<class U,
             class = std::enable_if_t<std::is_convertible<U*, T*>::value>>
    Span(std::vector<U>& v) noexcept : m_data(v.data()), m_size(v.size()) {}  // NOLINT

    template<class U,
             class = std::enable_if_t<std::is_convertible<const U*, T*>::value>>
    Span(const std::vector<U>& v) noexcept : m_data(v.data()), m_size(v.size()) {}  // NOLINT

    constexpr T*          begin()      const noexcept { return m_data; }
    constexpr T*          end()        const noexcept { return m_data + m_size; }
    constexpr std::size_t size()       const noexcept { return m_size; }
    constexpr std::size_t size_bytes() const noexcept { return m_size * sizeof(T); }
    constexpr bool        empty()      const noexcept { return m_size == 0; }
    constexpr T&          operator[](std::size_t i) const noexcept { return m_data[i]; }
    constexpr T*          data()       const noexcept { return m_data; }
};

} // namespace Slic3r::ImagePaint
