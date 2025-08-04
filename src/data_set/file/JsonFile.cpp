#include "JsonFile.h"

#include <fstream>
#include <iostream>
#include <vector>

#include "rapidjson/document.h"
#include "rapidjson/filereadstream.h"
#include "rapidjson/prettywriter.h"
#include "rapidjson/filewritestream.h"

std::vector<char> ReadFile(const char *path) {
	std::ifstream ifs(path, std::ios::in | std::ios::binary | std::ios::ate);
	const size_t file_size = ifs.tellg();
	ifs.seekg(0, std::ios::beg);
	std::vector<char> bytes(file_size + 1);
	ifs.read(&bytes[0], file_size);
	bytes[file_size] = '\0';
	return bytes;
}

void data_set::JsonFile::Save() {
	if (!modified_) return;
	FILE *fp = fopen(path_, "w");
	char buffer[65536];
	rapidjson::FileWriteStream frs(fp, buffer, sizeof buffer);
	rapidjson::PrettyWriter writer(frs);
	writer.SetFormatOptions(rapidjson::kFormatSingleLineArray);
	doc_.Accept(writer);
	fclose(fp);
	modified_ = false;
}

data_set::JsonFile::JsonFile (const char *path) {
	if (path == nullptr) return;
	path_ = strdup(path);
	FILE *fp = fopen(path_, "r");
	if (fp == nullptr) return;
	char buffer[65536];
	rapidjson::FileReadStream frs(fp, buffer, sizeof buffer);
	doc_.ParseStream(frs);
	fclose(fp);
	if (doc_.HasParseError()) {
		std::cerr << "Json parse error " << doc_.GetParseError() << " in file " << path << std::endl;
	}
}
data_set::JsonFile::~JsonFile () {
	Save();
	doc_.SetNull();
	free(path_);
}