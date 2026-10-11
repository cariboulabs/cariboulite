"""Decode cached AT86RF215 I/Q-mode register bytes for a passive display.

The inventory and fields follow Atmel-42415E-WIRELESS-RF215_Datasheet_052016,
especially sections 4, 5, 6 and the register summary in section 8. This module
does not read hardware. In particular, IRQS is a cached interrupt-handler
snapshot: reading the hardware register would clear its pending interrupts.
BBC registers are omitted because accesses have no effect in I/Q radio mode
(section 5.1.1), including the baseband phase measurement unit and counters.
"""


COMMON_REGISTERS = (
    "RST", "CFG", "CLKO", "BMDVC", "XOC", "IQIFC0", "IQIFC1", "IQIFC2",
    "PN", "VN",
)

RADIO_REGISTERS = (
    "IRQS", "IRQM", "AUXS", "STATE", "CMD", "CS", "CCF0L", "CCF0H",
    "CNL", "CNM", "RXBWC", "RXDFE", "AGCC", "AGCS", "RSSI", "EDC",
    "EDD", "EDV", "RNDV", "TXCUTC", "TXDFE", "PAC", "PADFE", "PLL",
    "PLLCF", "TXCI", "TXCQ", "TXDACI", "TXDACQ",
)

# Compact receive view, ordered to match the receiver monitor layout. The
# complete inventories above also serve register addressing and the full view.
RX_COMMON_REGISTERS = ("CFG", "CLKO", "RST", "IQIFC0", "IQIFC1", "IQIFC2")

RX_RADIO_REGISTERS = (
    "RSSI", "RXDFE", "RXBWC", "AGCC", "AGCS", "PAC", "PLL", "PLLCF",
    "IRQM", "IRQS", "STATE",
)

_RADIO_OFFSETS = (
    0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09,
    0x0A, 0x0B, 0x0C, 0x0D, 0x0E, 0x0F, 0x10, 0x11, 0x12, 0x13,
    0x14, 0x16, 0x21, 0x22, 0x25, 0x26, 0x27, 0x28,
)

REGISTER_ADDRESSES = {
    f"RF_{name}": address
    for name, address in zip(COMMON_REGISTERS, range(0x0005, 0x000F))
}
for _prefix, _base, _irq in (("RF09", 0x0100, 0x0000),
                             ("RF24", 0x0200, 0x0001)):
    REGISTER_ADDRESSES[f"{_prefix}_IRQS"] = _irq
    REGISTER_ADDRESSES.update({
        f"{_prefix}_{name}": _base + offset
        for name, offset in zip(RADIO_REGISTERS[1:], _RADIO_OFFSETS)
    })

_BANDWIDTH_KHZ = (160, 200, 250, 320, 400, 500, 630, 800, 1000,
                  1250, 1600, 2000)
_IF_KHZ = (250, 250, 250, 500, 500, 500, 1000, 1000, 1000,
           2000, 2000, 2000)
_LOWPASS_KHZ = (80, 100, 125, 160, 200, 250, 315, 400, 500, 625, 800, 1000)
_RCUT = {0: "0.25*Fs/2", 1: "0.375*Fs/2", 2: "0.5*Fs/2",
         3: "0.75*Fs/2", 4: "1.0*Fs/2"}
_SAMPLE_RATES = {1: "4000kHz", 2: "2000kHz", 3: "4000/3kHz",
                 4: "1000kHz", 5: "800kHz", 6: "2000/3kHz",
                 8: "500kHz", 10: "400kHz"}
_STATES = {2: "TRXOFF", 3: "TXPREP", 4: "TX", 5: "RX",
           6: "TRANSITION", 7: "RESET/SLEEP"}
_COMMANDS = {0: "NOP", 1: "SLEEP", 2: "TRXOFF", 3: "TXPREP",
             4: "TX", 5: "RX", 7: "RESET"}
_CHIP_MODES = {0: "BBRF", 1: "RF", 4: "BBRF09", 5: "BBRF24"}


def _byte(value):
    return value if type(value) is int and 0 <= value <= 255 else None


def _mapping(value):
    return value if isinstance(value, dict) else {}


def _gain_max(rxbwc):
    rxbwc = _byte(rxbwc)
    if rxbwc is None:
        return None
    bandwidth = rxbwc & 15
    return (21 if bandwidth <= 5 else 22 if bandwidth <= 8
            else 23 if bandwidth <= 11 else None)


