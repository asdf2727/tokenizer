#pragma once

#include "rapidjson/document.h"

namespace data_set {
	class JsonFile;
}

class data_set::JsonFile {
protected:
	char *path_;
	rapidjson::Document doc_;
	bool modified_ = false;

	void Save();

public:
	explicit JsonFile (const char *path);
	~JsonFile ();
};