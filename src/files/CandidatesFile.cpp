#include "CandidatesFile.h"

#include <algorithm>
#include <unordered_map>

#include <filesystem>
#include <fstream>
#include <iostream>
#include <ranges>

#include "../utils/Multithread.h"
#include "../config.h"

#include "DataFile.h"
#include "MetadataFile.h"

namespace json = rapidjson;
namespace fs = std::filesystem;

struct CandidatesEnv {
	size_t max_token_length;
	std::mutex cand_mutex;
	std::unordered_map <size_t, std::unordered_map <std::string, size_t>> cand;

	CandidatesEnv(const size_t max_length) : max_token_length(max_length) {}
};

struct CandidatesTask {
	std::string file_path;
};

bool FileScanCandidates(CandidatesEnv &env, CandidatesTask &task, const size_t tid) {
	const DataFile data(task.file_path);
	if (!data.IsValid()) return false;

	std::unordered_map <std::string, size_t> *local_freq;
	{
		std::unique_lock lock(env.cand_mutex);
		local_freq = &env.cand[tid];
	}

	for (const DataFile::Entry &entry : data.GetEntries()) {
		std::string text = entry.text;
		std::ranges::transform(text, text.begin(),
							   [](const unsigned char c) { return std::tolower(c); });
		for (size_t i = 0; i < text.size(); i++) {
			for (size_t len = 1; len <= std::min(env.max_token_length, text.size() - i); len++) {
				(*local_freq)[text.substr(i, len)]++;
				if (text[i + len] == ' ') break;
			}
		}
	}
	return true;
}

void CandidatesFile::BuildCandidates(const MetadataFile &metadata, const size_t max_len, const size_t file_cnt) {
	const fs::path root_path = metadata.GetRootPath();
	std::vector <MetadataFile::Entry> files = metadata.GetFiles();
	files.resize(std::min(file_cnt, files.size()));

	std::cout << "Processing " << files.size() << " files." << std::endl;
	std::vector <CandidatesTask> tasks;
	for (int i = 0; i < files.size(); i++) {
		tasks.emplace_back(root_path / files[i].path);
	}
	CandidatesEnv env(max_len);
	DistributeTasks <CandidatesEnv, CandidatesTask>(std::cout, env, tasks, FileScanCandidates);
	std::cout << "Merging thread tallies..." << std::endl;
	for (auto &local_cand : env.cand | std::views::values) {
		candidates_.merge(local_cand);
		for (const auto &[name, freq] : local_cand) {
			candidates_[name] += freq;
		}
		local_cand.clear();
	}
	WriteFile();
}

bool CandidatesFile::ReadFile () {
	std::ifstream fin(file_path_, std::ios::binary);
	char buffer[kBuildVersion.size()];
	fin.read(buffer, kBuildVersion.size());
	if (kBuildVersion != buffer) return false;
	size_t entry_cnt;
	fin.read(reinterpret_cast<char *>(&entry_cnt), sizeof(entry_cnt));
	while (entry_cnt-- && fin.good()) {
		size_t freq = 0;
		size_t byte;
		int sft = 0;
		do {
			byte = fin.get();
			freq |= (byte & 0x7F) << sft;
			sft += 7;
		} while (byte & 0x80);
		std::string name;
		std::getline(fin, name, '\0');
		candidates_[name] = freq;
	}
	std::cout << std::endl;
	if (!fin.good()) return false;
	fin.get();
	return fin.eof();
}
void CandidatesFile::WriteFile () {
 	std::ofstream fout(file_path_, std::ios::binary);
	fout << kBuildVersion;
	size_t entry_cnt = candidates_.size();
	fout.write(reinterpret_cast<char *>(&entry_cnt), sizeof(entry_cnt));
	for (const auto &[name, freq] : candidates_) {
		size_t copy = freq;
		while (copy) {
			uint8_t byte = copy & 0x7F;
			copy >>= 7;
			if (copy) byte |= 0x80;
			fout.put(byte);
		}
		fout << name;
		fout.put('\0');
	}
}

CandidatesFile::CandidatesFile(const MetadataFile &metadata, const size_t max_len, const size_t file_cnt, const bool rebuild):
	file_path_(metadata.GetRootPath() / (".candidates-" + (file_cnt == -1 ? "all" : std::to_string(file_cnt)) + ".bin")) {
	if(!rebuild && ReadFile()) return;
	BuildCandidates(metadata, max_len, file_cnt);
	WriteFile();
}
