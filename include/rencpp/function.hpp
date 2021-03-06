#ifndef RENCPP_EXTENSION_HPP
#define RENCPP_EXTENSION_HPP

//
// function.hpp
// This file is part of RenCpp
// Copyright (C) 2015 HostileFork.com
//
// Licensed under the Boost License, Version 1.0 (the "License")
//
//      http://www.boost.org/LICENSE_1_0.txt
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or
// implied.  See the License for the specific language governing
// permissions and limitations under the License.
//
// See http://rencpp.hostilefork.com for more information on this project
//

#include <cassert>
#include <functional>
#include <stdexcept>
#include <tuple>
#include <type_traits>
#include <utility>
#include <vector>

#include <mutex> // global table must be protected for thread safety

#include "values.hpp"
#include "engine.hpp"

namespace ren {

///
/// FUNCTION TYPE(S?)
///

//
// In the current implementation, a FunctionGenerator is really just a NATIVE!
// where the native function pointer (that processes the argument stack) is
// built automatically by the system.  It's not possible to have a term be
// both a template and a class, so if FunctionGenerator was going to be
// Function then Function would have to be written Function<> and be
// specialized for that.
//
// REVIEW: Should we call this Native instead, or should Function represent
// ANY-FUNCTION! types and not bother with inventing a separate AnyFunction?
//

class Function : public Value {
protected:
    friend class Value;
    Function (Dont const &) : Value (Dont::Initialize) {}
    inline bool isValid() const { return isFunction(); }

private:
    // Most classes can get away with setting up cell bits all in the
    // implementation files, but FunctionGenerator is a template.  It
    // needs to be able to finalize "in view".  We might consider another
    // way of shaping this, by having a public "set cell bits for function"
    // API in the hooks.h, then just use normal finishInit.  Might be what
    // has to be done.

    template <class R, class... Ts>
    friend class internal::FunctionGenerator;

    void finishInitSpecial(
        RenEngineHandle engine,
        Block const & spec,
        RenShimPointer const & shim
    );
};


///
/// EXTENSION FUNCTION TEMPLATE
///

//
// While calling Ren and the runtime from C++ is interesting (such as to
// accomplish tasks like "running PARSE from C++"), a potentially even
// more relevant task is to make it simple to call C++ code from inside
// the Rebol or Red system.  Goals for such an interface would be type-safety,
// brevity, efficiency, etc.
//
// This is a "modern" C++11 take on that process.  It uses template
// metaprogramming to analyze the type signature of a lambda function (or,
// if you prefer, anything else with an operator()) to be called for the
// implementation, and unpacks the parameters to call it with.  See the
// tests for the notation, but it is rather pretty.
//
// There may be ways of making the spec block automatically, but naming
// the parameters would be difficult.  A version that took a single
// argument and just called them arg1 arg2 etc and built the type
// strings may be possible in the future, but that's really not a good
// way to document your work even if it's technically achievable.
//

//
// Note that inheriting from std::function would probably be a bad idea:
//
//     http://stackoverflow.com/q/27263092/211160
//


namespace internal {

//
// Limits of the type system and specialization force us to have a table
// of functions on a per-template specialization basis.  However, there's
// no good reason to have one mutex per table.  One for all will do.
//

extern std::mutex extensionTablesMutex;

using RenShimId = int;

extern RenShimId shimIdToCapture;

using RenShimBouncer = RenResult (*)(RenShimId id, RenCell * stack);

extern RenShimBouncer shimBouncerToCapture;



template<class R, class... Ts>
class FunctionGenerator : public Function {
private:

    //
    // Rebol natives take in a pointer to the stack of REBVALs.  This stack
    // has protocol for the offsets of arguments, offsets for other
    // information (like where the value for the function being called is
    // written), and an offset where to write the return value:
    //
    //     int  (* REBFUN)(REBVAL * ds);
    //
    // That's too low level for a C++ library.  We wish to allow richer
    // function signatures that can be authored with lambdas (or objects with
    // operator() overloaded, any "Callable").  That means this C-like
    // interface hook ("shim") needs to be generated automatically from
    // examining the type signature, so that it can call the C++ hook.
    //

    using FunType = std::function<R(Ts...)>;

    using ParamsType = std::tuple<Ts...>;


    // When a "self-aware" shim forwards its parameter and its function
    // identity to the templatized generator that created it, then it
    // looks in this per-signature table to find the std::function to
    // unpack the parameters and give to.  It also has the engine handle,
    // which is required to construct the values for the cells in the
    // appropriate sandbox.  The vector is only added to and never removed,
    // but it has to be protected with a mutex in case multiple threads
    // are working with it at the same time.

    struct TableEntry {
        RenEngineHandle engine;
        FunType const fun;
    };

    static std::vector<TableEntry> table;


    // Function used to create Ts... on the fly and apply a
    // given function to them

