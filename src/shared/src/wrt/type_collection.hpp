#pragma once
namespace wrt {

template <typename T1, typename T2>
struct same_t {
    static constexpr bool value = false;
};
template <typename T>
struct same_t<T, T> {
    static constexpr bool value = true;
};

template<typename T>
struct TypeWrapper {
    using type = T;
};

template <typename... Ts>
struct TypeCollection {
    template <typename T>
    static constexpr bool contains = (same_t<T, Ts>::value || ...);
    static constexpr auto size = sizeof...(Ts);
};
}
