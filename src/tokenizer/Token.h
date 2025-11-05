#pragma once

#include <atomic>
#include <memory>
#include <string>

#include "TokenGenerator.h"

namespace annealing {
	class Token;
	class TokenGenerator;

	enum TokenReadErrCode {
		OK,              // valid read
		BAD_STREAM,      // stream can't be opened or read from
		INVALID_VERSION, // The format of the data is outdated/newer than current version
		BAD_FILE,        // The stream is corrupted or otherwise invalid
		NOT_DONE         // The stream finished earlier than expected
	};

	/**
	 * Reads the set of tokens from a custom binary format
	 * @param in The stream containing the binary data
	 * @param tokens The output vector containing the tokens
	 * @return The error code of the read operator
	 * @note If the error code is BAD_STREAM or INVALID version, tokens is left unchanged.
	 *		Otherwise, the vector is cleared and the parsed data is written into it.
	 */
	TokenReadErrCode ReadTokens (std::istream &in, std::vector <Token> &tokens);
	/**
	 * Writes the set of tokens in a custom binary format
	 * @param out The stream to write the formated data to
	 * @param tokens The input vector containing the tokens
	 */
	void WriteTokens(std::ostream &out, const std::vector <Token> &tokens);
};

class annealing::Token {
	struct Branch {
		Token *parent = nullptr;
		std::atomic<uint64_t> uses;

		explicit Branch(uint64_t uses);
		Branch(const Branch &other);

		/**
		 * Compute the delta raw cost of a branch when enabling/disabling this token
		 * @tparam LeftBranch Whether to compute for the left children (suffixes) or right (prefixes)
		 * @return The absolute delta raw cost (whether enabling or disabling)
		 */
		template <bool LeftBranch>
		[[nodiscard]] double SimulateStep() const;

		/**
		 * Apply enabling/disabling this token on a branch of children
		 * @tparam Enable Whether this token is being enabled or disabled
		 * @tparam LeftBranch Whether to compute for the left children (suffixes) or right (prefixes)
		 * @param saved_uses the uses computed at a previous time when thread safe
		 * @return The absolute delta raw cost (whether enabling or disabling)
		 * @note The value of saved_uses must be computed while the token mutex is locked to avoid race conditions
		 */
		template <bool Enable, bool LeftBranch>
		[[nodiscard]] double ApplyStep(size_t saved_uses) const;
	};

	std::mutex *mutex_;

	Branch l_branch_;
	Branch r_branch_;

	const char32_t chr_;
	std::atomic<bool> enabled_ = false;

	friend class TokenGenerator;

	friend TokenReadErrCode annealing::ReadTokens (std::istream &in, std::vector <Token> &tokens);
	friend void annealing::WriteTokens(std::ostream &out, const std::vector <Token> &tokens);

public:
	Token(char32_t name, uint64_t uses);
	Token(Token &&rhs) noexcept;

	[[nodiscard]] size_t size() const;
	[[nodiscard]] std::string GetName() const;

	void SetLParent(Token *l_par);
	void SetRParent(Token *r_par);

	[[nodiscard]] double SimulateStep() const;

	template <bool Enable>
	double ApplyStep();
};

