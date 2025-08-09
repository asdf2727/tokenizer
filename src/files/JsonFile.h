#pragma once

#include "rapidjson/document.h"

class JsonFile {
protected:
	const std::string path_;
	bool valid_;

	bool modified_ = false;
	rapidjson::Document doc_;

public:
	explicit JsonFile (std::string path, bool no_read);
	~JsonFile ();

	[[nodiscard]] bool IsValid() const { return valid_; };

	void Save();
};