#ifndef LIBFS_COMMON_HPP
#define LIBFS_COMMON_HPP

#include <optional>

namespace protocols {
namespace fs {

using ReadEntriesResult = std::optional<std::string>;

using PollResult = std::tuple<uint64_t, int, int>;

} } // namespace protocols::fs

#endif // LIBFS_COMMON_HPP