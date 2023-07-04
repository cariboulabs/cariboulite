clc;
close all;
clear;

CARIBOULITE_6G_MIN = 1e6;
CARIBOULITE_6G_MAX = 6000e6;
CARIBOULITE_MIN_LO = 85e6;
CARIBOULITE_MAX_LO = 4200e6;
CARIBOULITE_2G4_MIN = 2385e6;
CARIBOULITE_2G4_MAX = 2495e6;
CARIBOULITE_S1G_MIN1 = 377e6;
CARIBOULITE_S1G_MAX1 = 530e6;
CARIBOULITE_S1G_MIN2 = 779e6;
CARIBOULITE_S1G_MAX2 = 1020e6;

f_vec = 1e6:1e6:6000e6;
ref_vec = f_vec * 0;
mod_vec = f_vec * 0;
lo_vec = f_vec * 0;
rf_vec = f_vec * 0;

for ii=1:length(f_vec)
  if f_vec(ii)>=CARIBOULITE_6G_MIN & f_vec(ii)<CARIBOULITE_2G4_MIN
    ref_vec(ii) = find_ref_freq(f_vec(ii));
    mod_vec(ii) = CARIBOULITE_2G4_MAX;
    lo_vec(ii) = mod_vec(ii) - f_vec(ii);
    rf_vec(ii) = mod_vec(ii) - lo_vec(ii);
  elseif f_vec(ii)>=CARIBOULITE_2G4_MIN & f_vec(ii)<CARIBOULITE_2G4_MAX
    lo_vec(ii) = 0;
    mod_vec(ii) = f_vec(ii);
    rf_vec(ii) = mod_vec(ii);
  elseif f_vec(ii)>=CARIBOULITE_2G4_MAX & f_vec(ii)<=CARIBOULITE_6G_MAX
    ref_vec(ii) = find_ref_freq(f_vec(ii));
    mod_vec(ii) = CARIBOULITE_2G4_MIN;
    lo_vec(ii) = f_vec(ii) - mod_vec(ii);
    rf_vec(ii) = mod_vec(ii) + lo_vec(ii);
  endif
endfor

figure;
subplot(4,1,1);
plot(f_vec / 1e6, ref_vec / 1e6); xlabel('Frequency [MHz]'); ylabel('Ref Freq [Mhz]'); title('Ref Frequency'); axis tight;
subplot(4,1,2);
plot(f_vec / 1e6, mod_vec / 1e6); xlabel('Frequency [MHz]'); ylabel('Modem Freq [Mhz]'); title('Chosen Modem Frequency [MHz]'); axis tight;
subplot(4,1,3);
plot(f_vec / 1e6, lo_vec / 1e6); xlabel('Frequency [MHz]'); ylabel('LO Freq [Mhz]'); title('LO Frequency [MHz]'); axis tight;
subplot(4,1,4);
plot(f_vec / 1e6, rf_vec / 1e6); xlabel('Frequency [MHz]'); ylabel('RF Freq [Mhz]'); title('RF Frequency [MHz]'); axis tight;