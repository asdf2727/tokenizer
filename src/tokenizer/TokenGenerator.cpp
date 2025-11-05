#include "TokenGenerator.h"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <iostream>
#include <mutex>
#include <ranges>

#include "GetTokens.h"
#include "../utils/Multithread.h"

using namespace annealing;

constexpr size_t kMutexLogCount = 20;

TokenGenerator::TokenGenerator(std::vector <Token> &&tokens,
                               const size_t pref_token_count,
                               const size_t batch_size) :
	pref_cand_(pref_token_count),
	batch_size_(batch_size),
	mutexes_(1 << kMutexLogCount),
	tokens_(std::move(tokens)) {

	std::cout << "Initializing optimizer with " << tokens_.size() << " candidates..." << std::endl;

	double moment1 = 0;
	double moment2 = 0;
	size_t mutex_id = 0;
	for (Token &token : tokens_) {
		token.mutex_ = &mutexes_[mutex_id];
		mutex_id++;
		mutex_id &= (1 << kMutexLogCount) - 1;
		const size_t size = token.size();
		if (size == 1) {
			roots_.push_back(&token);
			token.enabled_ = true;
			continue;
		}
		disabled_.push_back(&token);
		double temp = token.l_branch_.uses * (size - 1);
		moment1 += temp;
		temp *= temp;
		moment2 += temp;
	}
	const_cast<size_t&>(tot_cand_) = disabled_.size();
	score_dist_.SetMoments(moment1 / tot_cand_, moment2 / tot_cand_);
	score_dist_.SetHalfLife((double)tot_cand_ * 0.5);
}

thread_local std::random_device rd;
thread_local std::mt19937 gen(rd());
thread_local std::uniform_real_distribution<> chance(0, 1);

Token* TokenGenerator::RandCandidate(std::vector<Token*> &from) {
	// TODO try to use chance_ to avoid new generator
	const size_t rand_pos = std::uniform_int_distribution<size_t>(0, from.size() - 1)(gen);
	std::swap(from[rand_pos], from.back());
	Token *ret = from.back();
	from.pop_back();
	return ret;
}

inline double TokenGenerator::CalcScore(const double raw_score, const size_t enabled_cnt) const {
	if (enabled_cnt == 0) return 0;
	const double contrib = tot_cand_ * score_dist_.GetBest((double)enabled_cnt / tot_cand_);
	const double new_pref_fill = (double)enabled_cnt / pref_cand_;
	return raw_score / contrib * new_pref_fill * (2 - new_pref_fill);
}

template <bool Enable>
std::vector<Token*> TokenGenerator::RunBatch(const size_t work_cnt, double *samples) {
	std::vector<Token*> working;
	{
		std::lock_guard lock(Enable ? disabled_mutex_ : enabled_mutex_);
		for (size_t i = 0; i < work_cnt && !(Enable ? disabled_ : enabled_).empty(); i++) {
			working.push_back(RandCandidate(Enable ? disabled_ : enabled_));
		}
	}

	for (Token *cand : working) {
		// TODO find out what causes a drop of quality when pulling these 3 lines out
		const size_t loc_enabled_cnt = enabled_cnt_;
		const double loc_raw_score = raw_score_;
		const double loc_score = CalcScore(loc_raw_score, loc_enabled_cnt);

		const double delta_raw_score = cand->SimulateStep();
		const double new_score = CalcScore(loc_raw_score + (Enable ? delta_raw_score : -delta_raw_score),
		                                   loc_enabled_cnt + (Enable ? 1 : -1));

		// TODO try move with probability k (default annealing) or k / 1+k (correct Boltzmann dist)
		//const bool do_step = new_score > loc_score || chance(gen) < std::exp((new_score - loc_score) / temp_);
		const bool do_step = chance(gen) > 1 / (1 + std::exp((new_score - loc_score) / temp_));

		// TODO consider updating delta_score with actual ApplyStep
		if (do_step) {
			raw_score_ += Enable ? cand->ApplyStep<true>() : -cand->ApplyStep<false>();
			enabled_cnt_ += Enable ? 1 : -1;
		}

		*samples++ = delta_raw_score;
	}

	return working;
}

