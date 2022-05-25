#pragma once

#include <boost/predef/compiler.h>

#if defined(BOOST_COMP_MSVC_AVAILABLE)
#pragma warning(push, 3)
#pragma warning(disable : 4905) // C4905: wide string literal cast to 'LPSTR'
#pragma warning(disable : 4906) // C4906: string literal cast to 'LPWSTR'
#pragma warning(disable : 4263) // C4263: member function does not override any
                                //        base class virtual member function
#endif

#include <llfio/llfio.hpp>

#if defined(BOOST_COMP_MSVC_AVAILABLE)
#pragma warning(pop)
#endif

namespace vefs
{

namespace llfio = LLFIO_V2_NAMESPACE;

}
