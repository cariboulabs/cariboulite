echo "Compiling device tree file"
dtc -O dtb -o cariboulite.dtbo -b 0 -@ cariboulite-overlay.dts

echo "Generating code blob"
mkdir h_files
../utils/generate_bin_blob ./cariboulite.dtbo cariboulite_dtbo ./h_files/cariboulite_dtbo.h

echo "Copying dtbo blob h-file to the code directory"
cp ./h_files/cariboulite_dtbo.h ../libcariboulite/src/
