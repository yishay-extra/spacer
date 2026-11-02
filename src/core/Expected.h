#pragma once

// Minimal expected<T, E> (std::expected needs C++23; the project is C++20).

#include <cassert>
#include <utility>
#include <variant>

namespace spacer {

template <class E>
struct Unexpected {
    E error;
};
template <class E>
Unexpected(E) -> Unexpected<E>;

template <class T, class E>
class Expected {
public:
    Expected(T value) : v_(std::in_place_index<0>, std::move(value)) {}
    Expected(Unexpected<E> e) : v_(std::in_place_index<1>, std::move(e.error)) {}

    bool hasValue() const { return v_.index() == 0; }
    explicit operator bool() const { return hasValue(); }

    T& value() {
        assert(hasValue());
        return std::get<0>(v_);
    }
    const T& value() const {
        assert(hasValue());
        return std::get<0>(v_);
    }
    const E& error() const {
        assert(!hasValue());
        return std::get<1>(v_);
    }
    T* operator->() { return &value(); }
    const T* operator->() const { return &value(); }
    T& operator*() { return value(); }
    const T& operator*() const { return value(); }

private:
    std::variant<T, E> v_;
};

}  // namespace spacer
