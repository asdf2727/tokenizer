#pragma once

#include <string>
#include <unordered_map>
#include <vector>

#include "JsonFile.h"

class TokenFile : JsonFile {
	std::unordered_map <std::string, size_t> tokens_;
	std::vector <const std::string *> ids_;
	size_t max_len_;

	bool Validate();

	void BuildDoc();

public:
	explicit TokenFile(const std::string &path);
	TokenFile(const std::vector <std::string> &tokens, const std::string &path);

	size_t GetId(const std::string &token) const;
	const char *GetToken(size_t id) const;

	std::vector <size_t> Tokenize(std::string input) const;
	std::string Detokenize(const std::vector <size_t> &ids) const;

	std::string Prettify (const std::vector <size_t> &ids) const;
};
