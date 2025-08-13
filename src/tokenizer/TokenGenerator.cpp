#include "TokenGenerator.h"

#include <iostream>
#include <cmath>

TokenGenerator::TokenGenerator(const std::unordered_map <std::string, size_t> &cands, const size_t pref_token_count):
	pref_cand_(pref_token_count) {
	char temp[2] = {'\0', '\0'};
	for (int i = 0; i < 256; i++) {
		temp[0] = i;
		uses_[temp].enabled = true;
	}
	std::cout << "Initializing " << cands.size() << " candidates... " << std::flush;
	for (const auto &[token, freq] : cands) {
		uses_[token].uses = freq;
		if (token.size() == 1) continue;
		disabled_.emplace_back(token);
		tot_cand_++;
	}
	std::cout << "Done\nInitializing parents... " << std::flush;
	score_dist_.SetHalfLife((double)tot_cand_ / 4);
	for (auto &[token, usage] : uses_) {
		if (token.size() == 1) continue;
		score_dist_.AddPoint(usage.uses * (token.size() - 1), 1);
		usage.parent = &uses_[token.substr(0, token.size() - 1)];
	}
	score_dist_.SetHalfLife((double)tot_cand_);
	std::cout << "Done" << std::endl;
}

template <bool Enable>
void TokenGenerator::RunStep (const double corr_factor) {
	constexpr long int delta_enabled = Enable ? 1 : -1;
	std::vector <std::string> &from = Enable ? disabled_ : enabled_;
	std::vector <std::string> &to = Enable ? enabled_ : disabled_;

	const size_t cand_index = std::uniform_int_distribution <size_t> (0, from.size() - 1)(gen_);
	auto &[uses, parent, enabled] = uses_[from[cand_index]];

	size_t delta_len = 1;
	for (const Usage *par = parent; !par->enabled; par = par->parent) { delta_len++; }
	const size_t new_raw_score = (raw_score_ + delta_enabled * delta_len * uses);

	// TODO Warning: estimation assumes weight << 1; do more meth to figure out the exact formula
	score_dist_.AddPoint(delta_len * uses, corr_factor);
	score_dist_.UpdateParams();

	const double fill_ratio = (double)(enabled_.size() + delta_enabled) / pref_cand_;
	const double contrib = tot_cand_ * score_dist_.GetBest((double)(enabled_.size() + delta_enabled) / tot_cand_);
	const double new_score = new_raw_score / contrib * fill_ratio * (2 - fill_ratio);
	const double temp = 0.0005 * std::exp(-(double)gen_cnt_ / tot_cand_ * 0.4);
	const double keep_prob = std::exp((new_score - score_) / temp);
	const bool keep_change = chance_(gen_) < keep_prob;

	if (keep_change) {
		std::swap(from[cand_index], from.back());
		to.push_back(std::move(from.back()));
		from.pop_back();
		enabled = Enable;

		for (Usage *par = parent; par != nullptr; par = par->parent) {
			par->uses -= delta_enabled * uses;
			if (par->enabled) break;
		}

		score_ = new_score;
		raw_score_ = new_raw_score;
	}

	if (gen_cnt_ % tot_cand_ == 0) {
		double beta, sigma;
		score_dist_.GetParams(&beta, &sigma);
		std::cout << gen_cnt_ << "\t\t" << temp << "\t\t" << enabled_.size() << "\t\t";
		std::cout << score_ << "\t\t" << beta << "\t\t" << sigma << std::endl;
	}
}

void TokenGenerator::Generate() {
	size_t steps = 30 * tot_cand_;
	std::cout << "Running simulated annealing for " << steps << " steps" << std::endl;
	while (steps--) {
		// Calculate the chance of enabling a candidate, based on x (current enabled cnt) and P (preferred enabled cnt)
		// The formula is P(x) = x / [x + (n-x) * p/(n-p)], but I rearranged it to remove floating point arithmetic
		// I did this to combat the tendency of entropy to enable half of the candidates, completely messing up my score function
		const size_t enabled_weight = enabled_.size() * (tot_cand_ - pref_cand_);
		const size_t total_weight = enabled_.size() * (tot_cand_ - pref_cand_) + disabled_.size() * pref_cand_;
		const bool hit_enabled = std::uniform_int_distribution <size_t> (0, total_weight - 1)(gen_) < enabled_weight;
		const double avg_weight = (double)total_weight / tot_cand_;
		if (hit_enabled) {
			RunStep <false> (avg_weight / (tot_cand_ - pref_cand_));
		}
		else {
			RunStep <true> (avg_weight / pref_cand_);
		}
		gen_cnt_++;
	}
}

std::vector <std::string> TokenGenerator::GetSolution() const {
	std::vector solution(enabled_);
	char temp[2] = {'\0', '\0'};
	for (int i = 0; i < 256; i++) {
		temp[0] = i;
		solution.emplace_back(temp);
	}
	return solution;
}
