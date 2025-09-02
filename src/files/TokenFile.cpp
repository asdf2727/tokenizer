#include "TokenFile.h"

#include <algorithm>
#include <iostream>
#include <ranges>

#include "../config.h"

namespace json = rapidjson;

const std::string kStartToken = "<START>";
const std::string kEndToken = "<END>";

bool TokenFile::Validate() {
	if (!doc_.IsObject()) return false;
	if (!doc_.HasMember("version")) return false;
	if (!doc_["version"].IsString()) return false;
	if (kBuildVersion != doc_["version"].GetString()) return false;

	if (!doc_.HasMember("tokens")) return false;
	if (!doc_["tokens"].IsArray()) return false;
	for (const auto &entry : doc_["tokens"].GetArray()) {
		if (!entry.IsString()) return false;
	}
	return true;
}
void TokenFile::BuildDoc() {
	std::cout << "Building new candidates file..." << std::endl;
	modified_ = true;
	doc_.SetObject();
	json::Document::AllocatorType &alloc = doc_.GetAllocator();
	doc_.AddMember("version", json::StringRef(kBuildVersion.c_str()), alloc);

	json::Value tokens(json::kArrayType);
	for (size_t i = 2; i < ids_.size(); i++) {
		tokens.PushBack(json::Value(ids_[i]->c_str(), alloc), alloc);
	}
	doc_.AddMember("tokens", tokens, alloc);

	Save();
}

TokenFile::TokenFile(const std::string &path):
	JsonFile(path, false),
	max_len_(0) {
	if (valid_) valid_ = Validate();
	if (!valid_) return;

	ids_.resize(doc_["tokens"].GetArray().Size() + 2);
	size_t cnt = 0;
	ids_[cnt++] = &kStartToken;
	ids_[cnt++] = &kEndToken;
	for (const auto &entry : doc_["tokens"].GetArray()) {
		std::string token = entry.GetString();
		max_len_ = std::max(max_len_, token.size());
		ids_[cnt++] = &tokens_.insert({std::move(token), cnt}).first->first;
	}
}
TokenFile::TokenFile(const std::vector <std::string> &tokens, const std::string &path):
	JsonFile(path, true),
	ids_(tokens.size() + 2),
	max_len_(0) {

	size_t cnt = 0;
	ids_[cnt++] = &kStartToken;
	ids_[cnt++] = &kEndToken;
	for (const auto &token : tokens) {
		max_len_ = std::max(max_len_, token.size());
		ids_[cnt++] = &tokens_.insert({token, cnt}).first->first;
	}
	BuildDoc();
}

size_t TokenFile::GetId(const std::string &token) const {
	const auto it = tokens_.find(token);
	return it == tokens_.end() ? -1 : it->second;
}

const char* TokenFile::GetToken(const size_t id) const {
	return id == -1 ? "<UNKNOWN>" : ids_[id]->c_str();
}

std::vector <size_t> TokenFile::Tokenize(std::string input) const {
	std::vector <size_t> ids;
	ids.push_back(0);
	size_t pos = 0;
	std::ranges::transform(input, input.begin(),
	                       [](const unsigned char c) { return std::tolower(c); });
	while (pos < input.size()) {
		for (size_t len = std::min(max_len_, input.size() - pos); len > 0; --len) {
			size_t id = GetId(input.substr(pos, len));
			if (id == -1 && len > 1) continue;
			ids.push_back(id);
			pos += len;
			break;
		}
	}
	ids.push_back(1);
	return ids;
}

std::string TokenFile::Detokenize(const std::vector <size_t> &ids) const {
	std::string text;
	for (const size_t id : ids) {
		text += GetToken(id);
	}
	return text;
}

std::string TokenFile::Prettify(const std::vector <size_t> &ids) const {
	std::string ans;
	for (const size_t id : ids) {
		ans += GetToken(id);
		ans += '|';
	}
	ans.pop_back();
	return ans;
}
