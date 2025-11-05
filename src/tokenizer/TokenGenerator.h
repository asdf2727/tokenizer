#pragma once

#include <atomic>
#include <random>
#include <string>
#include <thread>

#include "LomaxDist.h"

namespace annealing {
	class TokenGenerator;
	class Token;
}

class annealing::TokenGenerator {
	const size_t tot_cand_ = 0;
	const size_t pref_cand_;
	const size_t batch_size_;

	std::vector <std::mutex> mutexes_;
	std::vector <Token> tokens_;

	std::vector<Token*> roots_;
	std::mutex enabled_mutex_;
	std::vector<Token*> enabled_;
	std::mutex disabled_mutex_;
	std::vector<Token*> disabled_;

	std::atomic<size_t> enabled_cnt_ = 0;
	std::atomic<double> raw_score_ = 0;
	std::atomic<double> temp_;

	std::mutex dist_mutex_;
	LomaxDist score_dist_;

	std::atomic<size_t> gen_cnt_ = 0;

	static Token* RandCandidate(std::vector<Token*> &from);

	[[nodiscard]] inline double CalcScore(double raw_score, size_t enabled_cnt) const;

	template <bool Enable>
	std::vector<Token*> RunBatch(size_t work_cnt, double *samples);

	void WorkerTask();

public:
	TokenGenerator(std::vector <Token> &&tokens, size_t pref_token_count,
	               size_t batch_size = std::thread::hardware_concurrency());

	void Generate(size_t pass_cnt = -1);
	[[nodiscard]] std::vector<std::string> GetSolution() const;
};
