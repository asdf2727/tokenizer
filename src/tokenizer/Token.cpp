#include "Token.h"

#include <cassert>
#include <fstream>
#include <iostream>

#include "../config.h"
#include "utf8cpp/utf8.h"

using namespace annealing;

Token::Branch::Branch(const uint64_t uses) : uses(uses) {}
Token::Branch::Branch(const Branch &other) : parent(other.parent), uses((uint64_t)other.uses) {}

#define BRANCH(LeftBranch) (LeftBranch ? node->l_branch_ : node->r_branch_)

template <bool LeftBranch>
[[nodiscard]] double Token::Branch::SimulateStep() const {
	int64_t delta_len = 1;
	for (const Token *node = parent; !node->enabled_; node = BRANCH(LeftBranch).parent) delta_len++;
	return delta_len * uses;
}
template double Token::Branch::SimulateStep <false> () const;
template double Token::Branch::SimulateStep <true> () const;

template <bool Enable, bool LeftBranch>
[[nodiscard]] double Token::Branch::ApplyStep(const size_t saved_uses) const {
	int64_t delta_len = 1;
	for (Token *node = parent; true; node = BRANCH(LeftBranch).parent) {
		assert(node != nullptr);
		std::lock_guard node_lock(*node->mutex_);
		BRANCH(LeftBranch).uses -= (Enable ? 1 : -1) * saved_uses;
		if (node->enabled_) break;
		delta_len++;
	}
	return delta_len * saved_uses;
}
template double Token::Branch::ApplyStep <false, false> (size_t saved_uses) const;
template double Token::Branch::ApplyStep <false, true> (size_t saved_uses) const;
template double Token::Branch::ApplyStep <true, false> (size_t saved_uses) const;
template double Token::Branch::ApplyStep <true, true> (size_t saved_uses) const;

#undef BRANCH

Token::Token(const char32_t name, const uint64_t uses) :
	l_branch_(uses),
	r_branch_(uses),
	chr_(name) {}
Token::Token(Token &&rhs) noexcept :
	mutex_(std::move(rhs.mutex_)),
	l_branch_(rhs.l_branch_),
	r_branch_(rhs.r_branch_),
	chr_(rhs.chr_),
	enabled_((bool)rhs.enabled_) {}

size_t Token::size() const {
	size_t len = 1;
	const Token *par = r_branch_.parent;
	while (par != nullptr) {
		len++;
		par = par->r_branch_.parent;
	}
	return len;
}
std::string Token::GetName() const {
	std::string str;
	utf8::append(chr_, str);
	const Token *par = l_branch_.parent;
	while (par != nullptr) {
		utf8::append(par->chr_, str);
		par = par->l_branch_.parent;
	}
	return str;
}

void Token::SetLParent(Token *l_par) {
	l_branch_.parent = l_par;
}
void Token::SetRParent(Token *r_par) {
	r_branch_.parent = r_par;
}

double Token::SimulateStep() const {
	double score = 0;
	score += l_branch_.SimulateStep<true>();
	score += r_branch_.SimulateStep<false>();
	return score;
}

template <bool Enable>
double Token::ApplyStep() {
	uint64_t loc_l_uses;
	uint64_t loc_r_uses;
	{
		assert(mutex_ != nullptr);
		std::lock_guard my_lock(*mutex_);
		enabled_ = Enable;
		loc_l_uses = l_branch_.uses;
		loc_r_uses = r_branch_.uses;
	}
	double score = 0;
	score += l_branch_.ApplyStep<Enable, true>(loc_l_uses);
	score += r_branch_.ApplyStep<Enable, false>(loc_r_uses);
	return score;
}
template double Token::ApplyStep <false> ();
template double Token::ApplyStep <true> ();

TokenReadErrCode annealing::ReadTokens(std::istream &in, std::vector <Token> &tokens) {
	if (!in.good()) return BAD_STREAM;
	std::streambuf *buf = in.rdbuf();

	char version[kBuildVersion.size() + 1];
	buf->sgetn(version, sizeof(version));
	if (version[kBuildVersion.size()] != '\0') return INVALID_VERSION;
	if (strcmp(version, kBuildVersion.c_str()) != 0) return INVALID_VERSION;

	tokens.clear();
	size_t entry_cnt;
	buf->sgetn((char*)&entry_cnt, sizeof(entry_cnt));
	if (entry_cnt == 0) return OK;
	tokens.reserve(entry_cnt);

	constexpr size_t read_size = 8;
	char read[read_size];
	buf->sgetn(read, read_size);
	while (entry_cnt-- && buf->in_avail()) {
		char *use = read;
		const char32_t name = utf8::unchecked::next(use);

		uint64_t uses = 0;
		int sft = 0;
		uint8_t byte;
		do {
			byte = use < read + read_size ? *use++ : buf->sgetc();
			uses |= (byte & 0x7F) << sft;
			sft += 7;
		} while (byte & 0x80);

		*(int64_t*)read = *(int64_t*)use;
		const long read_cnt = use - read;
		buf->sgetn(read + read_size - read_cnt, read_cnt);

		tokens.emplace_back(name, uses);
	}
	if (entry_cnt != -1) return BAD_FILE;

	for (int i = 0; i < 8; i++) {
		buf->sungetc();
	}
	for (auto &token : tokens) {
		uint32_t index;
		buf->sgetn((char*)&index, sizeof(index));
		token.l_branch_.parent = index == -1 ? nullptr : &tokens[index];
		buf->sgetn((char*)&index, sizeof(index));
		token.r_branch_.parent = index == -1 ? nullptr : &tokens[index];
	}

	if (buf->in_avail() != 0) return NOT_DONE;
	return OK;
}

void annealing::WriteTokens(std::ostream &out, const std::vector<Token> &tokens) {
	std::streambuf *buf = out.rdbuf();

	buf->sputn(kBuildVersion.data(), kBuildVersion.size());
	buf->sputc('\0');
	const size_t token_cnt = tokens.size();
	buf->sputn((char*)&token_cnt, sizeof(token_cnt));

	char write[4];
	for (const auto &tkn : tokens) {
		//std::cout << tkn.GetName() << ' ' << tkn.l_branch_.uses << '\n';
		const char *stop = utf8::utf32to8(&tkn.chr_, &tkn.chr_ + 1, write);
		buf->sputn(write, stop - write);

		size_t copy = tkn.l_branch_.uses;
		while (copy) {
			uint8_t byte = copy & 0x7F;
			copy >>= 7;
			if (copy) byte |= 0x80;
			buf->sputc(byte);
		}
	}

	for (const auto &tkn : tokens) {
		uint32_t index = tkn.l_branch_.parent == nullptr ? -1 : tkn.l_branch_.parent - tokens.data();
		buf->sputn((char*)&index, sizeof(index));
		index = tkn.r_branch_.parent == nullptr ? -1 : tkn.r_branch_.parent - tokens.data();
		buf->sputn((char*)&index, sizeof(index));
	}
	out.flush();
}
