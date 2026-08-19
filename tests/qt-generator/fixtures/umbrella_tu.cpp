// The generated umbrella, in miniature.
//
// A consumer wrapper `.cpp` is never a translation unit of its own. The
// umbrella logos-cpp-generator emits (`logos_sdk.cpp`) is one `#include` per
// dependency wrapper, so a module with two dependencies compiles BOTH of them
// into a single TU — and anything the backend puts at file scope has to survive
// being present twice.
//
// Every golden beside this generates exactly one contract, so nothing here ever
// compiled two until this file. The first real two-dependency consumer to move
// onto the veneer backend did, and failed on a redefinition of the file-scope
// rejection helper: an anonymous namespace is no protection, because all
// anonymous namespaces within one TU are the SAME namespace.
//
// Nothing is executed. The assertion is that this compiles at all.
#include "plain_module_api.cpp"
#include "optional_module_api.cpp"
