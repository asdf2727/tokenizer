#include "DataFile.h"

bool ValidateDocData(const rapidjson::Document &doc) {
	if (doc.HasParseError()) return false;
	if (!doc.IsArray()) return false;
	if (doc.Empty()) return false;
	for (const auto &entry : doc.GetArray()) {
		if (!entry.IsObject()) return false;
		if (!entry.HasMember("id")) return false;
		if (!entry["title"].IsString()) return false;
		if (!entry.HasMember("text")) return false;
		if (!entry["text"].IsString()) return false;
		if (!entry.HasMember("title")) return false;
		if (!entry["title"].IsString()) return false;
	}
	return true;
}

data_set::DataFile::DataFile(const char *path, const bool load_entries) : JsonFile(path) {
	valid_ = ValidateDocData(doc_);
	if (valid_ && load_entries) {
		for (const auto &entry : doc_.GetArray()) {
			entries_.emplace_back(entry["id"].GetString(),
			                      entry["text"].GetString(),
			                      entry["title"].GetString());
		}
	}
}
