function ref = find_ref_freq(f)
  f_rf_mod_32 = f / 32e6;
  f_rf_mod_26 = f / 26e6;
  f_rf_mod_32 = f_rf_mod_32 - floor(f_rf_mod_32);
  f_rf_mod_26 = f_rf_mod_26 - floor(f_rf_mod_26);
  f_rf_mod_32 = f_rf_mod_32 * 32e6;
  f_rf_mod_26 = f_rf_mod_26 * 26e6;
  if (f_rf_mod_32 > 16e6)
    f_rf_mod_32 = 32e6 - f_rf_mod_32;
  endif
  if (f_rf_mod_26 > 13e6)
    f_rf_mod_26 = 26e6 - f_rf_mod_26;
  endif

  if f_rf_mod_32 > f_rf_mod_26
    ref = 32e6;
  else
    ref = 26e6;
  endif
endfunction