#pragma once

#if defined NDEBUG

#include <cstddef>
#include <cstdint>
#include <cassert>

#include <mutex>
#include <tuple>
#include <array>
#include <atomic>
#include <thread>
#include <memory>
#include <string>
#include <limits>
#include <utility>
#include <optional>
#include <stdexcept>
#include <functional>
#include <string_view>
#include <type_traits>
#include <system_error>

#include <boost/predef.h>

#if defined BOOST_COMP_MSVC_AVAILABLE
#pragma warning(push, 3)
#endif

#include <boost/exception/all.hpp>   
#include <boost/outcome.hpp>
       
#if defined BOOST_COMP_MSVC_AVAILABLE
#pragma warning(pop)
#endif

#include <cuckoohash_map.hh>
#include <moodycamel/concurrentqueue.h>

#endif
