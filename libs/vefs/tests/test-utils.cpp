#include "test-utils.hpp"
namespace adesso::vefs
{
std::ostream &operator<<(std::ostream &os,
                         const adesso::vefs::FileDescriptor &ref)
{
    os << "Forward declared ref: \n";
    return os;
}
} // namespace adesso::vefs
