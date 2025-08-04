#pragma once

#include <vector>

#include "../Entry.h"
#include "JsonFile.h"

namespace data_set {
	class DataFile;
}

class data_set::DataFile : protected JsonFile {
	bool valid_;
	std::vector <Entry> entries_;

public:
	DataFile(const char *path, bool load_entries);

	[[nodiscard]] bool IsValid() const { return valid_; }
	[[nodiscard]] size_t GetEntryCnt() const { return valid_ ? doc_.Size() : 0; }
	[[nodiscard]] const std::vector <Entry> &GetEntries () const { return entries_; }
};