#include "CandidatesFile.h"

#include <algorithm>
#include <list>
#include <map>

#include <filesystem>
#include <iostream>

#include "../utils/Multithread.h"
#include "../config.h"

#include "DataFile.h"
#include "MetadataFile.h"

namespace json = rapidjson;
namespace fs = std::filesystem;

bool CandidatesFile::Validate() {
	if (doc_.HasParseError()) return false;
	if (!doc_.IsObject()) return false;

	if (!doc_.HasMember("version")) return false;
	if (!doc_["version"].IsString()) return false;
	if (strcmp(doc_["version"].GetString(), kBuildVersion) != 0) return false;

	if (!doc_.HasMember("candidates")) return false;
	if (!doc_["candidates"].IsArray()) return false;
	for (const auto &entry : doc_["candidates"].GetArray()) {
		if (!entry.IsObject()) return false;
		if (!entry.HasMember("n")) return false;
		if (!entry["n"].IsString()) return false;
		if (!entry.HasMember("f")) return false;
		if (!entry["f"].IsUint64()) return false;
	}
	return true;
}

struct CandidatesEnv {
	size_t max_token_length;
	std::mutex map_mutex;
	std::vector <CandidatesFile::Entry> cand;

	CandidatesEnv(const size_t max_length) : max_token_length(max_length) {}
};

struct CandidatesTask {
	std::string file_path;
};

bool FileScanCandidates(CandidatesEnv &env, CandidatesTask &task) {
	const DataFile data(task.file_path);
	if (!data.IsValid()) return false;

	std::vector <CandidatesFile::Entry> local_cand;
	{
		std::map <std::string, size_t> local_freq;
		for (const DataFile::Entry &entry : data.GetEntries()) {
			std::string text = entry.text;
			std::ranges::transform(text, text.begin(),
								   [](const unsigned char c) { return std::tolower(c); });
			for (size_t i = 0; i < text.size(); i++) {
				for (size_t len = 1; len <= std::min(env.max_token_length, text.size() - i); len++) {
					local_freq[text.substr(i, len)]++;
					if (text[i + len] == ' ') break;
				}
			}
		}
		for (const auto &[key, value] : local_freq) {
			local_cand.emplace_back(key, value);
		}
	}
	{
		std::unique_lock lock(env.map_mutex);
		auto it = env.cand.begin();
		auto local_it = local_cand.begin();
		std::vector <CandidatesFile::Entry> temp;
		temp.reserve(local_cand.size() + env.cand.size());
		while (it != env.cand.end() && local_it != local_cand.end()) {
			while (it != env.cand.end() && it->name < local_it->name) {
				temp.push_back(std::move(*it++));
			}
			if (it == env.cand.end()) break;
			if (it->name == local_it->name) {
				temp.push_back(std::move(*it++));
				temp.back().freq += local_it->freq;
			}
			else {
				temp.push_back(std::move(*local_it));
			}
			++local_it;
		}
		std::swap(env.cand, temp);
		while (local_it != local_cand.end()) {
			env.cand.push_back(std::move(*local_it++));
		}
	}
	return true;
}

void CandidatesFile::BuildDoc(const size_t max_len, const size_t file_cnt) {
	std::cout << "Building new candidates file..." << std::endl;
	modified_ = true;
	doc_.SetObject();
	json::Document::AllocatorType &alloc = doc_.GetAllocator();
	doc_.AddMember("version", json::StringRef(kBuildVersion), alloc);

	{
		const fs::path root_path = metadata_.GetRootPath();
		std::vector <MetadataFile::Entry> files = metadata_.GetFiles();
		files.resize(std::min(file_cnt, files.size()));

		std::cout << "Processing " << files.size() << " files." << std::endl;
		std::vector <CandidatesTask> tasks;
		for (int i = 0; i < files.size(); i++) {
			tasks.emplace_back(root_path / files[i].path);
		}
		CandidatesEnv env(max_len);
		DistributeTasks <CandidatesEnv, CandidatesTask>(std::cout, env, tasks, FileScanCandidates);
		json::Value cands(json::kArrayType);
		for (const auto &[name, freq] : env.cand) {
			json::Value cand(json::kObjectType);
			cand.AddMember("n", json::Value(name.c_str(), alloc), alloc);
			cand.AddMember("f", freq, alloc);
			cands.PushBack(cand, alloc);
		}
		doc_.AddMember("candidates", cands, alloc);
	}

	std::cout << "New candidates built. Saving..." << std::endl;
	Save();
}

CandidatesFile::CandidatesFile(const MetadataFile &metadata, const size_t max_len, const size_t file_cnt, const bool rebuild):
	JsonFile(metadata.GetRootPath() / (".candidates-" + (file_cnt == -1 ? "all" : std::to_string(file_cnt)) + ".json"), rebuild),
	metadata_(metadata) {
	if (valid_) valid_ = Validate();
	if (!valid_) BuildDoc(max_len, file_cnt);
}

std::unordered_map <std::string, size_t> CandidatesFile::GetCandidates() const {
	std::unordered_map <std::string, size_t> candidates;
	for (const auto &entry : doc_["candidates"].GetArray()) {
		candidates[entry.FindMember("n")->value.GetString()] = entry.FindMember("f")->value.GetUint64();
	}
	return candidates;
}
