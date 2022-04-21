// Part of dump1090, a Mode S message decoder for RTLSDR devices.
//
// cpr.h - Compact Position Reporting prototypes
//
// Copyright (c) 2014,2015 Oliver Jowett <oliver@mutability.co.uk>
//
// This file is free software: you may copy, redistribute and/or modify it
// under the terms of the GNU General Public License as published by the
// Free Software Foundation, either version 2 of the License, or (at your
// option) any later version.
//
// This file is distributed in the hope that it will be useful, but
// WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
// General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program.  If not, see <http://www.gnu.org/licenses/>.

#ifndef DUMP1090_CPR_H
#define DUMP1090_CPR_H

int decodeCPRairborne(int even_cprlat, int even_cprlon,
                      int odd_cprlat, int odd_cprlon,
                      int fflag,
                      double *out_lat, double *out_lon);

int decodeCPRsurface(double reflat, double reflon,
                     int even_cprlat, int even_cprlon,
                     int odd_cprlat, int odd_cprlon,
                     int fflag,
                     double *out_lat, double *out_lon);

int decodeCPRrelative(double reflat, double reflon,
                      int cprlat, int cprlon,
                      int fflag, int surface,
                      double *out_lat, double *out_lon);

#endif
