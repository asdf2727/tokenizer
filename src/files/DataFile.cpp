#include "DataFile.h"

bool DataFile::Validate() {
	if (doc_.HasParseError()) return false;
	if (!doc_.IsArray()) return false;
	if (doc_.Empty()) return false;
	for (const auto &entry : doc_.GetArray()) {
		if (!entry.IsObject()) return false;
		if (!entry.HasMember("id")) return false;
		if (!entry["id"].IsString()) return false;
		if (!entry.HasMember("text")) return false;
		if (!entry["text"].IsString()) return false;
		if (!entry.HasMember("title")) return false;
		if (!entry["title"].IsString()) return false;
	}
	return true;
}

// TODO see if loading entries affects performance
DataFile::DataFile(const std::string &path) : JsonFile(path, false) {
	if (valid_) valid_ = Validate();
}

std::vector <DataFile::Entry> DataFile::GetEntries() const {
	std::vector <Entry> entries;
	for (const auto &entry : doc_.GetArray()) {
		entries.emplace_back(entry["id"].GetString(),
		                     entry["title"].GetString(),
		                     entry["text"].GetString());
	}
	return entries;
}
