#include "TokenGenerator.h"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <iostream>
#include <mutex>
#include <ranges>

#include "../utils/Multithread.h"

TokenGenerator::TokenGenerator(std::unordered_map<std::string, size_t> &&cands, const size_t pref_token_count) :
	pref_cand_(pref_token_count) {
	std::string str(1, '\0');
	for (int i = 0; i < UINT8_MAX; i++) {
		str[0] = (char)i;
		if (cands.contains(str)) continue;
		cands[str] = 0;
	}
	*(size_t*)&tot_cand_ = cands.size();
	std::cout << "Initializing optimizer with " << tot_cand_ << " candidates..." << std::endl;
	std::unordered_map<std::string, Candidate*> candidates;
	for (auto &[token, freq] : cands) {
		candidates[token] = new Candidate(token, freq);
	}

	std::cout << "Computing parents..." << std::endl;
	score_dist_.SetHalfLife((double)tot_cand_ * 0.25);
	for (auto *cand : candidates | std::views::values) {
		if (cand->token.size() == 1) {
			cand->enabled = true;
			roots_.push_back(cand);
		}
		else {
			disabled_.push_back(cand);
			score_dist_.AddPoint(cand->uses * (cand->token.size() - 1), 1);
			cand->parent = candidates[cand->token.substr(0, cand->token.size() - 1)];
		}
	}
	score_dist_.UpdateParams();
	score_dist_.SetHalfLife((double)tot_cand_ * 0.5);
	// TODO add best candidates as initial solution
}

TokenGenerator::~TokenGenerator() {
	for (const Candidate *cand : enabled_) {
		delete cand;
	}
	for (const Candidate *cand : disabled_) {
		delete cand;
	}
	for (const Candidate *cand : roots_) {
		delete cand;
	}
}


int64_t TokenGenerator::Candidate::SimulateStep() const {
	int64_t delta_len = 1;
	for (const Candidate *par = parent; !par->enabled; par = par->parent) delta_len++;
	return delta_len * uses;
}

template <bool Enable>
int64_t TokenGenerator::Candidate::ApplyStep() {
	uint64_t loc_uses;
	{
		std::lock_guard my_lock(mutex);
		enabled = Enable;
		loc_uses = uses;
	}
	int64_t delta_len = 1;
	for (Candidate *node = parent; true; node = node->parent) {
		std::lock_guard node_lock(node->mutex);
		node->uses -= (Enable ? 1 : -1) * loc_uses;
		if (node->enabled) break;
		delta_len++;
	}
	return delta_len * loc_uses;
}

thread_local std::random_device rd;
thread_local std::mt19937 gen(rd());
thread_local std::uniform_real_distribution<> chance(0, 1);

TokenGenerator::Candidate* TokenGenerator::RandCandidate(std::vector<Candidate*> &from) {
	// TODO try to use chance_ to avoid new generator
	const size_t rand_pos = std::uniform_int_distribution<size_t>(0, from.size() - 1)(gen);
	std::swap(from[rand_pos], from.back());
	Candidate *ret = from.back();
	from.pop_back();
	return ret;
}

inline double TokenGenerator::CalcScore(const size_t raw_score, const size_t enabled_cnt) const {
	if (enabled_cnt == 0) return 0;
	const double contrib = tot_cand_ * score_dist_.GetBest((double)enabled_cnt / tot_cand_);
	const double new_pref_fill = (double)enabled_cnt / pref_cand_;
	return (double)raw_score / contrib * new_pref_fill * (2 - new_pref_fill);
}

thread_local size_t loc_enabled_cnt;
thread_local size_t loc_raw_score;
thread_local double loc_score;

template <bool Enable>
size_t TokenGenerator::TryAndStep(Candidate *cand) {
	const int64_t delta_raw_score = cand->SimulateStep();
	const double new_score = CalcScore(loc_raw_score + (Enable ? delta_raw_score : -delta_raw_score),
	                                   loc_enabled_cnt + (Enable ? 1 : -1));

	// TODO try move with probability k (default annealing) or k / 1+k (correct Boltzmann dist)
	//const bool do_step = new_score > loc_score || chance(gen) < std::exp((new_score - loc_score) / temp_);
	const bool do_step = chance(gen) > 1 / (1 + std::exp((new_score - loc_score) / temp_));

	// TODO consider updating delta_score with actual ApplyStep
	if (do_step) raw_score_ += Enable ? cand->ApplyStep<true>() : -cand->ApplyStep<false>();

	return delta_raw_score;
}

