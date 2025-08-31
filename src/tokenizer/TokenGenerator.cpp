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
	// TODO add best candidates as initial solution
	score_dist_.SetHalfLife((double)tot_cand_ * 0.5);
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

std::pair <bool, double> TokenGenerator::WillDisable() const {
	// Calculate the chance of enabling a candidate, based on x (current enabled cnt) and P (preferred enabled cnt)
	// The formula is P(x) = x / [x + (n-x) * p/(n-p)], but I rearranged it to remove floating point arithmetic
	// I did this to combat the tendency of entropy to enable half of the candidates, completely messing up my score function
	const uint64_t enabled_weight = enabled_.size() * (tot_cand_ - pref_cand_);
	const uint64_t total_weight = enabled_weight + disabled_.size() * pref_cand_;
	const bool swap_enabled = std::uniform_int_distribution<size_t>(0, total_weight - 1)(gen) < enabled_weight;
	const double corr_factor = (double)total_weight / (tot_cand_ * (swap_enabled ? tot_cand_ - pref_cand_ : pref_cand_));
	return {swap_enabled, corr_factor};
}

template <bool Enable>
TokenGenerator::Candidate* TokenGenerator::RandCandidate() {
	std::vector<Candidate*> &from = Enable ? disabled_ : enabled_;
	std::lock_guard lock(Enable ? disabled_mutex_ : enabled_mutex_);
	// TODO try to use chance_ to avoid new generator
	const size_t rand_pos = std::uniform_int_distribution<size_t>(0, from.size() - 1)(gen);
	std::swap(from[rand_pos], from.back());
	Candidate *cand = from.back();
	from.pop_back();
	return cand;
}

inline double TokenGenerator::CalcScore(const size_t raw_score, const size_t enabled_cnt) const {
	if (enabled_cnt == 0) return 0;
	const double contrib = tot_cand_ * score_dist_.GetBest((double)enabled_cnt / tot_cand_);
	const double new_pref_fill = (double)enabled_cnt / pref_cand_;
	return (double)raw_score / contrib * new_pref_fill * (2 - new_pref_fill);
}

template <bool Enable>
size_t TokenGenerator::TryAndStep() {
	// Extract random candidate
	Candidate *cand = RandCandidate<Enable>();

	const size_t loc_enabled_cnt = enabled_cnt_;
	const uint64_t loc_raw_score = raw_score_;
	// TODO see if it's faster to recompute loc_score every time or read it from shared memory
	const double loc_score = CalcScore(loc_raw_score, loc_enabled_cnt);

	// Simulate switching
	const int64_t delta_raw_score = cand->SimulateStep();
	const double new_score = CalcScore(loc_raw_score + (Enable ? delta_raw_score : -delta_raw_score),
	                                   loc_enabled_cnt + (Enable ? 1 : -1));

	// Choose whether to keep change
	temp_ = 0.0005 * std::exp(-(double)gen_cnt_ / tot_cand_ * 0.4);
	// TODO try move with probability k (default annealing) or k / 1+k (correct Boltzmann dist)
	//const bool do_step = new_score > loc_score || chance(gen) < std::exp((new_score - loc_score) / temp_);
	const bool do_step = chance(gen) > 1 / (1 + std::exp((new_score - loc_score) / temp_));
	if (do_step) {
		raw_score_ += Enable ? cand->ApplyStep<true>() : -cand->ApplyStep<false>();
		enabled_cnt_ += Enable ? 1 : -1;
	}

	// Add back the candidate to the correct pool
	if (cand->enabled) {
		std::lock_guard lock(enabled_mutex_);
		enabled_.push_back(cand);
	}
	else {
		std::lock_guard lock(disabled_mutex_);
		disabled_.push_back(cand);
	}

	return delta_raw_score;
}

void TokenGenerator::RunStep() {
	const auto [disable, corr_factor] = WillDisable();
	// TODO Warning: estimation assumes weight << 1; do more meth to figure out the exact formula
	score_dist_.AddPoint(disable ? TryAndStep<false>() : TryAndStep<true>(), corr_factor);
	++gen_cnt_;
}

void TokenGenerator::Generate() {
	std::cout << "Running simulated annealing for " << 30 * tot_cand_ << " steps" << std::endl;
	ThreadPool pool;
	for (int pass = 0; pass < 30; pass++) {
		for (int i = 0; i < tot_cand_; i++) {
			pool.Enqueue([this] { RunStep(); });
		}
		pool.Wait();
		std::cout << gen_cnt_ << "\t\t" << CalcScore(raw_score_, enabled_cnt_) << "\t\t" << temp_ << "\t\t";
		std::cout << raw_score_ << "\t\t" << enabled_cnt_ << "\t\t";
		std::cout << enabled_.size() << "\t\t" << debug_;
		std::cout << std::endl;
		debug_ = 0;
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
