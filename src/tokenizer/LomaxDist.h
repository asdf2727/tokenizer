#pragma once
#include <atomic>

class LomaxDist {
	double alpha_ = 1;

	std::mutex moment_mutex_;
	double moment1_ = 0;
	double moment2_ = 0;

	std::atomic <double> beta_ = 1;
	std::atomic <double> sigma_ = 1;

public:
	void SetHalfLife (double half_life);

	void SetMoments (double moment1, double moment2);

	void AddPoint (double val, double weight);
	void UpdateParams();

	void GetParams (double *beta, double *sigma) const;

	[[nodiscard]] double GetMean() const;
	[[nodiscard]] double GetVar() const;

	[[nodiscard]] double GetPDF (double x) const;
	[[nodiscard]] double GetCDF (double x) const;

	[[nodiscard]] double GetBest (double p) const;
};
