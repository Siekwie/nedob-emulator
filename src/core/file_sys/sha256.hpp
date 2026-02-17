#pragma once

#include "../../common/common_types.hpp"
#include <cstddef>

namespace Sha256 {

/// Hash data with SHA-256. out must point to at least 32 bytes.
void hash(const u8* data, std::size_t len, u8* out);

}  // namespace Sha256
