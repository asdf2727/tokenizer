#include "LomaxDist.h"

#include <cmath>

void LomaxDist::SetHalfLife(const double half_life) {
	alpha_ = std::log(2) / half_life;
}

void LomaxDist::SetMoments (const double moment1, const double moment2) {
	moment1_ = moment1;
	moment2_ = moment2;
}

void LomaxDist::AddPoint (double val, double weight) {
	weight *= alpha_;
	moment1_ += (val - moment1_) * weight;
	val *= val;
	moment2_ += (val - moment2_) * weight;
}

void LomaxDist::UpdateParams() {
	const double temp = moment2_ / (moment2_ - 2 * moment1_ * moment1_);
	sigma_ = moment1_ * temp;
	beta_ = temp + 1;
}

void LomaxDist::GetParams (double *beta, double *sigma) const {
	if (beta != nullptr) *beta = beta_;
	if (sigma != nullptr) *sigma = sigma_;
}

double LomaxDist::GetMean() const {
	return moment1_;
}
double LomaxDist::GetVar() const {
	return moment2_ - (moment1_ * moment1_);
}

double LomaxDist::GetPDF (const double x) const {
	return beta_ / sigma_ * std::pow(1 + x / sigma_, -(beta_ + 1));
}
double LomaxDist::GetCDF (const double x) const {
	return 1 - std::pow(1 + x / sigma_, -beta_);
}

// The integral of the inverse of the CDF from 1-p to 1
// Represents the contribution of the best p percent of entries
double LomaxDist::GetBest (const double p) const {
	const double temp = 1.0 - 1 / beta_;
	return sigma_ * (std::pow(p, temp) / temp - p);
}
