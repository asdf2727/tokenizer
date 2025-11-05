#include "GetTokens.h"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <ranges>
#include <unordered_map>

#include <utf8cpp/utf8.h>

#include "../files/DataFile.h"
#include "../files/MetadataFile.h"
#include "../utils/Multithread.h"
#include "Trie.h"

using namespace annealing;

// TODO add check for candidate max len and rebuild if false

void ExtractCandidates(Trie &into, const std::string &text, const uint8_t max_token_length) {
	std::vector <char32_t> parsed;
	utf8::unchecked::utf8to32(text.data(), text.data() + text.size(), back_inserter(parsed));
	for (size_t i = 0; i < parsed.size(); i++) {
		into.AddString(parsed.data() + i, std::min(parsed.size() - i, (size_t)max_token_length));
	}
}

constexpr size_t kMergeSize = 4'000'000;

Trie FileCandidates(const MetadataFile &metadata, const uint8_t max_len, const size_t file_cnt) {
	const std::filesystem::path root_path = metadata.GetRootPath();
	const std::vector<MetadataFile::Entry> files = metadata.GetFiles(file_cnt);
	std::cout << "Extracting tokens from " << files.size() << " files..." << std::endl;

	ThreadPool pool;

	std::condition_variable dump_freq;
	std::mutex merge_mutex;
	Trie global_freq;
	std::mutex map_mutex;
	std::unordered_map <std::thread::id, Trie> local_freq;
	std::queue <ThreadPool::TaskRef> dep_queue;

	for (int i = 0; i < files.size(); i++) {
		auto path = root_path / files[i].path;
		const DataFile file(path);
		if (!file.IsValid()) {
			std::cerr << "Invalid file " << path << std::endl;
			continue;
		}
		if (i >= 3) {
			pool.Wait({dep_queue.front()});
			dep_queue.pop();
		}
		std::cout << "File " << i << " started" << std::endl;
		std::vector <ThreadPool::TaskRef> tasks;
		for (const auto &entry : file.GetEntries()) {
			tasks.push_back(pool.Enqueue([text = entry.text, max_len, &global_freq, &merge_mutex, &local_freq, &map_mutex] {
				std::unique_lock lock(map_mutex);
				Trie *my_freq = &local_freq[std::this_thread::get_id()];
				lock.unlock();
				ExtractCandidates(*my_freq, text, max_len);

				if (my_freq->size() < kMergeSize) return;
				lock.lock();
				const auto node = local_freq.extract(std::this_thread::get_id());
				lock.unlock();
				std::lock_guard merge_lock(merge_mutex);
				global_freq.Merge(node.mapped());
				std::cout << (double)((global_freq.size() + 20 * kMergeSize) * (40 + 24)) / (1 << 20) << '\n';
			}));
		}
		dep_queue.push(pool.Enqueue([i] {
			std::cout << "File " << i << " done" << std::endl;
		}, std::move(tasks)));
	}
	{
		std::vector <ThreadPool::TaskRef> deps;
		while (!dep_queue.empty()) {
			deps.push_back(dep_queue.front());
			dep_queue.pop();
		}
		pool.Wait(std::move(deps));
	}
	for (Trie &my_freq : local_freq | std::views::values) {
		global_freq.Merge(my_freq);
	}
	return global_freq;
}

std::vector<Token> annealing::GetTokens(const MetadataFile &metadata, const uint8_t max_len, const size_t file_cnt,
                                        const bool rebuild) {
	const std::filesystem::path file_path = metadata.GetRootPath() / (".candidates-" +
		(file_cnt == -1 ? "all" : std::to_string(file_cnt)) +
		(max_len == 255 ? "" : "-" + std::to_string(max_len)) + ".bin");

	std::vector<Token> tokens;
	if (!rebuild) {
		std::cout << "Reading tokens..." << std::endl;
		std::ifstream fin(file_path, std::ios::binary);
		if (ReadTokens(fin, tokens) == OK) {
			return tokens;
		}
		std::cout << "Invalid file. Rebuilding..." << std::endl;
		tokens.clear();
	}
	Trie freq = FileCandidates(metadata, max_len, file_cnt);
	tokens = freq.BuildTokens();
	std::cout << "Saving " << tokens.size() << " tokens..." << std::endl;
	std::ofstream fout(file_path, std::ios::binary);
	WriteTokens(fout, tokens);

	return tokens;
}
