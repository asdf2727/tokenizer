#pragma once

#include <cstdint>
#include <vector>

#include "Token.h"

#include "../utils/Multithread.h"

namespace annealing {
	class Trie;
}

class annealing::Trie {
	union FreqToken {
		uint64_t freq = 0;
		size_t index;
	};

	struct Node {
		std::vector <Node *> children;
		FreqToken val;
		char32_t chr;
		uint32_t sub_size = 1;

		explicit Node(const char32_t chr) : chr(chr) {}
		~Node ();

		[[nodiscard]] size_t FindChild(char32_t chd_chr) const;
		bool CreateChild(char32_t chd_chr, size_t pos);

		void CompSize();

		void Merge (Node &other, ThreadPoolDummy &pool);
		void BuildToken (char32_t fst, std::vector<Token> &tokens);
		void CompParents (const Node *pref, Node *suff, std::vector<Token> &tokens) const;
	};

	Node root_ = Node(0);

public:
	[[nodiscard]] uint64_t total() const { return root_.val.freq; }
	[[nodiscard]] uint64_t size() const { return root_.sub_size; }

	void clear();

	void AddString(const char32_t *begin, size_t len);

	void Merge (Trie &from);

	std::vector <Token> BuildTokens ();
};