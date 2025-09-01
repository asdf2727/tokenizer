#include <iostream>

#include "files/CandidatesFile.h"
#include "files/DataFile.h"
#include "files/MetadataFile.h"
#include "tokenizer/TokenGenerator.h"
#include "files/TokenFile.h"

#define RUN_SIM

//const std::string kDataPath = "../../Input Data/Raw Text/enwiki 2020-10-20";
const std::string kDataPath = "../../Input Data/Raw Text/test";

int main() {
	MetadataFile metadata(kDataPath + "/.metadata.json");
#ifdef RUN_SIM
	std::vector <std::string> solution;
	{
		TokenGenerator generator(GetCandidates(metadata, 10), 30000);
		generator.Generate();
		solution = generator.GetSolution();
	}
	TokenFile tkn(solution, kDataPath + "/.tokens.json");
#else
	TokenFile tkn(kDataPath + "/.tokens.json");
#endif

	/*std::string test_file = metadata.GetFiles().back().path;
	std::cout << "Benchmark on file " << test_file << std::endl;
	size_t init_size = 0;
	size_t comp_size = 0;
	DataFile test(metadata.GetRootPath() / test_file);
	for (const DataFile::Entry &entry : test.GetEntries()) {
		std::string text = entry.text;
		init_size += text.size();
		comp_size += tkn.Tokenize(text).size();
	}
	std::cout << init_size << " characters, " << comp_size << " tokens - compression factor ";
	std::cout << (double)init_size / comp_size << std::endl;

	while (true) {
		std::string str;
		std::getline(std::cin, str);
		std::vector <size_t> ids = tkn.Tokenize(str);
		std::cout << tkn.Prettify(ids) << '\n';
		std::cout << "Compression factor " << (double)str.size() / (ids.size() - 2) << '\n';
	}*/
}