#pragma once

#include <unordered_map>

#include "JsonFile.h"
#include "MetadataFile.h"

class CandidatesFile : public JsonFile {
	const MetadataFile &metadata_;

	bool Validate();
	void BuildDoc(size_t max_len, size_t file_cnt);

public:
	struct Entry {
		const std::string name;
		size_t freq;
	};

	explicit CandidatesFile(const MetadataFile &metadata, size_t max_len, size_t file_cnt = -1, bool rebuild = false);

	[[nodiscard]] std::unordered_map <std::string, size_t> GetCandidates() const;
};
