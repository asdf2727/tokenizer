#include "TokenGenerator.h"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <iostream>
#include <mutex>
#include <ranges>

TokenGenerator::TokenGenerator(std::unordered_map<std::string, size_t> &&cands, const size_t pref_token_count) :
	pref_cand_(pref_token_count) {
	std::string str(1, '\0');
	for (int i = 0; i < UINT8_MAX; i++) {
		str[0] = (char)i;
		if (cands.contains(str)) continue;
		cands[str] = 0;
	}
	tot_cand_ = cands.size();
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


size_t TokenGenerator::Candidate::SimulateStep() const {
	size_t delta_len = 1;
	for (const Candidate *par = parent; !par->enabled; par = par->parent) delta_len++;
	return uses * delta_len;
}

template <bool Enable>
size_t TokenGenerator::Candidate::ApplyStep() {
	size_t delta_len = 1;
	Candidate *node;
	for (node = parent; !node->enabled; node = node->parent) {
		node->uses -= (Enable ? 1 : -1) * uses;
		delta_len++;
	}
	node->uses -= (Enable ? 1 : -1) * uses;
	enabled = Enable;
	return delta_len * uses;
}

bool TokenGenerator::WillDisable(double &corr_factor) {
	// Calculate the chance of enabling a candidate, based on x (current enabled cnt) and P (preferred enabled cnt)
	// The formula is P(x) = x / [x + (n-x) * p/(n-p)], but I rearranged it to remove floating point arithmetic
	// I did this to combat the tendency of entropy to enable half of the candidates, completely messing up my score function
	const size_t enabled_weight = enabled_.size() * (tot_cand_ - pref_cand_);
	const size_t total_weight = enabled_weight + disabled_.size() * pref_cand_;
	const bool swap_enabled = std::uniform_int_distribution<size_t>(0, total_weight - 1)(gen_) < enabled_weight;
	corr_factor = (double)total_weight / (tot_cand_ * (swap_enabled ? tot_cand_ - pref_cand_ : pref_cand_));
	return swap_enabled;
}

template <bool Enable>
TokenGenerator::Candidate* TokenGenerator::RandCandidate() {
	std::vector<Candidate*> &from = Enable ? disabled_ : enabled_;
	std::lock_guard lock(Enable ? disabled_mutex_ : enabled_mutex_);
	// TODO try to use chance_ to avoid new generator
	const size_t rand_pos = std::uniform_int_distribution<size_t>(0, from.size() - 1)(gen_);
	std::swap(from[rand_pos], from.back());
	Candidate *cand = from.back();
	from.pop_back();
	return cand;
}

inline double TokenGenerator::CalcScore(const size_t raw_score, const size_t enabled_cnt) const {
	const double contrib = tot_cand_ * score_dist_.GetBest((double)enabled_cnt / tot_cand_);
	const double new_pref_fill = (double)enabled_cnt / pref_cand_;
	return (double)raw_score / contrib * new_pref_fill * (2 - new_pref_fill);
}

template <bool Enable>
void TokenGenerator::TryAndStep(const double corr_factor) {
	// Extract random candidate
	Candidate *cand = RandCandidate<Enable>();

	std::unique_lock s_lock(score_mutex_);
	const size_t loc_enabled_cnt = enabled_cnt_;
	const size_t loc_raw_score = raw_score_;
	const double loc_score = score_; // TODO see if it's faster to recompute score_ every time
	s_lock.unlock();

	// Simulate switching
	const size_t delta_raw_score = cand->SimulateStep();
	const double new_score = CalcScore(loc_raw_score + (Enable ? delta_raw_score : -delta_raw_score),
	                                   loc_enabled_cnt + (Enable ? 1 : -1));
	// TODO Warning: estimation assumes weight << 1; do more meth to figure out the exact formula
	score_dist_.AddPoint(delta_raw_score, corr_factor);

	// Choose whether to keep change
	temp_ = 0.001 * std::exp(-(double)gen_cnt_ / tot_cand_ * 0.3);
	// TODO try move with probability k (default annealing or k / 1+k (correct Boltzmann dist)
	if (chance_(gen_) > 1 / (1 + std::exp((new_score - loc_score) / temp_))) {
		s_lock.lock();
		raw_score_ += Enable ? cand->ApplyStep<true>() : -cand->ApplyStep<false>();
		enabled_cnt_ += Enable ? 1 : -1;
		score_ = CalcScore(raw_score_, enabled_cnt_);
		s_lock.unlock();
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
}

void TokenGenerator::RunStep() {
	double corr_factor;
	const bool disable = WillDisable(corr_factor);
	disable ? TryAndStep<false>(corr_factor) : TryAndStep<true>(corr_factor);
	++gen_cnt_;
}

void TokenGenerator::Generate() {
	size_t steps = 30 * tot_cand_;
	std::cout << "Running simulated annealing for " << steps << " steps" << std::endl;
	while (steps--) {
		RunStep();
		if ((gen_cnt_ + 1) % tot_cand_ == 0) {
			std::cout << gen_cnt_ << "\t\t" << score_ << "\t\t";
			std::cout << temp_ << "\t\t" << enabled_.size() << "\t\t" << debug_;
			debug_ = 0;
			std::cout << std::endl;
		}
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
