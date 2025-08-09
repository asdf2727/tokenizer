#include "MetadataFile.h"

#include <iostream>

//#define NO_DISTRIBUTE_DEBUG
#include "../utils/Multithread.h"
#include "../config.h"

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

struct MetadataEnv {
	std::mutex mutex = std::mutex();
	json::Value file_array = json::Value(json::kArrayType);
	json::Document::AllocatorType &alloc;
	fs::path root_path;

	explicit MetadataEnv(json::Document::AllocatorType &alloc, const fs::path &root_path) : alloc(alloc), root_path(root_path) {}
};

struct MetadataTask {
	std::string path;
};

bool FileScanMetadata(MetadataEnv &env, MetadataTask &task, size_t tid) {
	const DataFile file(task.path);
	if (!file.IsValid()) return false;
	json::Value object(json::kObjectType);
	object.AddMember("path", json::Value(fs::relative(task.path, env.root_path).c_str(), env.alloc), env.alloc);
	{
		std::unique_lock lock(env.mutex);
		env.file_array.PushBack(object, env.alloc);
	}
	return true;
}

void MetadataFile::BuildDoc() {
	std::cout << "Building new metadata file..." << std::endl;
	modified_ = true;
	doc_.SetObject();
	json::Document::AllocatorType& alloc = doc_.GetAllocator();
	doc_.AddMember("version", json::StringRef(kBuildVersion.c_str()), alloc);

	{
		const fs::path root_path = canonical(fs::path(path_).parent_path());
		std::vector <MetadataTask> tasks;
		std::cout << "Searching in " << root_path << "..." << std::endl;
		for (const auto &file : fs::recursive_directory_iterator(root_path)) {
			const fs::path &path = file.path();
			if (!fs::is_regular_file(path)) continue;
			if (path.extension() != ".json") continue;
			tasks.emplace_back(path.string());
		}
		std::cout << "Found " << tasks.size() << " files." << std::endl;
		MetadataEnv env(doc_.GetAllocator(), root_path);
		DistributeTasks <MetadataEnv, MetadataTask> (std::cout, env, tasks, FileScanMetadata);
		doc_.AddMember("files", env.file_array, alloc);
	}

	std::cout << "New metadata built. Saving..." << std::endl;
	Save();
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