template <bool Enable>
std::vector<TokenGenerator::Candidate*> TokenGenerator::RunBatch(const size_t work_cnt, const double corr_factor) {
	std::vector<Candidate*> working;
	{
		// Extract working candidates
		std::lock_guard lock(Enable ? disabled_mutex_ : enabled_mutex_);
		for (size_t i = 0; i < work_cnt; i++) {
			working.push_back(RandCandidate(Enable ? disabled_ : enabled_));
		}
	}

	// Step each candidate
	std::vector<int64_t> rand_samples;
	rand_samples.reserve(work_cnt);
	for (Candidate *cand : working) {
		rand_samples.push_back(TryAndStep<Enable>(cand));
	}

	{
		// Update score distribution
		std::lock_guard lock(dist_mutex_);
		for (const int64_t sample : rand_samples) {
			score_dist_.AddPoint(sample, corr_factor);
		}
		score_dist_.UpdateParams();
	}

	return working;
}

constexpr size_t kBatchSize = 1;

void TokenGenerator::WorkerTask() {
	loc_enabled_cnt = enabled_cnt_;
	loc_raw_score = raw_score_;
	loc_score = CalcScore(loc_raw_score, loc_enabled_cnt);

	// Calculate the chance of enabling a candidate, based on x (current enabled cnt) and P (preferred enabled cnt)
	// The formula is P(x) = x / [x + (n-x) * p/(n-p)], but I rearranged it to remove floating point arithmetic
	// I did this to combat the tendency of entropy to enable half of the candidates, completely messing up my score function
	const uint64_t enabled_weight = loc_enabled_cnt * (tot_cand_ - pref_cand_);
	const uint64_t disabled_weight = (tot_cand_ - loc_enabled_cnt) * pref_cand_;
	const uint64_t total_weight = enabled_weight + disabled_weight;
	const double corr_enable = (double)total_weight / (tot_cand_ * (tot_cand_ - pref_cand_));
	const double corr_disable = (double)total_weight / (tot_cand_ * pref_cand_);
	const size_t enable_cnt = std::binomial_distribution(kBatchSize, (double)disabled_weight / total_weight)(gen);

	temp_ = 0.0005 * std::exp(-(double)gen_cnt_ / tot_cand_ * 0.4);

	std::vector<Candidate*> enabled;
	std::vector<Candidate*> disabled;
	if (enable_cnt > 0) for (Candidate *cand : RunBatch<true>(enable_cnt, corr_enable)) {
		if (cand->enabled) enabled.push_back(cand);
		else disabled.push_back(cand);
	}
	if (enable_cnt < kBatchSize) for (Candidate *cand : RunBatch<false>(kBatchSize - enable_cnt, corr_disable)) {
		if (cand->enabled) enabled.push_back(cand);
		else disabled.push_back(cand);
	}

	if (!enabled.empty()) {
		std::lock_guard lock(enabled_mutex_);
		enabled_.insert(enabled_.end(), enabled.begin(), enabled.end());
		enabled_cnt_ = enabled_.size();
	}
	if (!disabled.empty()) {
		std::lock_guard lock(disabled_mutex_);
		disabled_.insert(disabled_.end(), disabled.begin(), disabled.end());
	}

	gen_cnt_ += kBatchSize;
}

void TokenGenerator::Generate() {
	std::cout << "Running simulated annealing for " << 30 * tot_cand_ << " steps" << std::endl;
	ThreadPool pool;
	for (int pass = 0; pass < 30; pass++) {
		for (int i = 0; i * kBatchSize < tot_cand_; i++) {
			pool.Enqueue([this] { WorkerTask(); });
		}
		pool.Wait();
		std::cout << gen_cnt_ << "\t\t" << CalcScore(raw_score_, enabled_cnt_) << "\t\t" << score_dist_.GetMean();
		std::cout << std::endl;
	}
}

std::vector<std::string> TokenGenerator::GetSolution() const {
	std::vector<std::pair<size_t, const std::string*>> to_sort;
	to_sort.reserve(enabled_.size());
	for (const Candidate *cand : enabled_) {
		to_sort.emplace_back(cand->SimulateStep(), &cand->token);
	}
	std::ranges::sort(to_sort, [](const auto &x, const auto &y) {
		return x.first == y.first ? *x.second < *y.second : x.first > y.first;
	});

	std::vector<std::string> solution;
	solution.reserve(to_sort.size() + roots_.size());
	for (const auto *token : to_sort | std::views::values) {
		solution.emplace_back(*token);
	}
	for (const auto *cand : roots_) {
		solution.emplace_back(cand->token);
	}
	return solution;
}
