#include "Trie.h"

#include <cassert>
#include <iostream>

using namespace annealing;

constexpr size_t kMinFreq = 1;

Trie::Node::~Node() {
	for (const Node *child : children) {
		delete child;
	}
}

size_t Trie::Node::FindChild(const char32_t chd_chr) const {
	size_t pos = 0;
	if (children.empty()) return pos;
	for (size_t pow = 1LL << (63 - __builtin_clzll(children.size() | 1)); pow; pow >>= 1) {
		const size_t new_pos = pos | pow;
		if (new_pos < children.size() && children[new_pos]->chr < chd_chr) {
			pos = new_pos;
		}
	}
	if (children[pos]->chr < chd_chr) pos++;
	return pos;
}
bool Trie::Node::CreateChild(const char32_t chd_chr, const size_t pos) {
	if (pos < children.size() && children[pos]->chr == chd_chr) return false;
	children.push_back(nullptr);
	for (size_t i = children.size() - 1; i > pos; --i) {
		children[i] = children[i - 1];
	}
	children[pos] = new Node(chd_chr);
	sub_size++;
	return true;
}

void Trie::Node::CompSize() {
	sub_size = 1;
	for (const Node *child : children) {
		sub_size += child->sub_size;
	}
}

void Trie::Node::Merge(Node &other, ThreadPoolDummy &pool) {
	if (other.val.freq < kMinFreq) return;
	val.freq += other.val.freq;
	if (children.empty()) {
		std::swap(children, other.children);
		sub_size = other.sub_size;
		return;
	}
	std::vector <Node *> paste;
	auto pos1 = children.begin();
	for (auto *pos2 : other.children) {
		while (pos1 < children.end() && (*pos1)->chr < pos2->chr) {
			++pos1;
		}
		if (pos1 < children.end() && (*pos1)->chr == pos2->chr) {
			pool.Enqueue([node = *pos1, pos2 = pos2, &pool] {
				node->Merge(*pos2, pool);
				delete pos2;
			});
		}
		else {
			paste.push_back(pos2);
		}
	}
	other.children.clear();

	size_t from1 = children.size() - 1;
	children.resize(children.size() + paste.size());
	auto from2 = paste.rbegin();
	for (size_t to = children.size() - 1; to < children.size() && to != from1; --to) {
		children[to] = from1 != -1 && (*from2)->chr < children[from1]->chr ? children[from1--] : *(from2++);
	}
	CompSize();
}

void Trie::Node::BuildToken(const char32_t fst, std::vector <Token> &tokens) {
	tokens.emplace_back(fst, val.freq);
	val.index = tokens.size() - 1;
	for (Node *chd : children) {
		if (chd->val.freq < kMinFreq) {
			chd->val.freq = -1;
			continue;
		}
		chd->BuildToken(fst, tokens);
	}
}
void Trie::Node::CompParents(const Node *pref, Node *suff, std::vector <Token> &tokens) const {
	if (val.freq == -1) return;
	const size_t pos = suff->FindChild(chr);
	assert(pos < suff->children.size() && suff->children[pos]->chr == chr);
	suff = suff->children[pos];
	assert(suff->val.freq != -1);
	tokens[val.index].SetRParent(&tokens[pref->val.index]);
	tokens[val.index].SetLParent(&tokens[suff->val.index]);
	for (const Node *chd : children) {
		chd->CompParents(this, suff, tokens);
	}
}

void Trie::clear() {
	for (const Node *node : root_.children) {
		delete node;
	}
	root_.children.clear();
	root_.sub_size = 1;
	root_.val.freq = 0;
}

void Trie::AddString(const char32_t *begin, const size_t len) {
	Node *branch[len + 1];
	branch[0] = &root_;
	++root_.val.freq;
	for (size_t i = 0; i < len; ++i) {
		const size_t pos = branch[i]->FindChild(*begin);
		branch[i]->CreateChild(*begin, pos);
		branch[i + 1] = branch[i]->children[pos];
		++branch[i + 1]->val.freq;
		++begin;
	}
	for (size_t i = len; i <= len; --i) {
		branch[i]->CompSize();
	}
}

void Trie::Merge (Trie &from) {
	ThreadPoolDummy pool;
	root_.Merge(from.root_, pool);
	pool.Wait();
	from.clear();
}

std::vector <Token> Trie::BuildTokens () {
	std::cout << "Building token objects..." << std::endl;
	std::vector <Token> tokens;
	for (Node *node : root_.children) {
		node->BuildToken(node->chr, tokens);
	}
	std::cout << "Computing parents..." << std::endl;
	for (const Node *chd : root_.children) {
		for (const Node *chd2 : chd->children) {
			chd2->CompParents(chd, &root_, tokens);
		}
	}
	std::cout << "Deleting trie..." << std::endl;
	clear();
	return tokens;
}