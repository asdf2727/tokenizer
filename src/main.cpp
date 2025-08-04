#include <filesystem>

#include "data_set/file/MetadataFile.h"

namespace fs = std::filesystem;

int main() {
	const std::string data_path = "../../Input Data/Raw Text/enwiki 2020-10-20/.metadata.json";
	data_set::MetadataFile metadata(data_path.data());
}