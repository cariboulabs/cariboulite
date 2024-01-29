// Usage Examples
//
// This shows you how to operate the filters
//

// This is the only include you need
#include "Iir.h"

#include <stdio.h>

int main(int, char**)
{
	const int order = 8;

	// sampling rate here 1kHz as an example
	const float samplingrate = 1000;

	FILE *fimpulse = NULL;

	// Butterworth lowpass
	Iir::Butterworth::LowPass<order> f;
	double cutoff_frequency = 100; // Hz
	f.setup(samplingrate, cutoff_frequency);
	fimpulse = fopen("lp.dat", "wt");
	// let's simulated date streaming in
	for (int i = 0; i < 1000; i++)
	{
		double a = 0;
		if (i == 10) a = 1;
		double b = f.filter(a);
		fprintf(fimpulse, "%e\n", b);
	}
	fclose(fimpulse);

	// RBJ highpass filter
	// The Q factor determines the resonance at the
	// cutoff frequency. If Q=1/sqrt(2) then the transition
	// has no resonance. The higher the Q-factor the higher
	// the resonance.
	Iir::RBJ::HighPass hp;
	const double hp_cutoff_frequency = 100;
	const double hp_qfactor = 5;
	hp.setup(samplingrate, hp_cutoff_frequency, hp_qfactor);
	fimpulse = fopen("hp_rbj.dat", "wt");
	for (int i = 0; i < 1000; i++)
	{
		double a = 0;
		if (i == 10) a = 1;
		double b = hp.filter(a);
		fprintf(fimpulse, "%e\n", b);
	}
	fclose(fimpulse);

	Iir::ChebyshevI::LowPass<8> lp_cheby1;
	const float pass_ripple_db2 = 1; // dB
	lp_cheby1.setup(samplingrate,
		cutoff_frequency,
		pass_ripple_db2);
	fimpulse = fopen("lp_cheby1.dat", "wt");
	for (int i = 0; i < 1000; i++)
	{
		double a = 0;
		if (i == 10) a = 1;
		double b = lp_cheby1.filter(a);
		fprintf(fimpulse, "%e\n", b);
	}
	fclose(fimpulse);

	// Demo here if one has a pure digital
	// system one can use normalised
	// frequencies from 0 to 1/2 (Nyquist)
	// where the frequency is 1/samples.
	Iir::ChebyshevII::HighPass<order> hp_cheby2;
	double stop_ripple_dB = 60;
	// Setting cutoff to normalised f=0.1
	double normalised_cutoff_freq = 0.1;
	hp_cheby2.setupN(normalised_cutoff_freq,
		stop_ripple_dB);
	fimpulse = fopen("hp_cheby2.dat", "wt");
	for (int i = 0; i < 1000; i++)
	{
		double a = 0;
		if (i == 10) a = 1;
		double b = hp_cheby2.filter(a);
		fprintf(fimpulse, "%e\n", b);
	}
	fclose(fimpulse);

	// Bandstop filter with default order
	Iir::Butterworth::BandStop<> bsn;
	const double norm_center_frequency = 0.1;
	const double norm_frequency_width = 0.01;
	bsn.setupN(norm_center_frequency, norm_frequency_width);
	fimpulse = fopen("bs_norm.dat", "wt");
	for (int i = 0; i < 1000; i++)
	{
		double a = 0;
		if (i == 10) a = 1;
		double b = bsn.filter(a);
		fprintf(fimpulse, "%e\n", b);
	}
	fclose(fimpulse);

	// generated with: "elliptic_design.py"
	const double coeff[][6] = {
		{1.665623674062209972e-02,
		 -3.924801366970616552e-03,
		 1.665623674062210319e-02,
		 1.000000000000000000e+00,
		 -1.715403014004022175e+00,
		 8.100474793174089472e-01},
		{1.000000000000000000e+00,
		 -1.369778997100624895e+00,
		 1.000000000000000222e+00,
		 1.000000000000000000e+00,
		 -1.605878925999785656e+00,
		 9.538657786383895054e-01}
	};
	const int nSOS = sizeof(coeff) / sizeof(coeff[0]);
	Iir::Custom::SOSCascade<nSOS> cust(coeff);
	fimpulse = fopen("ellip.dat", "wt");
	for (int i = 0; i < 1000; i++)
	{
		double a = 0;
		if (i == 10) a = 1;
		double b = cust.filter(a);
		fprintf(fimpulse, "%e\n", b);
	}
	fclose(fimpulse);



	// generated with: "bessel_design.py"
	const double coeff2[][6] = {
{2.920503346420688542e-04,5.841006692841377084e-04,2.920503346420688542e-04,1.000000000000000000e+00,-1.080977903549440899e+00,2.972760361591921807e-01},
{1.000000000000000000e+00,2.000000000000000000e+00,1.000000000000000000e+00,1.000000000000000000e+00,-1.109677060197390652e+00,3.586665760324884156e-01},
{1.000000000000000000e+00,2.000000000000000000e+00,1.000000000000000000e+00,1.000000000000000000e+00,-1.179438547708672624e+00,5.264979795433866183e-01},
	};
	const int nSOS2 = sizeof(coeff2) / sizeof(coeff2[0]);
	Iir::Custom::SOSCascade<nSOS2> cust2(coeff2);
	fimpulse = fopen("bessel.dat", "wt");
	for (int i = 0; i < 1000; i++)
	{
		double a = 0;
		if (i == 10) a = 1;
		double b = cust2.filter(a);
		fprintf(fimpulse, "%e\n", b);
	}
	fclose(fimpulse);



	printf("finished!\n");
	printf("Now run `plot_impulse_fresponse.py` to display the impulse/frequency responses.\n");
}
