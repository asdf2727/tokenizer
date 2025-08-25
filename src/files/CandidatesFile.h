#pragma once

#include <unordered_map>

#include "MetadataFile.h"

std::unordered_map <std::string, size_t> GetCandidates (const MetadataFile &metadata, size_t max_len = 10, size_t file_cnt = -1, bool rebuild = false);