void TokenGenerator::WorkerTask() {
	// Calculate the chance of enabling a Token, based on x (current enabled cnt) and P (preferred enabled cnt)
	// I did this to combat the tendency of entropy to enable half of the candidates, completely messing up my score function
	const uint64_t enabled_weight = enabled_cnt_ * (tot_cand_ - pref_cand_);
	const uint64_t disabled_weight = (tot_cand_ - enabled_cnt_) * pref_cand_;
	const double total_weight = enabled_weight + disabled_weight;
	const double corr_enable = total_weight / (tot_cand_ * pref_cand_);
	const double corr_disable = total_weight / (tot_cand_ * (tot_cand_ - pref_cand_));
	size_t enable_cnt = std::binomial_distribution(batch_size_, (double)disabled_weight / total_weight)(gen);
	if (enabled_cnt_ < batch_size_ - enable_cnt) enable_cnt = batch_size_ - enabled_cnt_;
	enable_cnt = std::min(enable_cnt, tot_cand_ - enabled_cnt_);

	temp_ = 0.003 * std::exp(-(double)gen_cnt_ / tot_cand_ * 0.1);
	//temp_ = 1e-20;

	std::vector<Token*> enabled;
	std::vector<Token*> disabled;
	double samples[batch_size_];
	if (enable_cnt > 0)
		for (Token *cand : RunBatch<true>(enable_cnt, samples)) {
			if (cand->enabled_) enabled.push_back(cand);
			else disabled.push_back(cand);
		}
	if (enable_cnt < batch_size_)
		for (Token *cand : RunBatch<false>(batch_size_ - enable_cnt, samples + enable_cnt)) {
			if (cand->enabled_) enabled.push_back(cand);
			else disabled.push_back(cand);
		}

	{
		size_t i = 0;
		std::lock_guard lock(dist_mutex_);
		while (i < enable_cnt) {
			score_dist_.AddPoint(samples[i++], corr_enable);
		}
		while (i < batch_size_) {
			score_dist_.AddPoint(samples[i++], corr_disable);
		}
		score_dist_.UpdateParams();
	}

	if (!enabled.empty()) {
		std::lock_guard lock(enabled_mutex_);
		enabled_.insert(enabled_.end(), enabled.begin(), enabled.end());
	}
	if (!disabled.empty()) {
		std::lock_guard lock(disabled_mutex_);
		disabled_.insert(disabled_.end(), disabled.begin(), disabled.end());
	}

	gen_cnt_ += batch_size_;
}

bool stdin_has_data()
{
	fd_set set;
	timeval timeout;
	FD_ZERO(&set);
	FD_SET(STDIN_FILENO, &set); // STDIN_FILENO is usually 0

	timeout.tv_sec = 0;
	timeout.tv_usec = 0; // 0 means non-blocking (poll)

	int rv = select(STDIN_FILENO + 1, &set, nullptr, nullptr, &timeout);
	return (rv > 0 && FD_ISSET(STDIN_FILENO, &set));
}

void TokenGenerator::Generate(const size_t pass_cnt) {
	std::cout << "Running simulated annealing";
	std::cout << ( pass_cnt == -1 ? "" : "for " + std::to_string(pass_cnt * tot_cand_) + " steps") << std::endl;
	ThreadPool pool(std::min((uint64_t)std::thread::hardware_concurrency(), tot_cand_ / batch_size_));
	for (int pass = 0; pass <= pass_cnt; pass++) {
		// TODO avoid master thread hogging queue mutex to allow others to start running
		for (int i = 0; i * batch_size_ < tot_cand_; i++) {
			pool.Enqueue([this] { WorkerTask(); });
		}
		pool.Wait();
		std::cout << gen_cnt_ << "\t\t" << CalcScore(raw_score_, enabled_cnt_) << "\t\t";
		std::cout << enabled_cnt_ << "\t\t" << temp_ << '\n';
		if (stdin_has_data()) break;
	}
}

std::vector<std::string> TokenGenerator::GetSolution() const {
	std::vector<std::pair<size_t, std::string>> to_sort;
	to_sort.reserve(enabled_.size());
	for (const Token *cand : enabled_) {
		to_sort.emplace_back(cand->SimulateStep(), cand->GetName());
	}
	std::ranges::sort(to_sort, [](const auto &x, const auto &y) {
		return x.first == y.first ? x.second < y.second : x.first > y.first;
	});

	{
		// TEMPORARY
		std::ofstream out("solution.txt");
		for (const auto &x : to_sort) {
			out << x.first << "\t" << x.second << '\n';
		}
	}

	std::vector<std::string> solution;
	solution.reserve(to_sort.size() + roots_.size());
	for (auto &&token : to_sort | std::views::values) {
		solution.emplace_back(std::move(token));
	}
	for (const auto *cand : roots_) {
		solution.emplace_back(cand->GetName());
	}
	return solution;
}