def decode_agc(agcc, agcs, rxbwc):
    """Return AGC fields, retaining the monitor's earlier public helper API.

    AGCC and AGCS must be bytes. An unavailable or reserved RXBWC bandwidth
    leaves gain_max unknown; it never implies that the gain is at maximum.
    """
    agcc, agcs = _byte(agcc), _byte(agcs)
    if agcc is None or agcs is None:
        raise ValueError("AGCC and AGCS must be integer register bytes")
    return {
        "enabled": bool(agcc & 1),
        "agci": (agcc >> 6) & 1,
        "average_samples": 8 << ((agcc >> 4) & 3),
        "reset": (agcc >> 3) & 1,
        "freeze_status": (agcc >> 2) & 1,
        "freeze_control": (agcc >> 1) & 1,
        "target_dbfs": -21 - 3 * ((agcs >> 5) & 7),
        "gain_word": agcs & 31,
        "gain_max": _gain_max(rxbwc),
    }


def _register_value(group, prefix, name):
    """Read one validated byte; explicit null must not fall back to old data."""
    if "valid" in group and group["valid"] is not True:
        return None
    registers = _mapping(group.get("registers"))
    for key in (name, f"{prefix}_{name}"):
        if key in registers:
            return _byte(registers[key])
    # Earlier version-1 snapshots used a group-wide validity bit and flat keys.
    if group.get("valid") is not True:
        return None
    if name.lower() in group:
        return _byte(group[name.lower()])
    if name == "RSSI":
        value = group.get("rssi_dbm")
        if type(value) in (int, float) and -127 <= value <= 4:
            if value == int(value):
                return int(value) & 255
    return None


def _enum(name, value, descriptions):
    return f"{name}={value}({descriptions.get(value, 'RESERVED')})"


def _bits(value, fields):
    return " ".join(f"{name}={(value >> shift) & mask}"
                    for name, shift, mask in fields)


def _signal_level(name, value):
    signed = value - 256 if value & 128 else value
    if -127 <= signed <= 4:
        return f"{name}={signed}dBm"
    return f"{name}={signed}(INVALID)"


def _frontend(name, value):
    rcut, rate = (value >> 5) & 7, value & 15
    # Undefined SR codes have a documented hardware fallback, unlike RCUT.
    sample_rate = _SAMPLE_RATES.get(rate, "UNDEFINED->4000kHz")
    direct = f" DM={(value >> 4) & 1}" if name == "TXDFE" else ""
    return f"{_enum('RCUT', rcut, _RCUT)}{direct} SR={rate}({sample_rate})"


def _decode_common(name, value):
    if name == "RST":
        command = value & 7
        return f"CMD={command}({'CHIP_RESET' if command == 7 else 'NO_EFFECT'})"
    if name == "CFG":
        return (f"IRQMM={(value >> 3) & 1}"
                f" IRQP={(value >> 2) & 1}"
                f" DRV={value & 3}({2 * ((value & 3) + 1)}mA)")
    if name == "CLKO":
        output = value & 7
        freq = ("OFF", "26MHz", "32MHz", "16MHz", "8MHz", "4MHz",
                "2MHz", "1MHz")[output]
        drive = (value >> 3) & 3
        return f"DRV={drive}({2 * (drive + 1)}mA) OS={output}({freq})"
    if name == "BMDVC":
        high_range, threshold = (value >> 4) & 1, value & 15
        millivolts = (2550 + 75 * threshold if high_range
                     else 1700 + 50 * threshold)
        return (f"BMS={(value >> 5) & 1} BMHR={high_range}"
                f" BMVTH={threshold}({millivolts / 1000:.3f}V)")
    if name == "XOC":
        trim = value & 15
        return f"FS={(value >> 4) & 1} TRIM={trim}(+{0.3 * trim:.1f}pF)"
    if name == "IQIFC0":
        drive, cmv = (value >> 4) & 3, (value >> 2) & 3
        cmv_override = (value >> 1) & 1
        cmv_label = f"{150 + 50 * cmv}mV"
        override_label = "(1.2V;CMV ignored)" if cmv_override else ""
        return (f"EXTLB={(value >> 7) & 1} SF={(value >> 6) & 1}"
                f" DRV={drive}({drive + 1}mA) CMV={cmv}({cmv_label})"
                f" CMV1V2={cmv_override}{override_label} EEC={value & 1}")
    if name == "IQIFC1":
        skew = value & 3
        return (f"FAILSF={(value >> 7) & 1}"
                f" {_enum('CHPM', (value >> 4) & 7, _CHIP_MODES)}"
                f" SKEWDRV={skew}({skew + 1}.906ns)")
    if name == "IQIFC2":
        return f"SYNC={(value >> 7) & 1} (cached latch)"
    if name == "PN":
        part = {0x34: "AT86RF215", 0x35: "AT86RF215IQ", 0x36: "AT86RF215M"}
        return f"PN={value}({part.get(value, 'UNKNOWN')})"
    if name == "VN":
        return f"VN={value}"
    return ""


