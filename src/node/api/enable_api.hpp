#pragma once
#include "general/logger/timing_logger.hpp"
#include "general/result.hpp"
#include "wrt/variant.hpp"

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
    auto& get() const
    {
        return std::get<i>(*this);
    }

    template <size_t i>
    requires(i < sizeof...(Args))
    auto& get()
    {
        return std::get<i>(*this);
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

    wrt::optional<logging::TimingSession> timing;
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
                tl::optional<logging::TimingObject> t;
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

#define DEFINE_API_0(structname, restype)                        \
    struct structname : public APIRequest<structname, restype> { \
        static constexpr const char name[] = #structname;        \
        using APIRequest<structname, restype>::APIRequest;       \
    };

#define DEFINE_API_1(structname, restype, type0, name0)                 \
    struct structname : public APIRequest<structname, restype, type0> { \
        static constexpr const char name[] = #structname;               \
        using APIRequest<structname, restype, type0>::APIRequest;       \
        auto& name0() const { return get<0>(); }                        \
    };
#define DEFINE_API_2(structname, restype, type0, name0, type1, name1)          \
    struct structname : public APIRequest<structname, restype, type0, type1> { \
        using APIRequest<structname, restype, type0, type1>::APIRequest;       \
        static constexpr const char name[] = #structname;                      \
        auto& name0() const { return get<0>(); }                               \
        auto& name1() const { return get<1>(); }                               \
    };
#define DEFINE_API_3(structname, restype, type0, name0, type1, name1, type2, name2)   \
    struct structname : public APIRequest<structname, restype, type0, type1, type2> { \
        using APIRequest<structname, restype, type0, type1, type2>::APIRequest;       \
        static constexpr const char name[] = #structname;                             \
        auto& name0() const { return get<0>(); }                                      \
        auto& name1() const { return get<1>(); }                                      \
        auto& name2() const { return get<2>(); }                                      \
    };
#define DEFINE_API_4(structname, restype, type0, name0, type1, name1, type2, name2, type3, name3) \
    struct structname : public APIRequest<structname, restype, type0, type1, type2, type3> {      \
        using APIRequest<structname, restype, type0, type1, type2, type3>::APIRequest;            \
        static constexpr const char name[] = #structname;                                         \
        auto& name0() const { return get<0>(); }                                                  \
        auto& name1() const { return get<1>(); }                                                  \
        auto& name2() const { return get<2>(); }                                                  \
        auto& name3() const { return get<3>(); }                                                  \
    };
#define DEFINE_API_5(structname, restype, type0, name0, type1, name1, type2, name2, type3, name3, type4, name4) \
    struct structname : public APIRequest<structname, restype, type0, type1, type2, type3, type4> {             \
        using APIRequest<structname, restype, type0, type1, type2, type3, type4>::APIRequest;                   \
        static constexpr const char name[] = #structname;                                                       \
        auto& name0() const { return get<0>(); }                                                                \
        auto& name1() const { return get<1>(); }                                                                \
        auto& name2() const { return get<2>(); }                                                                \
        auto& name3() const { return get<3>(); }                                                                \
        auto& name4() const { return get<4>(); }                                                                \
    };
#define GET_MACRO(_1, _2, _3, _4, _5, _6, _7, _8, _9, _10, _11, _12, NAME, ...) NAME
#define DEFINE_API(...) GET_MACRO(__VA_ARGS__, DEFINE_API_5, DEFINE_API_5, DEFINE_API_4, DEFINE_API_4, \
    DEFINE_API_3, DEFINE_API_3, DEFINE_API_2, DEFINE_API_2, DEFINE_API_1, DEFINE_API_1, DEFINE_API_0)(__VA_ARGS__)
