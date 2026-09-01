#include "sxpe/resources/s3sa.hpp"

namespace sxpe::resources {

S3saInfo inspect_s3sa(std::span<const std::byte> bytes, std::string_view nmap_name) {
    S3saInfo inf;
    inf.size = bytes.size();
    inf.module_hint = std::string(nmap_name);
    for (std::size_t i = 0; i + 1 < bytes.size(); ++i) {
        if (bytes[i] == std::byte{'M'} && bytes[i + 1] == std::byte{'Z'}) {
            inf.pe_offset = i;
            break;
        }
    }
    if (inf.module_hint.empty()) {
        inf.module_hint = "assembly.dll";
    } else if (inf.module_hint.find('.') == std::string::npos) {
        inf.module_hint += ".dll";
    }
    return inf;
}

}  // namespace sxpe::resources
