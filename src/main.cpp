#include <iostream>

#include "files/CandidatesFile.h"
#include "files/DataFile.h"
#include "files/MetadataFile.h"
#include "files/TokenFile.h"
#include "tokenizer/TokenGenerator.h"
#include "utils/Multithread.h"

#define RUN_SIM

const std::string kDataPath = "../../Input Data/Raw Text/enwiki 2020-10-20";

int main() {
	MetadataFile metadata(kDataPath + "/.metadata.json");
#ifdef RUN_SIM
	std::vector <std::string> solution;
	{
		// TODO compare different batch sizes for different thread counts to see if a relation can be inferred
		TokenGenerator generator(GetCandidates(metadata, 15, 20), 30000, 50);
		generator.Generate();
		std::cout << "Vocabulary done, saving..." << std::endl;
		solution = generator.GetSolution();
	}
	TokenFile tkn(solution, kDataPath + "/.tokens.json");
#else
	TokenFile tkn(kDataPath + "/.tokens.json");
#endif

	{
		std::string test_file = metadata.GetFiles().back().path;
		std::cout << "Benchmark on file " << test_file << std::endl;
		std::atomic <size_t> init_size = 0;
		std::atomic <size_t> comp_size = 0;
		DataFile test(metadata.GetRootPath() / test_file);
		ThreadPool pool;
		for (const DataFile::Entry &entry : test.GetEntries()) {
			pool.Enqueue([text = entry.text, &tkn, &comp_size, &init_size] {
				init_size += text.size();
				comp_size += tkn.Tokenize(text).size() - 2;
			});
		}
		std::cout << init_size << " characters, " << comp_size << " tokens - compression factor ";
		std::cout << (double)init_size / comp_size << std::endl;
	}

	while (true) {
		std::string str;
		std::getline(std::cin, str);
		if (str == "exit") break;
		std::vector <size_t> ids = tkn.Tokenize(str);
		std::cout << tkn.Prettify(ids) << '\n';
		std::cout << "Compression factor " << (double)str.size() / (ids.size() - 2) << '\n';
	}
}