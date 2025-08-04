#include "MetadataFile.h"

#include <condition_variable>
#include <filesystem>
#include <iostream>
#include <queue>
#include <thread>

#include "DataFile.h"

namespace json = rapidjson;
namespace fs = std::filesystem;

auto metadata_version = "1.2.0";

bool ValidateDocMetadata(const json::Document &doc) {
	if (doc.HasParseError()) return false;
	if (!doc.IsObject()) return false;

	if (!doc.HasMember("version")) return false;
	if (!doc["version"].IsString()) return false;
	if (strcmp(doc["version"].GetString(), metadata_version) != 0) return false;

	if (!doc.HasMember("files")) return false;
	if (!doc["files"].IsArray()) return false;
	for (const auto& entry : doc["files"].GetArray()) {
		if (!entry.IsObject()) return false;
		if (!entry.HasMember("path")) return false;
		if (!entry["path"].IsString()) return false;
		if (!entry.HasMember("entries")) return false;
		if (!entry["entries"].IsArray()) return false;
		for (const auto& entry : entry["entries"].GetArray()) {
			if (!entry.IsString()) return false;
		}
	}
	return true;
}

class FileThreadPool {
	// Modified only by master thread (no mutex needed)
	fs::path root_path_;
	size_t total_files_;

	// Gated by queue_mutex_
	std::queue <fs::path> queue_;
	size_t file_index_;
	std::mutex queue_mutex_;

	// Gated by print_mutex_
	std::ostream &out_;
	std::mutex print_mutex_;

	// Gated by json_mutex_
	json::Value file_array_;
	json::Document::AllocatorType &alloc_;
	std::mutex json_mutex_;

	bool AddFileValue(const char *path, const char *rel_path) {
		const data_set::DataFile file(path, true); // TODO see if loading entries affects performance
		json::Value object(json::kNullType);
		if (!file.IsValid()) return false;
		object.SetObject();
		{
			std::unique_lock alloc_lock(json_mutex_);
			object.AddMember("path", json::Value(json::StringRef(rel_path), alloc_).Move(), alloc_);
			object.AddMember("entries", json::Value(json::kArrayType), alloc_);
			for (const auto& entry : file.GetEntries()) {
				object["entries"].PushBack(json::Value(json::StringRef(entry.id.c_str()), alloc_).Move(), alloc_);
			}
			file_array_.PushBack(object.Move(), alloc_);
		}
		return true;
	}

	void ThreadRoutine(const size_t tid) {
		while (true) {
			fs::path current_path;
			size_t saved_index;
			std::string rel_path;
			{
				std::unique_lock lock(queue_mutex_);
				if (queue_.empty()) break;
				current_path = std::move(queue_.front());
				queue_.pop();
				rel_path = fs::relative(current_path, root_path_).string();
			}
			{
				std::unique_lock lock(print_mutex_);
				saved_index = ++file_index_;
				out_ << '\t' << "Thread " << tid << ":\t[" << saved_index << '/' << total_files_ << "]: ";
				out_ << rel_path << "..." << std::endl;
			}
			const bool valid = AddFileValue(current_path.c_str(), rel_path.c_str());
			{
				std::unique_lock lock(print_mutex_);
				const size_t line_jump = file_index_ - saved_index + 1;
				out_ << "\033[" << line_jump << "A\033[1G";
				out_ << '\t' << "Thread " << tid << "\t[" << saved_index << '/' << total_files_ << "]: ";
				out_ << rel_path << "... " << (valid ? "valid" : "skipped") << std::flush;
				out_ << "\033[1G\033[" << line_jump << "B";
			}
		}
	}

public:
	FileThreadPool(std::ostream &out, json::Document::AllocatorType& alloc):
		out_(out),
		alloc_(alloc) {}

	json::Value SearchPath (const fs::path &root_path, size_t thread_cnt = std::thread::hardware_concurrency()) {
			root_path_ = root_path;
		out_ << "Searching in " << root_path_ << "..." << std::endl;
		while (!queue_.empty()) queue_.pop();
		for (const auto &path : fs::recursive_directory_iterator(root_path_)) {
			if (!fs::is_regular_file(path)) continue;
			if (path.path().extension() != ".json") continue;
			queue_.emplace(path);
		}

		file_index_ = 0;
		total_files_ = queue_.size();
		out_ << "Found " << total_files_ << " files." << std::endl;
		file_array_.SetArray();

		thread_cnt = std::min(thread_cnt, queue_.size());
		out_ << "Processing with " << thread_cnt << " threads." << std::endl;
		std::vector <std::thread> thread_pool;
		for (int i = 0; i < thread_cnt; i++) {
			thread_pool.emplace_back([this, i] {ThreadRoutine(i + 1);});
		}
		for (int i = 0; i < thread_cnt; i++) {
			thread_pool[i].join();
		}
		return std::move(file_array_);
	}
};

void data_set::MetadataFile::BuildDoc() {
	std::cout << "Building new metadata file..." << std::endl;
	modified_ = true;
	doc_.SetObject();
	json::Document::AllocatorType& alloc = doc_.GetAllocator();
	doc_.AddMember("version", json::StringRef(metadata_version), alloc);

	// Files
	{
		FileThreadPool pool(std::cout, alloc);
		json::Value files = pool.SearchPath(canonical(fs::path(path_).parent_path()));
		doc_.AddMember("files", files.Move(), alloc);
	}
	std::cout << "New json built. Saving..." << std::endl;
	Save();
}

data_set::MetadataFile::MetadataFile(const char *path, const bool rebuild) : JsonFile(rebuild ? nullptr : path) {
	if (rebuild) path_ = strdup(path);
	valid_ = rebuild ? false :ValidateDocMetadata(doc_);
	if (!valid_) BuildDoc();
	for (const auto &entry : doc_["files"].GetArray()) {
		files_.emplace_back(entry["path"].GetString(), std::vector <std::string>());
		for (const auto& id : entry["entries"].GetArray()) {
			files_.back().entries_.emplace_back(id.GetString());
		}
	}
}
