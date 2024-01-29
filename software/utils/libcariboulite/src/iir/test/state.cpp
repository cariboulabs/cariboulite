#include "Iir.h"

#include <stdio.h>
#include<iostream>

// All coefficients, inputs, results come from state.py

const double coeff1[1][6] =
{{ 0.02008337,  0.04016673,  0.02008337,
   1.,         -1.56101808,  0.64135154}};

const double input1[] = {
	-1.0,
	0.5,
	1.0};

const double result1[] = {
	-2.00833656e-02,
	-6.14755450e-02,
	-6.30005740e-02};

const double coeff2[2][6] =
{{1.78260999e-03,  3.56521998e-03,  1.78260999e-03,
  1.00000000e+00,  -1.25544047e+00, 4.09013783e-01},
 {1.00000000e+00,  2.00000000e+00,  1.00000000e+00,
  1.00000000e+00,  -1.51824184e+00, 7.03962657e-01}};

const double input2[] = {
	-1,0.5,-1,0.5,-0.3,3,-1E-5
};

const double result2[] = {
	-0.00178261, -0.01118353, -0.03455084, -0.07277369, -0.11973872, -0.158864,
	-0.15873629
};

void assert_double(const double a, const double b) {
	const double d = fabs(a-b);
	const double epsilon = 1E-5;
	const double err1 = d/fabs(a);
	const double err2 = d/fabs(b);
	if ((err1 < epsilon) && (err2 < epsilon)) {
		fprintf(stderr,"%e == %e, err = %e,%e\n",a,b,err1,err2);
		return;
	}
	fprintf(stderr,"%e != %e, err = %e,%e\n",a,b,err1,err2);
	exit(-1);
}

void test1 () {
	fprintf(stderr,"2nd order test, DirectFormI\n");
        const int nSOS = 1;
        Iir::Custom::SOSCascade<nSOS,Iir::DirectFormI> cust(coeff1);
	int i = 0;
	for(auto &v:input1) {
		assert_double(cust.filter(v),result1[i++]);
	}
}

void test2 () {
	fprintf(stderr,"2nd order test, DirectFormII\n");
        const int nSOS = 1;
        Iir::Custom::SOSCascade<nSOS,Iir::DirectFormII> cust(coeff1);
	int i = 0;
	for(auto &v:input1) {
		assert_double(cust.filter(v),result1[i++]);
	}
}

void test3() {
	fprintf(stderr,"4th order test, DirectFormI\n");
        const int nSOS = 2;
        Iir::Custom::SOSCascade<nSOS,Iir::DirectFormI> cust(coeff2);
	int i = 0;
	for(auto &v:input2) {
		assert_double(cust.filter(v),result2[i++]);
	}
}

void test4() {
	fprintf(stderr,"4th order test, DirectFormII\n");
        const int nSOS = 2;
        Iir::Custom::SOSCascade<nSOS,Iir::DirectFormII> cust(coeff2);
	int i = 0;
	for(auto &v:input2) {
		assert_double(cust.filter(v),result2[i++]);
	}
}

int main (int,char**)
{
	test1();
	test2();
	test3();
	test4();

	return 0;
}
