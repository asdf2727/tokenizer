#pragma once

#include <random>
#include <string>
#include <unordered_map>

#include "LomaxDist.h"

class TokenGenerator {
	double temp_sum_ = 0;

	std::random_device rd_;
	std::mt19937 gen_ = std::mt19937(rd_());
	std::uniform_real_distribution<> chance_ = std::uniform_real_distribution<>(0, 1);

	size_t pref_cand_;

	struct Candidate {
		const std::string token;
		size_t uses = 0;
		Candidate *parent = nullptr;
		bool enabled = false;

		explicit Candidate(std::string &&name) : token(std::move(name)) {}
	};
	std::vector <Candidate *> enabled_;
	std::vector <Candidate *> disabled_;

	size_t tot_cand_ = 0;
	size_t raw_score_ = 0;
	LomaxDist score_dist_;
	double score_ = 0;

	size_t SimulateStep(size_t uses, const Candidate *parent) const;
	template <bool Enable>
	void ApplyStep(size_t uses, Candidate *parent);

	template <bool Enable>
	double CalcScore(size_t delta_raw_score, double corr_factor);

	template <bool Enable>
	void RunStep(double corr_factor);

	size_t gen_cnt_ = 0;

public:
	TokenGenerator(std::unordered_map <std::string, size_t> &&cands, size_t pref_token_count);
	~TokenGenerator();

	void Generate();
	std::vector <std::string> GetSolution() const;
};
