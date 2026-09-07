#pragma once

#include <cstdint>

namespace sxpe::core::caps {

inline constexpr std::uint64_t kMaxMapBytes = 4ull << 30;
inline constexpr std::uint32_t kMaxIndexEntries = 500'000;
inline constexpr std::uint32_t kMaxResourceBytes = 256u << 20;
inline constexpr std::uint32_t kMaxCompressRatio = 1024;
inline constexpr std::uint32_t kHeaderSize = 96;
inline constexpr std::uint32_t kMaxTableEntries = 500'000;
inline constexpr std::uint32_t kMaxNameBytes = 64u << 10;
inline constexpr std::uint32_t kMaxDdsEdge = 8192;
inline constexpr std::uint32_t kMaxLivePreviewBytes = 8u << 20;
/// Max uncompressed `_XML`/`ITUN` body for xml.get / xml.set (plain-text editor).
inline constexpr std::uint32_t kMaxXmlEditorBytes = 4u << 20;

}  // namespace sxpe::core::caps
