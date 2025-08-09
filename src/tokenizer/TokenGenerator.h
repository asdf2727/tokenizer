#pragma once

#include <random>
#include <string>
#include <unordered_map>

#include "../files/CandidatesFile.h"
#include "LomaxDist.h"

class TokenGenerator {
	double temp_sum_ = 0;

	std::random_device rd_;
	std::mt19937 gen_ = std::mt19937(rd_());
	std::uniform_real_distribution<> chance_ = std::uniform_real_distribution<>(0, 1);

	size_t pref_cand_;

	struct Usage {
		size_t uses = 0;
		Usage *parent = nullptr;
		bool enabled = false;
	};
	std::unordered_map <std::string, Usage> uses_;

	size_t tot_cand_ = 0;
	std::vector <std::string> enabled_;
	std::vector <std::string> disabled_;

	LomaxDist score_dist_;
	size_t raw_score_ = 0;
	double score_ = 0;

	template <bool Enable>
	void RunStep(double corr_factor);

	size_t gen_cnt_ = 0;

public:
	TokenGenerator(const CandidatesFile &candidates, size_t pref_token_count);

	void Generate();
	std::vector <std::string> GetSolution() const;
};
