#pragma once

// Forward a forwarding reference without naming its template parameter.
// Equivalent to std::forward<decltype(x)>(x), which is all std::forward<X>(x)
// does when X is the type that x was declared with. It is the only spelling
// available where the parameter has no name to forward from: deducing this
// (this auto&& self) and generic lambdas (auto&& lambda). Packs expand as
// FWD(pack)... .
//
// Not a substitute for std::forward_like, which propagates the value category
// of one object onto a subobject.
#define FWD(x) static_cast<decltype(x) &&>(x)
