//
// engine.cpp
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

#include "rencpp/engine.hpp"
#include "rencpp/context.hpp"

namespace ren {

Engine::Finder Engine::finder;

Value Runtime::evaluate(
    internal::Loadable const loadables[],
    size_t numLoadables,
    Context * context
) {
    Value result {Value::Dont::Initialize};

    if (context == nullptr)
        context = &Context::runFinder(nullptr);

    Value::constructOrApplyInitialize(
        context->getEngine().getHandle(),
        context->getHandle(),
        nullptr,
        loadables,
        numLoadables,
        nullptr, // don't construct
        &result // do apply
    );

    return result;
}


Value Runtime::evaluate(
    internal::Loadable const loadables[],
    size_t numLoadables,
    Engine * engine
) {
    Value result {Value::Dont::Initialize};

    if (engine == nullptr)
        engine = &Engine::runFinder();

    Value::constructOrApplyInitialize(
        engine->getHandle(),
        REN_CONTEXT_HANDLE_INVALID,
        nullptr,
        loadables,
        numLoadables,
        nullptr, // don't construct
        &result // do apply
    );

    return result;
}

}
