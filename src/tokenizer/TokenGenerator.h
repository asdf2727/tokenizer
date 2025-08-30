#pragma once

#include <atomic>
#include <random>
#include <string>
#include <unordered_map>
#include <utility>

#include "LomaxDist.h"

class TokenGenerator {
	size_t debug_ = 0;

	std::random_device rd_;
	std::mt19937 gen_ = std::mt19937(rd_());
	std::uniform_real_distribution<> chance_ = std::uniform_real_distribution<>(0, 1);

	struct Candidate {
		const std::string token;
		const size_t tot_uses;
		std::atomic <size_t> uses;
		Candidate *parent = nullptr;
		std::atomic <bool> enabled = false;

		explicit Candidate(std::string name, const size_t tot_uses) :
			token(std::move(name)),
			tot_uses(tot_uses),
			uses(tot_uses) {}

		[[nodiscard]] size_t SimulateStep() const;
		template <bool Enable>
		[[nodiscard]] size_t ApplyStep();
	};

	size_t tot_cand_;
	size_t pref_cand_;

	std::mutex enabled_mutex_;
	std::mutex disabled_mutex_;

	std::vector <Candidate *> roots_;
	std::vector <Candidate *> enabled_;
	std::vector <Candidate *> disabled_;

	std::mutex score_mutex_;
	size_t enabled_cnt_ = 0;
	size_t raw_score_ = 0;
	double score_ = 0;

	LomaxDist score_dist_;
	std::atomic <double> temp_;

	std::atomic <size_t> gen_cnt_ = 0;

	bool WillDisable(double &corr_factor);

	template <bool Enable>
	Candidate *RandCandidate ();

	[[nodiscard]] inline double CalcScore(size_t raw_score, size_t enabled_cnt) const;

	template <bool Enable>
	void TryAndStep(double corr_factor);

	void RunStep();

public:
	TokenGenerator(std::unordered_map <std::string, size_t> &&cands, size_t pref_token_count);
	~TokenGenerator();

	void Generate();
	[[nodiscard]] std::vector <std::string> GetSolution() const;
};
