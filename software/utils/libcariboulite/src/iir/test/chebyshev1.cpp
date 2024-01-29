#include "Iir.h"

#define _USE_MATH_DEFINES
#include <stdio.h>
#include <math.h>

#include "assert_print.h"

int main(int, char**)
{
	const int order = 3;
	Iir::ChebyshevI::HighPass<order> f;
	const float samplingrate = 1000; // Hz
	const float cutoff_frequency = 5; // Hz
	f.setup(samplingrate, cutoff_frequency, 1);
	double b;
	for (int i = 0; i < 100000; i++)
	{
		float a = 0;
		if (i == 10) a = 1;
		b = f.filter(a);
		assert_print(!isnan(b), "Highpass output is NAN\n");
	}
	//fprintf(stderr,"%e\n",b);
	assert_print(fabs(b) < 1E-15, "Highpass value for t->inf to high!");

	Iir::ChebyshevI::BandStop<4, Iir::DirectFormI> bs;
	const float center_frequency = 50;
	const float frequency_width = 5;
	bs.setup(samplingrate, center_frequency, frequency_width, 1);
	bs.reset();
	for (int i = 0; i < 100000; i++)
	{
		float a = 0;
		if (i == 10) a = 1;
		b = bs.filter(a);
		assert_print(!isnan(b), "Bandstop output is NAN\n");
		//fprintf(stderr,"%e\n",b);
	}
	//fprintf(stderr,"%e\n",b);
	assert_print(fabs(b) < 1E-15, "Bandstop value for t->inf to high!");

	Iir::ChebyshevI::LowPass<8> lp_cheby1;
	const float pass_ripple_db2 = 1; // dB
	double norm_cutoff_frequency = 0.01;
	lp_cheby1.setupN(norm_cutoff_frequency,
		pass_ripple_db2);
	double norm_signal_frequency = 0.097;
	for (int i = 0; i < 10000; i++)
	{
		b = fabs(lp_cheby1.filter(sin(2 * M_PI*norm_signal_frequency*i)));
		if (i > 5000) {
			assert_print(b < 1E-5, "Lowpass not removing a sine.\n");
		}
	}

	return 0;


}
