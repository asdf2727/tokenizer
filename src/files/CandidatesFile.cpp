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

// TODO add check for candidate max len and rebuild if false
std::unordered_map <std::string, size_t> ReadFile (const std::string &file_path) {
	std::ifstream fin(file_path, std::ios::binary);
	std::unordered_map <std::string, size_t> cand;
	char buffer[kBuildVersion.size() + 1];
	fin.read(buffer, kBuildVersion.size() + 1);
	if (buffer[kBuildVersion.size()] != '\0') return cand;
	if (kBuildVersion != buffer) return cand;
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
		cand[name] = freq;
	}
	if (!fin.good()) cand.clear();
	fin.get();
	if (!fin.eof()) cand.clear();
	return cand;
}

void WriteFile (const std::string &file_path, const std::unordered_map <std::string, size_t> &cand) {
 	std::ofstream fout(file_path, std::ios::binary);
	fout << kBuildVersion << '\0';
	size_t entry_cnt = cand.size();
	fout.write(reinterpret_cast<char *>(&entry_cnt), sizeof(entry_cnt));
	for (const auto &[name, freq] : cand) {
		size_t copy = freq;
		while (copy) {
			uint8_t byte = copy & 0x7F;
			copy >>= 7;
			if (copy) byte |= 0x80;
			fout.put(byte);
		}
		fout << name << '\0';
	}
}

struct ScanCandidatesEnv {
	size_t max_token_length;
	std::mutex cand_mutex;
	std::vector <std::unordered_map <std::string, size_t>> cand;

	ScanCandidatesEnv(const size_t max_length) : max_token_length(max_length) {}
};

struct ScanCandidatesTask {
	std::string file_path;
};

bool ScanCandidates(ScanCandidatesEnv &env, ScanCandidatesTask &task, const size_t tid) {
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

void ExtractCandidates(std::unordered_map <std::string, size_t> &into, const std::string &text, const size_t max_token_length) {
	std::string str(text);
	std::ranges::transform(str, str.begin(),
						   [](const unsigned char c) { return std::tolower(c); });
	for (size_t i = 0; i < str.size(); i++) {
		for (size_t len = 1; len <= std::min(max_token_length, str.size() - i); len++) {
			into[str.substr(i, len)]++;
			if (str[i + len] == ' ') break;
		}
	}
}

std::unordered_map <std::string, size_t> BuildCandidates(const MetadataFile &metadata, const size_t max_len, const size_t file_cnt) {
	std::cout << "Building new candidates file..." << std::endl;
	const std::filesystem::path root_path = metadata.GetRootPath();
	std::vector <MetadataFile::Entry> files = metadata.GetFiles();
	files.resize(std::min(file_cnt, files.size()));

	std::mutex cand_mutex;
	std::unordered_map <std::thread::id, std::unordered_map <std::string, size_t>> cand_map;

	{
		ThreadPool pool;
		for (auto &file : files) {
			pool.Enqueue([&root_path, &file, &cand_map, &cand_mutex, max_len] {
				const DataFile data(root_path / file.path);
				if (!data.IsValid()) std::cerr << "Invalid file " << file.path << std::endl;

				std::unordered_map <std::string, size_t> *my_cand;
				{
					std::unique_lock lock(cand_mutex);
					my_cand = &cand_map[std::this_thread::get_id()];
				}

				for (const DataFile::Entry &entry : data.GetEntries()) ExtractCandidates(*my_cand, entry.text, max_len);
			});
		}
		pool.Wait();
	}

	std::cout << "Candidates extracted, merging results..." << std::endl;
	std::unordered_map<std::string, size_t> ret;
	{
		std::vector <std::unordered_map <std::string, size_t>> cand_vec;
		for (auto &&val : cand_map | std::views::values) {
			cand_vec.push_back(std::move(val));
		}
		cand_map.clear();
		ThreadPool pool;
		size_t tasks_done = 0;
		while (cand_vec.size() > 1) {
			const size_t new_size = (cand_vec.size() + 1) / 2;
			for (int i = 0; i < cand_vec.size() / 2; i++) {
				pool.Enqueue([to = &cand_vec[i], from = &cand_vec[new_size + i]] {
					to->merge(*from);
					for (const auto &[name, freq] : *from) {
						(*to)[name] += freq;
					}
					from->clear();
				});
			}
			pool.Wait();
			std::cout << "Merged " << cand_vec.size() << " to " << new_size << std::endl;
			cand_vec.resize(new_size);
		}
		ret = std::move(cand_vec[0]);
	}
	return ret;
}

/*bool CandidatesFile::CheckFile () {
	std::ifstream fin(file_path_, std::ios::binary);
	char buffer[kBuildVersion.size() + 1];
	fin.read(buffer, kBuildVersion.size() + 1);
	if (buffer[kBuildVersion.size()] != '\0') return false;
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
		auto it = candidates_.find(name);
		if (it == candidates_.end()) {
			std::cerr << "oops1" << std::endl;
			return false;
		}
		if (it->second != freq) {
			std::cerr << std::hex << it->second << ' ' << freq << std::endl;
			return false;
		}
	}
	if (!fin.good()) {
		std::cerr << "oops2" << std::endl;
		return false;
	}
	fin.get();
	if (!fin.eof()) {
		std::cerr << "oops3" << std::endl;
		return false;
	}
	return fin.eof();
}*/

std::unordered_map <std::string, size_t> GetCandidates(const MetadataFile &metadata, const size_t max_len, const size_t file_cnt, const bool rebuild) {
	const std::string file_path = metadata.GetRootPath() / (".candidates-" + (file_cnt == -1 ? "all" : std::to_string(file_cnt)) + ".bin");
	std::unordered_map <std::string, size_t> cand;
	if(!rebuild) cand = ReadFile(file_path);
	if (cand.empty()) {
		cand = BuildCandidates(metadata, max_len, file_cnt);
		WriteFile(file_path, cand);
	}
	return cand;
}
