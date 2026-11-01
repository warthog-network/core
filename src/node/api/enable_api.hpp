#pragma once
#include "general/logger/timing_logger.hpp"
#include "general/result.hpp"
#include "wrt/variant.hpp"
#include <utility>

template <typename T>
struct APIResponseType {
    using response_t = Result<T>;
};

template <>
struct APIResponseType<void> {
    using response_t = Error;
};

template <typename DerivedRequest, typename ResultType, typename... Args>
struct APIRequest : public std::tuple<Args...> {
    using base_tuple_t = std::tuple<Args...>;
    using result_t = ResultType;
    using base_tuple_t::base_tuple_t;
    using is_api_request = std::true_type;
    using Response = typename APIResponseType<ResultType>::response_t;
    using Callback = std::function<void(const Response&)>;
    static constexpr size_t size() { return sizeof...(Args); }

    template <size_t i>
    requires(i < sizeof...(Args))
    auto&& get(this auto&& self)
    {
        return std::get<i>(std::forward<decltype(self)>(self));
    }
    struct Object {
        using is_api_object = std::true_type;
        using Request = DerivedRequest;
        DerivedRequest request;
        Callback callback;
    };
};

template <typename T>
concept IsAPIRequest = requires {
    typename T::is_api_request;
} && std::is_same_v<typename T::is_api_request, std::true_type>;

template <typename... Ts>
struct TypeCollection { };
template <typename, typename... Ts>
using TypeCollectionWithoutFirst = TypeCollection<Ts...>;

template <typename T>
concept IsAPIObject = requires {
    typename T::is_api_object;
} && std::is_same_v<typename T::is_api_object, std::true_type>;

#define COMMA_FIRST(_0, ...) , _0
#define DEFINE_TYPE_COLLECTION(name, mapper) \
    mapper(DEFINE_API) using name = TypeCollectionWithoutFirst<void mapper(COMMA_FIRST)>;

template <typename Host, typename... Requests>
struct enable_api_methods;

template <typename Host, typename... Requests>
struct enable_api_methods<Host, TypeCollection<Requests...>> {

    std::optional<logging::TimingSession> timing;
    template <typename T>
    using object_t = T::Object;
    template <typename... T>
    using events_t = wrt::variant<T..., object_t<Requests>...>;

    template <typename T>
    static constexpr bool supports { (std::is_same_v<T, Requests> || ...) };

protected:
    void dispatch_event(auto&& event)
    {
        visit([&]<typename E>(E&& e) {
            Host* phost { static_cast<Host*>(this) };
            if constexpr (IsAPIObject<std::remove_cvref_t<E>>) {
                std::optional<logging::TimingObject> t;
                if (timing) {
                    t.emplace(timing->time(E::Request::name));
                }
                try {
                    if constexpr (std::is_same_v<void, decltype(phost->handle_api(std::move(e.request)))>) {
                        phost->handle_api(std::move(e.request));
                        e.callback({});
                    } else {
                        e.callback(phost->handle_api(std::move(e.request)));
                    }
                } catch (Error& err) {
                    e.callback(err);
                }
            } else {
                phost->handle_event(std::forward<E>(e));
            }
        },
            std::forward<decltype(event)>(event));
    }
};

// Accessor for one request field. Deducing this preserves the value category of
// the request, so std::move(request).field() yields a reference that a by-value
// callee can move from. The returned reference points into the request, so the
// accessor must not be called on a temporary.
#define DEFINE_API_ACCESSOR(field, index)                                \
    auto&& field(this auto&& self)                                       \
    {                                                                    \
        return std::forward<decltype(self)>(self).template get<index>(); \
    }

#define DEFINE_API_0(structname, restype)                        \
    struct structname : public APIRequest<structname, restype> { \
        static constexpr const char name[] = #structname;        \
        using APIRequest<structname, restype>::APIRequest;       \
    };

#define DEFINE_API_1(structname, restype, type0, name0)                 \
    struct structname : public APIRequest<structname, restype, type0> { \
        static constexpr const char name[] = #structname;               \
        using APIRequest<structname, restype, type0>::APIRequest;       \
        DEFINE_API_ACCESSOR(name0, 0)                                   \
    };
#define DEFINE_API_2(structname, restype, type0, name0, type1, name1)          \
    struct structname : public APIRequest<structname, restype, type0, type1> { \
        using APIRequest<structname, restype, type0, type1>::APIRequest;       \
        static constexpr const char name[] = #structname;                      \
        DEFINE_API_ACCESSOR(name0, 0)                                          \
        DEFINE_API_ACCESSOR(name1, 1)                                          \
    };
#define DEFINE_API_3(structname, restype, type0, name0, type1, name1, type2, name2)   \
    struct structname : public APIRequest<structname, restype, type0, type1, type2> { \
        using APIRequest<structname, restype, type0, type1, type2>::APIRequest;       \
        static constexpr const char name[] = #structname;                             \
        DEFINE_API_ACCESSOR(name0, 0)                                                 \
        DEFINE_API_ACCESSOR(name1, 1)                                                 \
        DEFINE_API_ACCESSOR(name2, 2)                                                 \
    };
#define DEFINE_API_4(structname, restype, type0, name0, type1, name1, type2, name2, type3, name3) \
    struct structname : public APIRequest<structname, restype, type0, type1, type2, type3> {      \
        using APIRequest<structname, restype, type0, type1, type2, type3>::APIRequest;            \
        static constexpr const char name[] = #structname;                                         \
        DEFINE_API_ACCESSOR(name0, 0)                                                             \
        DEFINE_API_ACCESSOR(name1, 1)                                                             \
        DEFINE_API_ACCESSOR(name2, 2)                                                             \
        DEFINE_API_ACCESSOR(name3, 3)                                                             \
    };
#define DEFINE_API_5(structname, restype, type0, name0, type1, name1, type2, name2, type3, name3, type4, name4) \
    struct structname : public APIRequest<structname, restype, type0, type1, type2, type3, type4> {             \
        using APIRequest<structname, restype, type0, type1, type2, type3, type4>::APIRequest;                   \
        static constexpr const char name[] = #structname;                                                       \
        DEFINE_API_ACCESSOR(name0, 0)                                                                           \
        DEFINE_API_ACCESSOR(name1, 1)                                                                           \
        DEFINE_API_ACCESSOR(name2, 2)                                                                           \
        DEFINE_API_ACCESSOR(name3, 3)                                                                           \
        DEFINE_API_ACCESSOR(name4, 4)                                                                           \
    };
#define GET_MACRO(_1, _2, _3, _4, _5, _6, _7, _8, _9, _10, _11, _12, NAME, ...) NAME
#define DEFINE_API(...) GET_MACRO(__VA_ARGS__, DEFINE_API_5, DEFINE_API_5, DEFINE_API_4, DEFINE_API_4, \
    DEFINE_API_3, DEFINE_API_3, DEFINE_API_2, DEFINE_API_2, DEFINE_API_1, DEFINE_API_1, DEFINE_API_0)(__VA_ARGS__)
