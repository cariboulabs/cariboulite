#include "Iir.h"

#include <stdio.h>

#include "assert_print.h"

int main (int,char**)
{
	// dummy filter parameters
	const float samplingrate = 1000;
	const float bw = 5;

	try {
		Iir::Butterworth::LowPass<3> f;
		f.setup (samplingrate, samplingrate / 2);
		assert_print(0,"No exception thrown by LowPass.");
	} catch ( const std::invalid_argument& e ) {
		fprintf(stderr,"Correct lowpass exception thrown for fc = fs/2: %s\n",e.what());
	}
	
	try {
		Iir::Butterworth::HighPass<3> f;
		f.setup (samplingrate, samplingrate / 2);
		assert_print(0,"No exception thrown by HighPass.");
	} catch ( const std::invalid_argument& e ) {
		fprintf(stderr,"Correct highpass exception thrown for fc = fs/2: %s\n",e.what());
	}
	
	try {
		Iir::Butterworth::BandStop<3> f;
		f.setup (samplingrate, samplingrate / 2, bw);
		assert_print(0,"No exception thrown by BandStop.");
	} catch ( const std::invalid_argument& e ) {
		fprintf(stderr,"Correct bandstop exception thrown for fc = fs/2: %s\n",e.what());
	}
	
	try {
		Iir::Butterworth::BandPass<3> f;
		f.setup (samplingrate, samplingrate / 2, bw);
		assert_print(0,"No exception thrown by BandPass.");
	} catch ( const std::invalid_argument& e ) {
		fprintf(stderr,"Correct bandpass exception thrown for fc = fs/2: %s\n",e.what());
	}
	
	return 0;
}