def _decode_radio(name, value, registers, prefix):
    if name in ("IRQS", "IRQM"):
        fields = _bits(value, (("IQIFSF", 5, 1), ("TRXERR", 4, 1),
                               ("BATLOW", 3, 1), ("EDC", 2, 1),
                               ("TRXRDY", 1, 1), ("WAKEUP", 0, 1)))
        return fields + (" (cached events)" if name == "IRQS" else "")
    if name == "AUXS":
        mapping = (value >> 5) & 3
        voltage = value & 3
        return (f"EXTLNABYP={(value >> 7) & 1}"
                f" {_enum('AGCMAP', mapping, {0: 'INTERNAL', 1: 'LNA9dB', 2: 'LNA12dB'})}"
                f" AVEXT={(value >> 4) & 1} AVEN={(value >> 3) & 1}"
                f" AVS={(value >> 2) & 1}"
                f" {_enum('PAVC', voltage, {0: '2.0V', 1: '2.2V', 2: '2.4V'})}")
    if name == "STATE":
        return _enum("STATE", value & 7, _STATES)
    if name == "CMD":
        return _enum("CMD", value & 7, _COMMANDS)
    if name in ("CCF0L", "CCF0H", "CNL"):
        return f"{name}={value} (frequency byte)"
    if name == "CS":
        channel_mode = registers.get("CNM")
        spacing = (f"({value * 25}kHz)"
                   if channel_mode is not None and channel_mode >> 6 == 0
                   else "(raw byte)")
        return f"CS={value}{spacing}"
    if name == "CNM":
        modes = {0: "CHANNEL_SCHEME", 1: "389.5-510MHz", 2: "779-1020MHz",
                 3: "2400-2483.5MHz"}
        mode = (value >> 6) & 3
        applicability = ""
        if mode and ((prefix == "RF09" and mode == 3)
                     or (prefix == "RF24" and mode in (1, 2))):
            applicability = " (not applicable to this radio)"
        return f"{_enum('CM', mode, modes)} CNH={value & 1}{applicability}"
    if name == "RXBWC":
        bandwidth = value & 15
        description = (f"{_BANDWIDTH_KHZ[bandwidth]}kHz,IFbase={_IF_KHZ[bandwidth]}kHz"
                       if bandwidth < len(_BANDWIDTH_KHZ) else "RESERVED")
        return (f"IFI={(value >> 5) & 1} IFS={(value >> 4) & 1}"
                f" BW={bandwidth}({description})")
    if name in ("RXDFE", "TXDFE"):
        return _frontend(name, value)
    if name == "AGCC":
        averaging, agci = (value >> 4) & 3, (value >> 6) & 1
        return (f"AGCI={agci}({'UNFILT' if agci else 'FILT'})"
                f" AVGS={averaging}({8 << averaging})"
                f" RST={(value >> 3) & 1} FRZS={(value >> 2) & 1}"
                f" FRZC={(value >> 1) & 1} EN={value & 1}")
    if name == "AGCS":
        target, gain = (value >> 5) & 7, value & 31
        maximum = _gain_max(registers.get("RXBWC"))
        if maximum is None:
            gain_label = f"{gain}/unknown"
        else:
            suffix = ("(MAX)" if gain == maximum else "(OUT_OF_RANGE)"
                      if gain > maximum else "")
            gain_label = f"{gain}/{maximum}{suffix}"
        return f"TGT={target}({-21 - 3 * target}dBFS) GCW={gain_label}"
    if name in ("RSSI", "EDV"):
        return _signal_level(name, value)
    if name == "EDC":
        return _enum("EDM", value & 3,
                     {0: "AUTO", 1: "SINGLE", 2: "CONT", 3: "OFF"})
    if name == "EDD":
        factor, basis = (value >> 2) & 63, value & 3
        microseconds = (2, 8, 32, 128)[basis]
        return f"DF={factor} DTB={basis}({microseconds}us) AVG={factor * microseconds}us"
    if name == "RNDV":
        return f"RNDV={value} (raw byte; RNG validity unknown)"
    if name == "TXCUTC":
        ramp, cutoff = (value >> 6) & 3, value & 15
        frequency = (f"{_LOWPASS_KHZ[cutoff]}kHz"
                     if cutoff < len(_LOWPASS_KHZ) else "RESERVED")
        return f"PARAMP={ramp}({4 << ramp}us) LPFCUT={cutoff}({frequency})"
    if name == "PAC":
        current, power = (value >> 5) & 3, value & 31
        reduction = ("-22mA", "-18mA", "-11mA", "FULL")[current]
        power_label = "(MAX)" if power == 31 else "(MIN)" if power == 0 else ""
        return f"PACUR={current}({reduction}) TXPWR={power}{power_label}"
    if name == "PADFE":
        mode = (value >> 6) & 3
        descriptions = ("OFF", "TX/RX_SWITCH", "ENABLE+TX/RX", "TX/RX+LNA_BYPASS")
        return f"PADFE={mode}({descriptions[mode]})"
    if name == "PLL":
        bandwidth, locked = (value >> 4) & 3, (value >> 1) & 1
        bandwidth_label = _enum("LBW", bandwidth, {0: "DEFAULT", 1: "-15%", 2: "+15%"})
        if prefix == "RF24":
            bandwidth_label += "(RF09-only)"
        return f"{bandwidth_label} LS={locked}({'LOCKED' if locked else 'UNLOCKED'})"
    if name == "PLLCF":
        return f"CF={value & 63}"
    if name in ("TXCI", "TXCQ"):
        component = "I" if name == "TXCI" else "Q"
        return f"DCO{component}={value & 63}"
    if name in ("TXDACI", "TXDACQ"):
        component = "I" if name == "TXDACI" else "Q"
        return f"ENTXDAC{component}D={(value >> 7) & 1} TXDAC{component}D={value & 127}"
    return ""


