#pragma once

#include <filesystem>
#include <string>
#include <vector>

#include "JsonFile.h"

class MetadataFile : public JsonFile {
	bool Validate();
	void BuildDoc();

public:
	struct Entry {
		const char *path;
	};

	explicit MetadataFile(const std::string &path, bool rebuild = false);

	[[nodiscard]] std::filesystem::path GetRootPath() const;
	[[nodiscard]] std::vector<Entry> GetFiles(size_t file_cnt = -1) const;
};