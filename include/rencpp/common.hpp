#ifndef RENCPP_COMMON_HPP
#define RENCPP_COMMON_HPP

#include <cstdint>
#include <cstddef>
#include <limits>
#include <stdexcept>


// http://stackoverflow.com/a/4030983/211160
// Use to indicate a variable is being intentionally not referred to (which
// usually generates a compiler warning)
#ifndef UNUSED
  #define UNUSED(x) ((void)(true ? 0 : ((x), void(), 0)))
#endif

//
// Hopefully not too many of these will creep in; but right now there are some
// by matter of necessity.
//

static_assert(
    sizeof(void *) == sizeof(int32_t),
    "building rencpp binding with non 32-bit pointers..."
    "see evilPointerToInt32Cast in include/rencpp/common.hpp"
);

static_assert(
    sizeof(size_t) == sizeof(int32_t),
    "building rencpp binding with non 32-bit size_t..."
    "see remarks in include/rencpp/common.hpp"
);

inline int32_t evilPointerToInt32Cast(void const * somePointer) {
    return reinterpret_cast<int32_t>(somePointer);
}

template<class T>
inline T evilInt32ToPointerCast(int32_t someInt) {
    static_assert(
        std::is_pointer<T>::value,
        "evilInt32ToPointer cast used on non-ptr"
    );
    return reinterpret_cast<T>(someInt);
}



//
// Not putting const on functions in the C-like exported .h at the moment
// Red/System 1.0 has no const.  It's still nice on the C++ side to keep
// the bookkeeping on constness until the very last moment.
//
template<class T>
inline T * evilMutablePointerCast(T const * somePointer) {
    return const_cast<T*>(somePointer);
}



///
/// GENERAL MAGIC FOR INFERRING TYPES OF LAMBDAS
///

//
// This craziness is due to a seeming-missing feature in C++11, which is a
// convenient way to use type inference from a lambda.  Lambdas are the most
// notationally convenient way of making functions, and yet you can't make
// a template which picks up their signature.  This creates an annoying
// repetition where you wind up typing the signature twice.
//
// We are trusting here that it's not a "you shouldn't do it" but rather a
// "the standard library didn't cover it".  This helpful adaptation from
// Morwenn on StackOverflow does the trick.
//
//     http://stackoverflow.com/a/19934080/211160
//
// But putting it in use is as ugly as any standard library implementation
// code, though.  :-P  If you're dealing with "Generic Lambdas" from C++14
// it breaks, but should work for the straightforward case it was designed for.
//

namespace ren {

namespace utility {

template<std::size_t...>
struct indices {};

template<std::size_t N, std::size_t... Ind>
struct make_indices:
    make_indices<N-1, N-1, Ind...>
{};

template<std::size_t... Ind>
struct make_indices<0, Ind...>:
    indices<Ind...>
{};

template<typename T>
struct function_traits:
    function_traits<decltype(&T::operator())>
{};

template<typename C, typename Ret, typename... Args>
struct function_traits<Ret(C::*)(Args...) const>
{
    enum { arity = sizeof...(Args) };

    using result_type = Ret;

    template<std::size_t N>
    using arg = typename std::tuple_element<N, std::tuple<Args...>>::type;
};



///
/// FREAKY GENERAL MAGIC FOR TUPLE UNPACKING
///

//
// It would be nice to be able to store a set of parameters to something and
// then call it later with those parameters, right?  Yes, but... trickier
// than it looks.  Johannes Schaub (@litb) to the rescue:
//
//     http://stackoverflow.com/a/7858971/211160
//

template<int ...>
struct seq { };

template<int N, int ...S>
struct gens : gens<N-1, N-1, S...> { };

template<int ...S>
struct gens<0, S...> {
  typedef seq<S...> type;
};



///
/// MORE FREAKY MAGIC
///

template<std::size_t N, typename T, typename F, std::size_t... Indices>
auto apply_from_array_impl(F&& func, T (&arr)[N], indices<Indices...>)
    -> decltype(std::forward<F>(func)(arr[Indices]...))
{
    return std::forward<F>(func)(arr[Indices]...);
}

template<std::size_t N, typename T, typename F,
         typename Indices = make_indices<N>>
auto apply_from_array(F&& func, T (&arr)[N])
    -> decltype(apply_from_array_impl(std::forward<F>(func), arr, Indices()))
{
    return apply_from_array_impl(std::forward<F>(func), arr, Indices());
}

} // end namespace utility

} // end namespace ren

#endif // end include guard
