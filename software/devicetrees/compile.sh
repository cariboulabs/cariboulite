#dtc -O dtb -o smi.dtbo -b 0 -@ smi-overlay.dts
#dtc -O dtb -o smi-dev.dtbo -b 0 -@ smi-dev-overlay.dts

echo "Compiling device tree file"
dtc -O dtb -o cariboulite.dtbo -b 0 -@ cariboulite-overlay.dts

#../utils/generate_bin_blob ./smi.dtbo smi_dtbo ./h_files/smi_dtbo.h
#../utils/generate_bin_blob ./smi-dev.dtbo smi_dev_dtbo ./h_files/smi_dev_dtbo.h

echo "Generating code blob"
../utils/generate_bin_blob ./cariboulite.dtbo cariboulite_dtbo ./h_files/cariboulite_dtbo.h

echo "Copying dtbo blob h-file to the code directory"
cp ./h_files/cariboulite_dtbo.h ../libcariboulite/src/cariboulite_eeprom/