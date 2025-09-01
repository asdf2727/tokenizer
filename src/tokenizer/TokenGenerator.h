#pragma once

#include <atomic>
#include <random>
#include <string>
#include <unordered_map>
#include <utility>

#include "LomaxDist.h"

class TokenGenerator {
	const size_t tot_cand_ = 0;
	const size_t pref_cand_;

	struct Candidate {
		const std::string token;
		Candidate *parent = nullptr;
		//const uint64_t tot_uses;

		std::mutex mutex;
		std::atomic <uint64_t> uses;
		std::atomic <bool> enabled = false;

		explicit Candidate(std::string name, const uint64_t tot_uses) :
			token(std::move(name)),
			//tot_uses(tot_uses),
			uses(tot_uses) {}

		[[nodiscard]] int64_t SimulateStep() const;
		template <bool Enable>
		[[nodiscard]] int64_t ApplyStep();
	};

	std::vector <Candidate *> roots_;
	std::mutex enabled_mutex_;
	std::vector <Candidate *> enabled_;
	std::mutex disabled_mutex_;
	std::vector <Candidate *> disabled_;

	std::atomic <size_t> enabled_cnt_ = 0;
	std::atomic <uint64_t> raw_score_ = 0;
	std::atomic <double> temp_;

	std::mutex dist_mutex_;
	LomaxDist score_dist_;

	std::atomic <size_t> gen_cnt_ = 0;

	static Candidate* RandCandidate(std::vector<Candidate *> &from);

	[[nodiscard]] inline double CalcScore(size_t raw_score, size_t enabled_cnt) const;

	template <bool Enable>
	size_t TryAndStep(Candidate *cand);

	template <bool Enable>
	std::vector <Candidate *>  RunBatch(size_t work_cnt, double corr_factor);

	void WorkerTask();

public:
	TokenGenerator(std::unordered_map <std::string, size_t> &&cands, size_t pref_token_count);
	~TokenGenerator();

	void Generate();
	[[nodiscard]] std::vector <std::string> GetSolution() const;
};
