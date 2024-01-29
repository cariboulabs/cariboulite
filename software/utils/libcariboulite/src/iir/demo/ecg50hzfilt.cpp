// Usage Examples
//
#include "Iir.h"

#include <stdio.h>

int main (int,char**)
{
	const float fs = 1000;
        const float mains = 50;
	Iir::RBJ::IIRNotch iirnotch;
	iirnotch.setup(fs,mains);

        const float ecg_max_f = 100;
	Iir::Butterworth::LowPass<4> lp;
	lp.setup(fs,ecg_max_f);

	FILE *finput = fopen("ecg50hz.dat","rt");
	FILE *foutput = fopen("ecg_filtered.dat","wt");
	// let's simulate incoming streaming data
	for(;;) 
	{
		float a;
		if (fscanf(finput,"%f\n",&a)<1) break;
		a = a - 2250;
		a = lp.filter(a);
		a = iirnotch.filter(a);
		fprintf(foutput,"%f\n",a);
	}
	fclose(finput);
	fclose(foutput);
	fprintf(stderr,"Written the filtered ECG to 'ecg_filtered.dat'\n");
}
