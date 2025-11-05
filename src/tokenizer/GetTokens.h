#pragma once

#include "Token.h"
#include "../files/MetadataFile.h"

namespace annealing {
	std::vector <Token> GetTokens (const MetadataFile &metadata, uint8_t max_len = UINT8_MAX, size_t file_cnt = -1, bool rebuild = false);
}