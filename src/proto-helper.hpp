#pragma once

#include <vefs/blob.hpp>

namespace vefs
{
    template <typename T>
    bool parse_blob(T &out, blob_view raw)
    {
        return out.ParseFromArray(raw.data(), static_cast<int>(raw.size()));
    }

    template <typename T>
    bool serialize_to_blob(blob out, T &data)
    {
        return data.SerializeToArray(out.data(), static_cast<int>(out.size()));
    }
}
