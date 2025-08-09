#pragma once

class LomaxDist {
	double alpha_ = 1;
	double moment1_ = 0;
	double moment2_ = 0;

	double beta_ = 1;
	double sigma_ = 1;

public:
	LomaxDist () = default;

	void SetHalfLife (double halflife);
	void AddPoint (double val, double weight);

	void UpdateParams ();
	void GetParams (double *beta, double *sigma) const;

	[[nodiscard]] double GetPDF (double x) const;
	[[nodiscard]] double GetCDF (double x) const;

	[[nodiscard]] double GetBest (double p) const;
};
