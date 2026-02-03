#pragma once
#include <cstdint>
#include <span>

template <typename T>
concept RawSerializer = requires(T t, const std::span<const uint8_t>& s) {
    { t.write(s) };
} || requires(T t, size_t N) {
    { t.add_size(N) };
};

template <typename S, typename T>
concept RawSerializing = RawSerializer<S> && requires(S& s, const T& t) {
    { t.serialize(s) };
};

template <typename T>
concept MerkleSerializer = requires(T t) {
    { t.writer };
    { t.hook() };
};

template <typename T>
concept Serializer = RawSerializer<T> || MerkleSerializer<T>;

template <typename R>
concept IsReader = std::is_same_v<typename R::is_reader, std::true_type>;
