# HAT identification and EEPROM support

This component reads board identity, handles the HAT EEPROM format and provides
EEPROM helpers used by the native library and manufacturing tools. Its current
interfaces are in [hat.h](hat.h) and [eeprom_utils.h](eeprom_utils.h).

The [EEPROM programming tutorial](../../../../docs/flashing/README.md) is a
manufacturing procedure with an outstanding validation checklist. Normal
installation and board detection do not require rewriting the EEPROM.

# License
<a rel="license" href="http://creativecommons.org/licenses/by-sa/4.0/"><img alt="Creative Commons License" style="border-width:0" src="https://i.creativecommons.org/l/by-sa/4.0/88x31.png" /></a><br />This work is licensed under a <a rel="license" href="http://creativecommons.org/licenses/by-sa/4.0/">Creative Commons Attribution-ShareAlike 4.0 International License</a>.
