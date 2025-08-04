#pragma once

#include <vector>

#include "JsonFile.h"

namespace data_set {
	class MetadataFile;
}

class data_set::MetadataFile : public JsonFile {
	bool valid_;

public:
	struct FileInfo {
		const char *path_;
		std::vector <std::string> entries_;
	};

private:
	std::vector <FileInfo> files_;

	void BuildDoc();

public:
	explicit MetadataFile(const char *path, bool rebuild = false);

	[[nodiscard]] const std::vector <FileInfo> &GetFiles() const { return files_; }
};