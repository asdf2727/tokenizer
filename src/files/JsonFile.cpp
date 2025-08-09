#include "JsonFile.h"

#include <fstream>
#include <iostream>
#include <utility>

#include "rapidjson/document.h"
#include "rapidjson/filereadstream.h"
#include "rapidjson/prettywriter.h"
#include "rapidjson/filewritestream.h"

#include "../config.h"

JsonFile::JsonFile (std::string path, const bool no_read) :
	path_(std::move(path)) {
	valid_ = false;
	if (no_read) return;
	FILE *fp = fopen(path_.c_str(), "r");
	if (fp == nullptr) return;
	char buffer[65536];
	rapidjson::FileReadStream frs(fp, buffer, sizeof buffer);
	doc_.ParseStream(frs);
	fclose(fp);

	if (doc_.HasParseError()) return;

	valid_ = true;
}
JsonFile::~JsonFile () {
	Save();
	doc_.SetNull();
}

void JsonFile::Save() {
	if (!modified_) return;
	valid_ = false;
	FILE *fp = fopen(path_.c_str(), "w");
	char buffer[65536];
	rapidjson::FileWriteStream frs(fp, buffer, sizeof buffer);
	rapidjson::PrettyWriter writer(frs);
	writer.SetFormatOptions(rapidjson::kFormatSingleLineArray);
	doc_.Accept(writer);
	fclose(fp);
	modified_ = false;
}