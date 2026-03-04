// libquant -- library source
//
// This file exists solely to produce a compilation unit for
// the shared/static library.  QuantMath is header-only but
// this ensures the .so/.a contain at least one object file
// and allows future non-inline additions.

#include "quantmath.h"

// Force instantiation of key template-like paths so the linker
// has something to export.  All real work is in the header.
namespace libquant_internal
{
    // Version info
    extern "C" const char* libquant_version() { return "1.0.0"; }
}
