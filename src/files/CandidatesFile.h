#pragma once

#include <unordered_map>

#include "MetadataFile.h"

std::vector <std::pair <std::string, size_t>> GetCandidates (const MetadataFile &metadata, size_t max_len = -1, size_t file_cnt = -1, bool rebuild = false);