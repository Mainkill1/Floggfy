#pragma once
#include <cstdint>
#include <string>
#include <utility>
#include <vector>
namespace history {
struct Tags {
    std::vector<std::pair<std::string,std::string>> fields;
    std::vector<uint8_t> cover;
    std::string mime;
};
bool BuildPicture(const Tags& tags,std::vector<uint8_t>& picture);
bool TagOgg(const std::vector<uint8_t>& source,const Tags& tags,
            std::vector<uint8_t>& output,std::string& error);
}
