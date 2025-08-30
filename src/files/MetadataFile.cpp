#include "MetadataFile.h"

#include <iostream>

// #define NO_DISTRIBUTE_DEBUG
#include <mutex>
#include <utility>


#include "../config.h"
#include "../utils/Multithread.h"

#include "DataFile.h"

namespace json = rapidjson;
namespace fs = std::filesystem;

bool MetadataFile::Validate() {
	if (!doc_.IsObject()) return false;
	if (!doc_.HasMember("version")) return false;
	if (!doc_["version"].IsString()) return false;
	if (kBuildVersion != doc_["version"].GetString()) return false;

	if (!doc_.HasMember("files")) return false;
	if (!doc_["files"].IsArray()) return false;
	for (const auto& entry : doc_["files"].GetArray()) {
		if (!entry.IsObject()) return false;
		if (!entry.HasMember("path")) return false;
		if (!entry["path"].IsString()) return false;
	}
	return true;
}

void MetadataFile::BuildDoc() {
	std::cout << "Building new metadata file..." << std::endl;
	modified_ = true;
	doc_.SetObject();
	json::Document::AllocatorType& alloc = doc_.GetAllocator();
	doc_.AddMember("version", json::StringRef(kBuildVersion.c_str()), alloc);

	const fs::path root_path = canonical(fs::path(path_).parent_path());

	json::Value file_array(json::kArrayType);
	{
		std::mutex mutex;
		ThreadPool pool;
		for (const auto &file : fs::recursive_directory_iterator(root_path)) {
			fs::path path = file.path();
			if (!fs::is_regular_file(path)) continue;
			if (path.extension() != ".json") continue;

			pool.Enqueue([&root_path, &alloc, path = std::move(path), &mutex, &file_array] {
				if (!DataFile(path).IsValid()) return false;
				json::Value object(json::kObjectType);
				object.AddMember("path", json::Value(fs::relative(path, root_path).c_str(), alloc), alloc);
				{
					std::lock_guard lock(mutex);
					file_array.PushBack(object, alloc);
				}
				return true;
			});
		}
		pool.Wait();
	}
	std::cout << "Found " << file_array.Size() << " valid files. Saving..." << std::endl;
	doc_.AddMember("files", file_array, alloc);

	Save();
	std::cout << "New metadata built." << std::endl;
}

MetadataFile::MetadataFile(const std::string &path, const bool rebuild):
	JsonFile(path, rebuild) {
	if (valid_) valid_ = Validate();
	if (!valid_) BuildDoc();
}

fs::path MetadataFile::GetRootPath() const {
	return canonical(fs::path(path_).parent_path());
}
std::vector <MetadataFile::Entry> MetadataFile::GetFiles() const {
	std::vector <Entry> files;
	for (const auto &entry : doc_["files"].GetArray()) {
		files.emplace_back(entry["path"].GetString());
	}
	return files;
}