    template <std::size_t... Indices>
    static auto applyFunImpl(
        FunType const & fun,
        RenEngineHandle engine,
        RenCell * stack,
        utility::indices<Indices...>
    )
        -> decltype(
            fun(
                Value::construct_<
                    typename std::decay<
                        typename utility::type_at<Indices, Ts...>::type
                    >::type
                >(
                    *REN_STACK_ARGUMENT(stack, Indices),
                    engine
                )...
            )
        )
    {
        return fun(
            Value::construct_<
                typename std::decay<
                    typename utility::type_at<Indices, Ts...>::type
                >::type
            >(
                *REN_STACK_ARGUMENT(stack, Indices),
                engine
            )...
        );
    }

    template <typename Indices = utility::make_indices<sizeof...(Ts)>>
    static auto applyFun(
        FunType const & fun, RenEngineHandle engine, RenCell * stack
    ) ->
        decltype(applyFunImpl(fun, engine, stack, Indices {}))
    {
        return applyFunImpl(fun, engine, stack, Indices {});
    }

private:
    static int bounceShim(internal::RenShimId id, RenCell * stack) {
        using internal::extensionTablesMutex;

        // The extension table is add-only, but additions can resize,
        // and move the underlying memory.  We either hold the lock for
        // the entire duration of the function run, or copy the table
        // entry out by value... the latter makes more sense.

        extensionTablesMutex.lock();
        TableEntry entry = table[id];
        extensionTablesMutex.unlock();

        // Our applyFun helper does the magic to recursively forward
        // the Value classes that we generate to the function that
        // interfaces us with the Callable the extension author wrote
        // (who is blissfully unaware of the stack convention and
        // writing using high-level types...)

        auto && result = applyFun(entry.fun, entry.engine, stack);

        // The return result is written into a location that is known
        // according to the protocol of the stack

        *REN_STACK_RETURN(stack) = result.cell;

        // Note: trickery!  R_RET is 0, but all other R_ values are
        // meaningless to Red.  So we only use that one here.

        return REN_SUCCESS;
    }

public:
    FunctionGenerator (
        RenEngineHandle engine,
        Block const & spec,
        RenShimPointer shim,
        FunType const & fun = FunType {nullptr}
    ) :
        Function (Dont::Initialize)
    {
        // First we lock the global table so we can call the shim for the
        // initial time.  It will grab its identity so that from then on it
        // can forward calls to us.

        std::lock_guard<std::mutex> lock {internal::extensionTablesMutex};

        assert(::ren::internal::shimIdToCapture == -1);
        assert(not ::ren::internal::shimBouncerToCapture);

        ::ren::internal::shimIdToCapture = table.size();
        ::ren::internal::shimBouncerToCapture = &bounceShim;

        if (shim(nullptr) != REN_SHIM_INITIALIZED)
            throw std::runtime_error(
                "First shim call didn't return REN_SHIM_INITIALIZED"
            );

        ::ren::internal::shimIdToCapture = -1;
        ::ren::internal::shimBouncerToCapture = nullptr;

        // Insert the shim into the mapping table so it can find itself while
        // the shim code is running.  Note that the tableAdd code has
        // to be thread-safe in case two threads try to modify the global
        // table at the same time.

        table.push_back({engine, fun});

        // We've got what we need, but depending on the runtime it will have
        // a different encoding of the shim and type into the bits of the
        // cell.  We defer to a function provided by each runtime.

        Function::finishInitSpecial(engine, spec, shim);
    }
};


//
// There is some kind of voodoo that makes this work, even though it's in a
// header file.  So each specialization of the FunctionGenerator type gets its
// own copy and there are no duplicate symbols arising from multiple includes
//

template<class R, class... Ts>
std::vector<
    typename FunctionGenerator<R, Ts...>::TableEntry
> FunctionGenerator<R, Ts...>::table;



///
/// PREPROCESSOR UNIQUE LAMBDA FUNCTION POINTER TRICK
///

//
// While preprocessor macros are to be avoided whenever possible, the
// particulars of this case require them.  At the moment, the only "identity"
// the generated parameter-unpacking "shim" function gets when it is called
// is its own pointer pushed on the stack.  That's the only way it can look
// up the C++ function it's supposed to unpack and forward to.  And C/C++
// functions *don't even know their own pointer*!
//
// So what we do here is a trick.  The first call to the function isn't asking
// it to forward parameters, it's just asking it to save knowledge of its own
// identity and the pointer to the function it should forward to in future
// calls.  Because the macro is instantiated by each client, there is a
// unique pointer for each lambda.  It's really probably the only way this
// can be done without changing the runtime to pass something more to us.

#define REN_STD_FUNCTION \
    [](RenCell * stack) -> int {\
        static ren::internal::RenShimId id = -1; \
        static ren::internal::RenShimBouncer bouncer = nullptr; \
        if (id != -1) \
            return bouncer(id, stack); \
        id = ren::internal::shimIdToCapture; \
        bouncer = ren::internal::shimBouncerToCapture; \
        return REN_SHIM_INITIALIZED; \
    }

} // end namespace internal



///
/// USER-FACING CONSTRUCTION FOR MAKE FUNCTION
///

//
// The FunctionGenerator is an internal class.  One reason why the interface
// is exposed as a function instead of as a class is because of a problem:
// C++11 is missing the feature of a convenient way to use type inference from
// a lambda to determine its signature.  So you can't make a templated class
// that automatically detects and extracts their type when passed as an
// argument to the constructor.  This creates an annoying repetition where
// you wind up typing the signature twice...once on what you're trying to
// pass the lambda to, and once on the lambda itself.
//
//     Foo<Baz(Bar)> temp { [](Bar bar) -> Baz {...} }; // booo!
//
//     auto temp = makeFoo { [](Bar bar) -> Baz {...} }; // better!
//
// Eliminating the repetition is desirable.  So Morwenn adapted this technique
// from one of his StackOverflow answers:
//
//     http://stackoverflow.com/a/19934080/211160
//
// It will not work in the general case with "Callables", e.g. objects that
// have operator() overloading.  (There could be multiple overloads, which one
// to pick?  For a similar reason it will not work with "Generic Lambdas"
// from C++14.)  However, it works for ordinary lambdas...and that leads to
// enough benefit to make it worth including.
//

template<typename Fun, std::size_t... Ind>
Function makeFunction_(
    std::true_type, // Func return type is void
    RenEngineHandle engine,
    Block const & spec,
    RenShimPointer shim,
    Fun && fun,
    utility::indices<Ind...>
) {
    using Ret = Unset;

    using Gen = internal::FunctionGenerator<
        Ret,
        typename utility::function_traits<
            typename std::remove_reference<Fun>::type
        >::template arg<Ind>...
    >;

    return Gen {
        engine,
        spec,
        shim,
        std::function<
            Ret(
                typename utility::function_traits<
                    typename std::remove_reference<Fun>::type
                >::template arg<Ind>...
            )
        >([&fun](typename utility::function_traits<
                    typename std::remove_reference<Fun>::type
                >::template arg<Ind>... args){
            fun(args...);
            return Unset();
        })
    };
}

template<typename Fun, std::size_t... Ind>
Function makeFunction_(
    std::false_type,    // Func return type is not void
    RenEngineHandle engine,
    Block const & spec,
    RenShimPointer shim,
    Fun && fun,
    utility::indices<Ind...>
) {
    using Ret = typename utility::function_traits<
        typename std::remove_reference<Fun>::type
    >::result_type;

    using Gen = internal::FunctionGenerator<
        Ret,
        typename utility::function_traits<
            typename std::remove_reference<Fun>::type
        >::template arg<Ind>...
    >;

    return Gen {
        engine,
        spec,
        shim,
        std::function<
            Ret(
                typename utility::function_traits<
                    typename std::remove_reference<Fun>::type
                >::template arg<Ind>...
            )
        >(fun)
    };
}

template<typename Fun>
Function makeFunction_(
    RenEngineHandle engine,
    Block const & spec,
    RenShimPointer shim,
    Fun && fun
) {
    using Ret = typename utility::function_traits<
        typename std::remove_reference<Fun>::type
    >::result_type;

    using Indices = utility::make_indices<
        utility::function_traits<
            typename std::remove_reference<Fun>::type
        >::arity
    >;

    return makeFunction_(
        typename std::is_void<Ret>::type{},  // tag dispatching
        engine,
        spec,
        shim,
        std::forward<Fun>(fun),
        Indices{}
    );
}


//
// For convenience, we define specializations that let you be explicit about
// the engine and/or provide an already built spec block.
//

template<typename Fun>
Function makeFunction(
    char const * spec,
    RenShimPointer shim,
    Fun && fun
) {
    return makeFunction_(
        Engine::runFinder().getHandle(),
        Block {spec},
        shim,
        std::forward<Fun>(fun)
    );
}


template<typename Fun>
Function makeFunction(
    Block const & spec,
    RenShimPointer shim,
    Fun && fun
) {
    return makeFunction_(
        Engine::runFinder().getHandle(),
        spec,
        shim,
        std::forward<Fun>(fun)
    );
}


template<typename Fun>
Function makeFunction(
    Engine & engine,
    char const * spec,
    RenShimPointer shim,
    Fun && fun
) {
    return makeFunction_(
        engine,
        Block {spec},
        shim,
        std::forward<Fun>(fun)
    );
}


template<
    typename Fun,
    typename Indices = utility::make_indices<
        utility::function_traits<
            typename std::remove_reference<Fun>::type
        >::arity
    >
>
Function makeFunction(
    Engine & engine,
    Block const & spec,
    RenShimPointer shim,
    Fun && fun
) {
    return makeFunction_(
        engine.getHandle(),
        spec,
        shim,
        std::forward<Fun>(fun)
    );
}


}

#endif
