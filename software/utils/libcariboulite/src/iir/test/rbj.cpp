#include "Iir.h"

#define _USE_MATH_DEFINES
#include <stdio.h>
#include <math.h>

#include "assert_print.h"

int main(int, char**)
{
	Iir::RBJ::LowPass f;
	const double samplingrate = 1000; // Hz
	const double cutoff_frequency = 5; // Hz
	const double qfactor = 1;
	f.setup(samplingrate, cutoff_frequency, qfactor);
	double b;
	for (int i = 0; i < 10000; i++)
	{
		double a = 0;
		if (i == 10) a = 1;
		b = f.filter(a);
		//fprintf(stdout,"%e\n",b);
		assert_print(!isnan(b), "Lowpass output is NAN\n");
	}
	fprintf(stderr, "%e\n", b);
	assert_print(fabs(b) < 1E-15, "Lowpass value for t->inf to high!");

	Iir::RBJ::BandStop bs;
	const double center_frequency = 0.05;
	const double frequency_width = 0.005;
	bs.setupN(center_frequency, frequency_width);
	for (int i = 0; i < 100000; i++)
	{
		double a = 0;
		if (i == 10) a = 1;
		b = bs.filter(a);
		assert_print(!isnan(b), "Bandstop output is NAN\n");
	}
	fprintf(stderr, "%e\n", b);
	assert_print(fabs(b) < 1E-15, "Bandstop value for t->inf to high!");

	for (int i = 0; i < 100000; i++)
	{
		b = fabs(bs.filter(sin(2 * M_PI*center_frequency*i)));
		if (i > 50000) {
			assert_print(b < 1E-5, "Bandstop not removing sine.");
		}
	}
	fprintf(stderr, "%e\n", b);
	return 0;
}
