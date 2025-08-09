#pragma once

#include <unordered_map>

#include "MetadataFile.h"

class CandidatesFile {
	const std::string file_path_;
	std::unordered_map <std::string, size_t> candidates_;

	void BuildCandidates(const MetadataFile &metadata, size_t max_len, size_t file_cnt);

	bool ReadFile();
	void WriteFile();

public:

	CandidatesFile(const MetadataFile &metadata, size_t max_len, size_t file_cnt = -1, bool rebuild = false);

	[[nodiscard]] const std::unordered_map <std::string, size_t> &GetCandidates() const { return candidates_; }
};
