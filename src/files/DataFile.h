#pragma once

#include <vector>

#include "JsonFile.h"


class DataFile : public JsonFile {
	bool Validate();

public:
	struct Entry {
		const std::string id;
		const std::string title;
		const std::string text;
	};

	explicit DataFile(const std::string &path);

	[[nodiscard]] std::vector <Entry> GetEntries () const;
};