def format_register_rows(hardware, channel, view="rx"):
    """Return one raw-byte/decoded-fields row per relevant cached register.

    hardware.common and hardware.rf09/rf24 contain ``registers`` maps keyed
    by short names (AGCC) or full names (RF24_AGCC). Missing, null, invalid or
    explicitly invalidated group values display as unavailable, never zero.
    Only the selected radio's rows are returned. Unknown channel selections
    retain common rows and clearly indicate that radio selection is unknown.
    The default ``rx`` view selects six common and eleven receiver/status
    registers. ``all`` preserves the complete I/Q-mode inventory. Decoded
    fields follow register bit order from highest to lowest, like the datasheet.
    """
    if view == "rx":
        common_names, radio_names = RX_COMMON_REGISTERS, RX_RADIO_REGISTERS
    elif view == "all":
        common_names, radio_names = COMMON_REGISTERS, RADIO_REGISTERS
    else:
        raise ValueError("register view must be 'rx' or 'all'")
    hardware = _mapping(hardware)
    common = _mapping(hardware.get("common"))
    rows = []
    for name in common_names:
        value = _register_value(common, "RF", name)
        rows.append(f"RF {name}=0x-- unavailable" if value is None
                    else f"RF {name}=0x{value:02X} {_decode_common(name, value)}")
    aliases = {"S1G/RF09": "RF09", "RF09": "RF09", "S1G (RF09)": "RF09",
               "HiF/RF24": "RF24", "RF24": "RF24", "HiF (RF24)": "RF24"}
    prefix = aliases.get(channel) if isinstance(channel, str) else None
    if prefix is None:
        rows.append("Active radio registers unavailable (unknown channel)")
        return rows
    group = _mapping(hardware.get(prefix.lower()))
    registers = {name: _register_value(group, prefix, name)
                 for name in RADIO_REGISTERS}
    for name in radio_names:
        value = registers[name]
        rows.append(f"{prefix} {name}=0x-- unavailable" if value is None
                    else f"{prefix} {name}=0x{value:02X} {_decode_radio(name, value, registers, prefix)}")
    return rows
