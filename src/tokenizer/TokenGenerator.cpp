#include "TokenGenerator.h"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <iostream>
#include <mutex>
#include <ranges>

#include "../files/CandidatesFile.h"
#include "../utils/Multithread.h"

TokenGenerator::TokenGenerator(std::vector <std::pair <std::string, size_t>> &&cands,
                               const size_t pref_token_count,
                               const size_t batch_size) :
	pref_cand_(pref_token_count),
	batch_size_(batch_size) {

	std::cout << "Initializing optimizer with " << cands.size() << " candidates..." << std::endl;
	std::unordered_map<std::string, Candidate*> candidates;
	std::vector <std::vector <Candidate *>> raw_cands;
	{
		raw_cands.resize(std::thread::hardware_concurrency());
		while (!cands.empty()) {
			auto &[name, freq] = cands.back();
			cands.pop_back();
			auto *cand = new Candidate(name, freq);

			candidates[name] = cand;
			if (name.size() == 1) {
				cand->enabled = true;
				roots_.push_back(cand);
			}
			else {
				raw_cands[disabled_.size() % 20].push_back(cand);
				disabled_.push_back(cand);
			}
		}
	}

	*(size_t*)&tot_cand_ = disabled_.size();
	std::string str(1, '\0');
	for (int i = 0; i < 0x100; i++) {
		str[0] = (char)i;
		if (!candidates.contains(str)) roots_.push_back(new Candidate(str, 0));
	}

	std::cout << "Computing parents..." << std::endl;
	std::atomic <double> moment1 = 0;
	std::atomic <double> moment2 = 0;
	ThreadPool pool;
	for (auto &vec : raw_cands) {
		pool.Enqueue([&vec, &moment1, &moment2, &candidates] {
			for (auto *cand : vec) {
				double temp = cand->l_branch.uses * (cand->token.size() - 1);
				moment1 += temp;
				temp *= temp;
				moment2 += temp;
				cand->l_branch.parent = candidates[cand->token.substr(0, cand->token.size() - 1)];
				cand->r_branch.parent = candidates[cand->token.substr(1)];
			}
			vec.clear();
		});
	}
	pool.Wait();

	score_dist_.SetMoments(moment1 / tot_cand_, moment2 / tot_cand_);
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

#define BRANCH(LeftBranch) (LeftBranch ? node->l_branch : node->r_branch)

template <bool LeftBranch>
double TokenGenerator::Candidate::Branch::SimulateStep() const {
	int64_t delta_len = 1;
	for (const Candidate *node = parent; !node->enabled; node = BRANCH(LeftBranch).parent) delta_len++;
	return delta_len * uses;
}

template <bool Enable, bool LeftBranch>
double TokenGenerator::Candidate::Branch::ApplyStep(const size_t saved_uses) const {
	int64_t delta_len = 1;
	for (Candidate *node = parent; true; node = BRANCH(LeftBranch).parent) {
		std::lock_guard node_lock(node->mutex);
		BRANCH(LeftBranch).uses -= (Enable ? 1 : -1) * saved_uses;
		if (node->enabled) break;
		delta_len++;
	}
	return delta_len * saved_uses;
}

#undef BRANCH

double TokenGenerator::Candidate::SimulateStep() const {
	double score = 0;
	score += l_branch.SimulateStep<true>();
	//score += r_branch.SimulateStep<false>();
	return score;
}

template <bool Enable>
double TokenGenerator::Candidate::ApplyStep() {
	uint64_t loc_l_uses, loc_r_uses;
	{
		std::lock_guard my_lock(mutex);
		enabled = Enable;
		loc_l_uses = l_branch.uses;
		//loc_r_uses = r_branch.uses;
	}
	double score = 0;
	score += l_branch.ApplyStep<Enable, true>(loc_l_uses);
	//score += r_branch.ApplyStep<Enable, false>(loc_r_uses);
	return score;
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

inline double TokenGenerator::CalcScore(const double raw_score, const size_t enabled_cnt) const {
	if (enabled_cnt == 0) return 0;
	const double contrib = tot_cand_ * score_dist_.GetBest((double)enabled_cnt / tot_cand_);
	const double new_pref_fill = (double)enabled_cnt / pref_cand_;
	return raw_score / contrib * new_pref_fill * (2 - new_pref_fill);
}

template <bool Enable>
std::vector<TokenGenerator::Candidate*> TokenGenerator::RunBatch(const size_t work_cnt, double *samples) {
	std::vector<Candidate*> working;
	{
		std::lock_guard lock(Enable ? disabled_mutex_ : enabled_mutex_);
		for (size_t i = 0; i < work_cnt; i++) {
			working.push_back(RandCandidate(Enable ? disabled_ : enabled_));
		}
	}

	for (Candidate *cand : working) {
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
	// Calculate the chance of enabling a candidate, based on x (current enabled cnt) and P (preferred enabled cnt)
	// I did this to combat the tendency of entropy to enable half of the candidates, completely messing up my score function
	const uint64_t enabled_weight = enabled_cnt_ * (tot_cand_ - pref_cand_);
	const uint64_t disabled_weight = (tot_cand_ - enabled_cnt_) * pref_cand_;
	const double total_weight = enabled_weight + disabled_weight;
	const double corr_enable = total_weight / (tot_cand_ * pref_cand_);
	const double corr_disable = total_weight / (tot_cand_ * (tot_cand_ - pref_cand_));
	size_t enable_cnt = std::binomial_distribution(batch_size_, (double)disabled_weight / total_weight)(gen);
	if (enabled_cnt_ < batch_size_) enable_cnt = batch_size_;
	if (tot_cand_ - enabled_cnt_ < batch_size_) enable_cnt = batch_size_ + enabled_cnt_ - tot_cand_;

	temp_ = 0.001 * std::exp(-(double)gen_cnt_ / tot_cand_ * 0.1);
	//temp_ = 1e-20;

	std::vector<Candidate*> enabled;
	std::vector<Candidate*> disabled;
	double samples[batch_size_];
	if (enable_cnt > 0)
		for (Candidate *cand : RunBatch<true>(enable_cnt, samples)) {
			if (cand->enabled) enabled.push_back(cand);
			else disabled.push_back(cand);
		}
	if (enable_cnt < batch_size_)
		for (Candidate *cand : RunBatch<false>(batch_size_ - enable_cnt, samples + enable_cnt)) {
			if (cand->enabled) enabled.push_back(cand);
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
	for (int pass = 0; pass < pass_cnt || pass_cnt == -1; pass++) {
		// TODO avoid master thread hogging queue mutex to allow others to start running
		for (int i = 0; i * batch_size_ < tot_cand_; i++) {
			pool.Enqueue([this] { WorkerTask(); });
		}
		pool.Wait();
		std::cout << gen_cnt_ << "\t\t" << CalcScore(raw_score_, enabled_cnt_) << "\t\t";
		std::cout << enabled_cnt_ << "\t\t" << temp_ << '\n';
		if (stdin_has_data()) break;
	}
	std::cin.ignore();
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

	{
		std::ofstream out("solution.txt");
		for (const auto &x : to_sort) {
			out << x.first << "\t" << *x.second << '\n';
		}
	}

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
