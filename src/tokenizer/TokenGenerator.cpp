#include "TokenGenerator.h"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <iostream>
#include <ranges>

TokenGenerator::TokenGenerator(std::unordered_map <std::string, size_t> &&cands, const size_t pref_token_count):
	pref_cand_(pref_token_count) {
	char temp[2] = {'\0', '\0'};
	for (int i = 0; i < 256; i++) {
		temp[0] = i;
		uses_[temp].enabled = true;
	}
	for (const auto &[token, freq] : cands) {
		uses_[token].uses = freq;
		if (token.size() == 1) continue;
		disabled_.emplace_back(token);
		tot_cand_++;
	}
	score_dist_.SetHalfLife((double)tot_cand_ / 4);
	for (auto &[token, usage] : uses_) {
		if (token.size() == 1) continue;
		score_dist_.AddPoint(usage.uses * (token.size() - 1), 1);
		usage.parent = &uses_[token.substr(0, token.size() - 1)];
	}
	score_dist_.SetHalfLife((double)tot_cand_);
}
TokenGenerator::~TokenGenerator() {
	for (const Candidate *cand : enabled_) {
		delete cand;
	}
	for (const Candidate *cand : disabled_) {
		delete cand;
	}
}


size_t TokenGenerator::SimulateStep(const size_t uses, const Candidate *parent) const {
	size_t delta_len = 1;
	for (const Candidate *par = parent; !par->enabled; par = par->parent) delta_len++;
	return uses * delta_len;
}
template <bool Enable>
void TokenGenerator::ApplyStep(const std::string *cand, Candidate &usage) {
	// TODO usage mutex here
	for (Candidate *par = usage.parent; par != nullptr; par = par->parent) {
		Enable ? par->uses -= usage.uses : par->uses += usage.uses;
		Enable ? raw_score_ += usage.uses : raw_score_ -= usage.uses;
		if (par->enabled) break;
	}
}

template <bool Enable>
double TokenGenerator::CalcScore(const size_t raw_score) const {
	const double new_enabled_cnt = enabled_.size() + (Enable ? 1 : -1);
	const double contrib = tot_cand_ * score_dist_.GetBest(new_enabled_cnt / tot_cand_);
	const double new_pref_fill = new_enabled_cnt / pref_cand_;
	return raw_score / contrib * new_pref_fill * (2 - new_pref_fill);
}

template <bool Enable>
void TokenGenerator::RunStep (const double corr_factor) {
	std::vector <const std::string*> &from = Enable ? disabled_ : enabled_;
	std::vector <const std::string*> &to = Enable ? enabled_ : disabled_;

	// Choose candidate
	const size_t cand_index = std::uniform_int_distribution <size_t> (0, from.size() - 1)(gen_);
	const std::string* cand;
	{
		// TODO from mutex here
		std::swap(from[cand_index], from.back());
		cand = from.back();
		from.pop_back();
	}

	// Get usage
	const auto [uses, parent, enabled] = uses_[*cand];

	// Simulate switching
	const size_t delta_raw_score = SimulateStep(uses, parent);
	const size_t new_raw_score = Enable ? raw_score_ + delta_raw_score : raw_score_ - delta_raw_score;
	const double new_score = CalcScore<Enable>(new_raw_score, corr_factor);

	// Add to score distribution
	{
		// TODO add score mutex here???
		// TODO Warning: estimation assumes weight << 1; do more meth to figure out the exact formula
		score_dist_.AddPoint(delta_raw_score, corr_factor);
		score_dist_.UpdateParams();
	}

	// Choose whether to keep change
	const double temp = 0.0005 * std::exp(-(double)gen_cnt_ / tot_cand_ * 0.4);
	const bool keep_change = new_score > score_ || chance_(gen_) < std::exp((new_score - score_) / temp);
	if (keep_change) {
		ApplyStep <Enable>(uses, parent);
		enabled = Enable;
	}
	if (usage.enabled) {
		// TODO disabled mutex here
		disabled_.push_back(cand);
	}
	else {
		// TODO enabled mutex here
		enabled_.push_back(cand);
	}

	if (gen_cnt_ % tot_cand_ == 0) {
		double beta, sigma;
		score_dist_.GetParams(&beta, &sigma);
		std::cout << gen_cnt_ << "\t\t" << score_ << '\n';
		std::cout << '\t' << temp << "\t\t" << enabled_.size() << '\n';
		//std::cout << '\t' << beta << "\t\t" << sigma << '\n';
		std::cout.flush();
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
	std::vector <std::pair <size_t, std::string>> to_sort;
	to_sort.reserve(enabled_.size() + 256);
	for (const std::string &str : enabled_) {
		auto it = uses_.find(str);
		if (it == uses_.end()) throw std::runtime_error("Can't find uses for " + str);
		const auto &[uses, parent, enabled] = it->second;

		size_t delta_len = 1;
		for (const Candidate *par = parent; !par->enabled; par = par->parent) delta_len++;
		to_sort.emplace_back(delta_len * uses, str);
	}
	char temp[2] = {'\0', '\0'};
	for (int i = 0; i < 256; i++) {
		temp[0] = i;
		auto it = uses_.find(temp);
		to_sort.emplace_back(it == uses_.end() ? 0 : it->second.uses, temp);
	}
	std::ranges::sort(to_sort, [](const auto &x, const auto &y) {
		return x.first == y.first ? x.second < y.second : x.first > y.first;
	});

	std::vector <std::string> solution;
	std::ofstream fout("temp.txt");
	solution.reserve(to_sort.size());
	for (const auto &[score, token] : to_sort) {
		fout << token << ' ' << score << '\n';
		solution.push_back(token);
	}
	fout.flush();
	return solution;
}
