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
std::vector <std::pair <std::string, size_t>> ReadFile (const std::string &file_path) {
	std::cout << "Getting candidates..." << std::endl;
	std::vector <std::pair <std::string, size_t>> cand;

	std::string str;
	{
		std::ostringstream sstr;
		std::ifstream fin(file_path, std::ios::binary);
		if (!fin.good()) return {};
		sstr << fin.rdbuf();
		str = sstr.str();
	}
	const char *pos = str.c_str();

	if (strcmp(pos, kBuildVersion.c_str()) != 0) return {};
	pos += strlen(pos) + 1;

	size_t entry_cnt;
	strncpy((char *)&entry_cnt, pos, sizeof(entry_cnt));
	pos += sizeof(entry_cnt);
	while (entry_cnt-- && pos < str.c_str() + str.size()) {
		size_t freq = 0;
		int sft = 0;
		do {
			freq |= (*pos & 0x7F) << sft;
			sft += 7;
		} while (*pos++ & 0x80);
		cand.emplace_back(std::string(pos), freq);
		pos += strlen(pos) + 1;
	}
	if (pos != str.c_str() + str.size() || entry_cnt != -1) return {};
	return cand;
}

void WriteFile (const std::string &file_path, const std::vector <std::pair <std::string, size_t>> &cand) {
	std::cout << "Saving " << cand.size() << " candidates..." << std::endl;
	std::string buf = kBuildVersion + '\0';
	size_t entry_cnt = cand.size();
	buf.append(reinterpret_cast<char *>(&entry_cnt), sizeof(entry_cnt));
	for (const auto &[name, freq] : cand) {
		size_t copy = freq;
		while (copy) {
			uint8_t byte = copy & 0x7F;
			copy >>= 7;
			if (copy) byte |= 0x80;
			buf.push_back((char)byte);
		}
		buf += name + '\0';
	}

 	std::ofstream fout(file_path, std::ios::binary);
	fout.write(buf.c_str(), buf.size());
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

std::vector <std::pair <std::string, size_t>> BuildCandidates(const MetadataFile &metadata, const size_t max_len, const size_t file_cnt) {
	std::cout << "Building new candidates file..." << std::endl;
	const std::filesystem::path root_path = metadata.GetRootPath();
	std::vector <MetadataFile::Entry> files = metadata.GetFiles();
	files.resize(std::min(file_cnt, files.size()));

	std::unordered_map <std::thread::id, std::unordered_map <std::string, size_t>> cand_map;

	{
		ThreadPool pool;
		const auto *data = new DataFile(root_path / files[0].path);
		for (int i = 0; i < files.size(); i++) {
			std::cout << "Parsing file " << i << "..." << std::endl;
			DataFile *next_data;
			if (i + 1 < files.size()) pool.Enqueue([&next_data, path = root_path / files[i + 1].path] {
				next_data = new DataFile(path);
			});
			if (!data->IsValid()) std::cerr << "Invalid file " << files[i].path << std::endl;
			for (const DataFile::Entry &entry : data->GetEntries()) {
				pool.Enqueue([&cand_map, max_len, text = entry.text] {
					std::unordered_map <std::string, size_t> *my_cand = &cand_map[std::this_thread::get_id()];
					// TODO see which is faster
					//ExtractCandidates(*my_cand, text, max_len);
					std::unordered_map <std::string, size_t> temp;
					ExtractCandidates(temp, text, max_len);
					my_cand->merge(temp);
					for (const auto &[name, freq] : temp) {
						(*my_cand)[name] += freq;
					}
				});
			}
			pool.Wait();
			delete data;
			data = next_data;
		}
	}

	std::cout << "Candidates extracted, merging results..." << std::endl;
	std::vector <std::pair <std::string, size_t>> ret;
	{
		std::vector <std::unordered_map <std::string, size_t>> cand_vec;
		for (auto &&val : cand_map | std::views::values) {
			cand_vec.push_back(std::move(val));
		}
		cand_map.clear();
		ThreadPool pool;
		while (cand_vec.size() > 1) {
			const size_t new_size = (cand_vec.size() + 1) / 2;
			std::cout << "Merging " << cand_vec.size() << " to " << new_size << std::endl;
			for (int i = 0; i < cand_vec.size() / 2; i++) {
				pool.Enqueue([to = &cand_vec[i], from = &cand_vec[new_size + i]] {
					to->merge(*from);
					while (!from->empty()) {
						auto node = from->extract(from->begin());
						(*to)[node.key()] += node.mapped();
					}
				});
			}
			pool.Wait();
			cand_vec.resize(new_size);
		}
		std::cout << "Compressing final result..." << std::endl;
		while (!cand_vec[0].empty()) {
			auto temp = cand_vec[0].extract(cand_vec[0].cbegin());
			if (temp.mapped() < 2) continue;
			ret.emplace_back(std::move(temp.key()), temp.mapped());
		}
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

std::vector <std::pair <std::string, size_t>> GetCandidates(const MetadataFile &metadata, const size_t max_len, const size_t file_cnt, const bool rebuild) {
	const std::string file_path = metadata.GetRootPath() / (".candidates-" +
		(file_cnt == -1 ? "all" : std::to_string(file_cnt)) +
		(max_len == -1 ? "" : "-" + std::to_string(max_len)) + ".bin");
	std::vector <std::pair <std::string, size_t>> cand;
	if(!rebuild) cand = ReadFile(file_path);
	if (cand.empty()) {
		cand = BuildCandidates(metadata, max_len, file_cnt);
		WriteFile(file_path, cand);
	}
	return cand;
}
