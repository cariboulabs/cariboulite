# CaribouLite FPGA + Raspberry Pi SMI Engineering History

**Prepared:** 2026-09-12  
**Purpose:** Preserve the accumulated engineering knowledge from the CaribouLite FPGA, LVDS, SMI, TX/RX, modem-control, and Raspberry Pi integration work.  
**Audience:** Future development in VS Code/Codex; engineers maintaining or reviving the CaribouLite platform; first-article validation of reproduced Rev 2.8 boards.

**Current-source review: 2026-10-16.** This is a historical narrative prepared
on September 12, not the current architecture specification or a host support
matrix. Performance, clock, register and thread values below describe their
original experiments; in particular the Pi Zero's approximately 75 frames/s,
eight-frame FIFO concept and writer priority around 80 are not current TX
defaults. Historical Pi 5 experiments do not establish a supported installation
of the current BCM2835 SMI driver.

The current source uses cyclic RX/TX DMA and hardware-paced TX (`37d2414`):
four application frames, two periods of kernel admission, independent microphone
capture/adaptive resampling and producer/writer priority requests 40/48 on
CPUs 1/2, where permitted. Six 180-second microphone/tone trials at 1/2/4 MS/s
passed on each of the Pi Zero 2 W and Pi 4 at approximately 100 frames/s,
alongside RX restoration and qualitative listening checks. See
[TX design and qualification](docs/tx-hardware-paced-design.md),
[driver/API notes](driver/README.md) and [radio control](docs/radio-control.md)
for current behavior and measurement limits. Requested scheduling is separate
from the policy actually granted by Linux.

The FPGA remains the September 16 gap/reset/loopback baseline; its packaged
image hash, build settings, register contract and validation limits are in
[firmware/README.md](firmware/README.md). LVDS optimization and PMOD sequencer
integration are still [planned](docs/lvds-fmax-next-test-2026-10-08.md).
`smi_ctrl.v` currently forces TX-enable high in assembled LVDS words; host
control bits or zero IQ do not unkey RF. The `TX_EN = I[0]` discussion below
records the modem interface and earlier experiments rather than current
propagation of a host control bit.

---

# 1. Executive summary

Over an extended development and debugging effort, the CaribouLite platform was taken well beyond its original software state and used as a practical Raspberry Pi SDR platform with working transmit and receive paths, custom FPGA work, low-level SMI integration, modem register control, real-time streaming, and diagnostic tooling.

The most important achievements were:

- understanding and modifying the FPGA-side LVDS and SMI data paths;
- establishing the relationship between Raspberry Pi SMI transfers, FPGA buffering, LVDS framing and AT86RF215 IQ framing;
- building a reliable TX pipeline from audio through NBFM generation to 4 MSPS IQ;
- building a corresponding RX pipeline;
- implementing multi-threaded real-time buffering around SMI transfers;
- developing software synchronization/recovery around SMI/FPGA stream timing;
- identifying that superficially similar SMI synchronization failures may have **either software/driver causes or genuine board-level hardware causes**;
- establishing a known-good Rev 2.8 board as an A/B reference;
- diagnosing one second board whose persistent RX synchronization and dirty-TX behavior was ultimately judged to be hardware-related;
- validating the platform across Raspberry Pi Zero 2 W, Raspberry Pi 4 and Raspberry Pi 5 systems;
- bringing the code forward across Raspberry Pi OS Bookworm and later Trixie environments;
- developing enough low-level understanding to support future reproduction, bring-up and manufacturing validation of new Rev 2.8 boards.

The current engineering philosophy is conservative:

> First reproduce and validate the original Rev 2.8 hardware and data path exactly.  
> Only after that should architectural improvements or redesigns be considered.

---

# 2. System architecture

At a high level the working system is:

```text
             Raspberry Pi
        ┌─────────────────────┐
        │ DSP / audio / modem │
        │ userspace threads   │
        └─────────┬───────────┘
                  │
                  │ IQ samples / control
                  ▼
        ┌─────────────────────┐
        │ Raspberry Pi SMI    │
        │ kernel + userspace  │
        └─────────┬───────────┘
                  │ parallel SMI bus
                  ▼
        ┌─────────────────────┐
        │ CaribouLite FPGA    │
        │ registers / FIFOs   │
        │ framing / timing    │
        └─────────┬───────────┘
                  │ LVDS IQ interface
                  ▼
        ┌─────────────────────┐
        │ AT86RF215           │
        │ S1G + HiF RF modem  │
        └─────────┬───────────┘
                  │
                  ▼
                 RF
```

The FPGA is not merely glue logic. It is the timing boundary between two very different interfaces:

1. the Raspberry Pi SMI parallel interface, which moves control words and IQ samples between Linux and the HAT;
2. the RF215 LVDS IQ interface, which has strict framing and clocking requirements.

Much of the engineering work centered on making those two timing domains coexist robustly.

---

# 3. Hardware context

The development history involved two CaribouLite boards:

- one known-good board;
- one problematic board that showed persistent RX SMI synchronization failures and unstable/dirty TX.

The good board became the reference platform.

The problematic board became extremely useful diagnostically because identical software could be tested against both boards.

This established an important principle:

> An SMI synchronization failure cannot automatically be blamed on the SMI driver.

Incorrect SMI timing/configuration can certainly produce synchronization failures, but genuine FPGA/LVDS/clock/RF215 hardware faults can produce very similar symptoms.

This distinction became central to later debugging.

---

# 4. Raspberry Pi platforms used

The work spans several Raspberry Pi platforms.

## 4.1 Raspberry Pi Zero 2 W

The Zero 2 W was used as a compact CaribouLite host.

Observed performance was lower than on the Pi 4.

For the main 4 MSPS TX path, the Zero 2 W achieved roughly:

```text
~75 frames / producer operations per second
```

compared with the Pi 4's ability to maintain the intended ~100 operations/s cadence.

This made the Zero 2 W useful for exposing timing sensitivity and buffering requirements.

## 4.2 Raspberry Pi 4

The Pi 4 became a strong reference platform.

A working system could sustain approximately:

```text
100 producer frames/s
```

for the main 10 ms producer cadence.

Pi 4 systems were also useful for A/B testing because both known-good and problematic boards could be exercised with the same host software.

A separate support case involving another Pi 4 2 GB showed significantly worse behavior:

```text
~20–30 TX puts/s
TX FIFO 64/64
IQ sync false
RF24 state around 0x03
"producer: re-anchor (rc=0)"
```

That case reinforced that host/kernel/driver configuration can also produce symptoms that resemble hardware failure.

## 4.3 Raspberry Pi 5

The Pi 5 was used for later software and SMI work and as a higher-performance development machine.

The Pi 5 was also the platform on which software-side stream synchronization and more sophisticated TX/RX threading were exercised.

---

# 5. Operating-system evolution

The work crossed several Raspberry Pi OS generations.

Known environments included:

- Raspberry Pi OS Bookworm Lite;
- Raspberry Pi OS Bookworm Desktop;
- later Raspberry Pi 4 rebuilds using Trixie;
- Raspberry Pi 5 systems based on Bookworm.

One important observation was that Bookworm Lite proved more predictable than some earlier Desktop configurations during SMI synchronization debugging.

Kernel-module mismatches were also encountered.

A representative external support case showed a module path/version mismatch such as:

```text
6.12.62
vs
6.12.75
```

This is a reminder that an apparently correct driver installation can silently fail if the built module and running kernel are not aligned.

---

# 6. Raspberry Pi SMI role

SMI is the high-throughput parallel bus connecting the Raspberry Pi to the FPGA.

The SMI path serves two broad functions:

1. register/control communication with the FPGA and attached RF subsystem;
2. streaming IQ data.

SMI is therefore both a control interface and a real-time sample transport.

The engineering challenge is that Linux userspace, kernel buffering, DMA/SMI timing and FPGA framing all have to remain aligned.

A failure in any one layer can appear as:

- RX synchronization failure;
- stale or invalid IQ;
- TX FIFO starvation;
- TX FIFO saturation;
- partial frames;
- corrupted sample alignment;
- a stream that starts but later falls out of sync.

---

# 7. SMI peripheral reset work

One low-level implementation detail that proved useful was the SMI reset sequence.

A reset operation used the SMI register at offset:

```text
0x2c
```

from the SMI peripheral base.

The reset sequence was:

```text
write 0x08
write 0x00
```

This was part of work intended to ensure that the SMI peripheral returned to a known state before transfers resumed.

This sort of explicit reset became important during iterative development because stale hardware state can survive process restarts.

---

# 8. SMI configuration findings

At one stage, a modem/SMI configuration mismatch was isolated.

The corrected values recorded during that investigation were:

```text
SMI_L = 0xA0002083
SMI_D = 0xA0002123
```

Previous values could produce visible TX activity while RX remained invalid.

The important lesson was that SMI configuration can fail asymmetrically:

> "TX does something" is not proof that the bus is configured correctly for RX.

RX is often the more sensitive diagnostic path because a malformed stream exposes frame-alignment failure immediately.

---

# 9. SMI synchronization strategy

One of the major software achievements was a software synchronization mechanism capable of recovering or re-anchoring the stream.

The software could detect loss of expected framing and reposition itself relative to FPGA output.

A representative diagnostic message was:

```text
producer: re-anchor (rc=0)
```

This concept is important because a Linux process cannot assume that every read begins exactly at the FPGA frame boundary.

A robust implementation therefore needs:

- framing markers or recognizable structure;
- a mechanism to scan/re-anchor;
- safe handling of partial data;
- recovery without restarting the entire application where possible.

This work turned synchronization from a one-time startup assumption into a recoverable runtime property.

---

# 10. Distinguishing software SMI faults from hardware faults

This became one of the most important project lessons.

A failing board and an incorrect driver can produce remarkably similar symptoms.

Useful diagnostic questions became:

1. **Has this exact board ever worked in another host/software configuration?**
2. **Does another CaribouLite board work on the exact same host/software setup?**
3. **Does the board fail across multiple Raspberry Pi models and OS images?**
4. **Does FPGA programming/self-test work while IQ streaming fails?**
5. **Can TX work while RX synchronization fails?**
6. **Does a known-good board pass with no software changes?**

The user's own A/B case was particularly strong:

```text
same software
same host
same test setup
good board works
bad board fails RX synchronization and produces dirty/unstable TX
```

That made a purely software explanation unlikely.

---

# 11. FPGA development overview

The FPGA work evolved over time.

Some earlier experiments used framing/clock structures that differ from the later working implementation.

This history is worth preserving because old branches or snippets may contain these earlier formats.

Do not blindly mix assumptions from one phase with another.

---

# 12. Early LVDS experiments

Earlier FPGA experiments included a narrower frame representation.

One recorded framing experiment used a 12-bit concept in which special treatment was given to low-order I bits.

For example:

```text
frame[0] = ~i_data[0]
frame[1] = ~i_data[1]
payload copied into frame[2+:DATA_WIDTH]
unused bits zero-filled
```

RX reversed the special treatment:

```text
i_data[0] = ~frame[0]
i_data[1] = ~frame[1]
```

The corresponding receive FSM included states such as:

```text
IDLE
DATA
WAIT_EVEN
WAIT_ODD
```

Debugging concentrated on:

- initial state;
- marker polarity;
- even/odd framing;
- frame alignment;
- correct cell boundaries.

These experiments helped establish the need for explicit framing rather than treating the LVDS stream as raw sample bits.

---

# 13. Early clocking experiments

Historical FPGA work also referenced clock relationships around:

```text
FPGA clock ~48 MHz
LVDS clock ~96 MHz
```

These values belong to an earlier development phase and should not be assumed to describe the later stable implementation.

They are retained here because old HDL branches may use them.

---

# 14. Intermediate 16-bit cell experiments

Another evolution changed a `bits_to_cells` concept from:

```text
6
```

to:

```text
8
```

producing 16-bit cells instead of 12-bit cells.

An experimental representation included I/Q payload bits plus zero/padding cells.

This phase also required better partial-frame handling.

The FPGA FSM was extended to:

- retain a `frame_idx`;
- handle incomplete frames;
- explicitly signal completed frames;
- ensure the frame counter could represent every cell.

A specific bug was that a frame index was too narrow.

With:

```text
FRAME_CELLS = 9
```

the counter needed to represent at least:

```text
0 .. 8
```

and therefore required 4 bits rather than 3.

This is a classic HDL failure mode:

> control counters must be sized for the number of states/cells, not merely for the nominal payload width.

---

# 15. HDL portability lessons

Not all synthesis tooling accepted the same SystemVerilog syntax.

One concrete portability issue involved declarations such as:

```text
int unsigned
```

in parameter/port contexts.

Using:

```text
parameter integer
```

proved more portable with the open-source synthesis flow.

This mattered because the final CaribouLite FPGA toolchain targeted:

```text
yosys
nextpnr
icestorm
```

for an:

```text
iCE40 LP1K QN84
```

device.

---

# 16. Final/working IQ framing model

The later working implementation used a 32-bit IQ word with explicit synchronization patterns.

The recorded frame format is:

```text
[31:30] = 10      I synchronization marker
[29:16] = I[13:0]
[15:14] = 01      Q synchronization marker
[13:0]  = Q[13:0]
```

This gives:

```text
2 marker bits
14 I bits
2 marker bits
14 Q bits
= 32 bits
```

The markers allow software/FPGA logic to recognize correct I/Q alignment.

This framing is central to the software synchronization strategy.

---

# 17. TX enable embedded in IQ

With EEC enabled, the implementation used:

```text
TX_EN = I[0]
```

That means a low-order I bit participates in transmit-enable behavior.

The modem configuration therefore cannot be understood solely as "14-bit sample values."

The exact EEC/framing interaction matters.

A working configuration used:

```text
EEC = 1
```

This is another reason why generic sample repacking or "cleaning up" low bits can break transmit behavior.

---

# 18. Later FPGA TX FSM

This section records an earlier FSM. Current `firmware/lvds_tx.v` uses `IDLE`,
`TX_FRAME`, `TX_GAP` and `LOOPBACK`; it has no `TX_CHECK` state.

A later working LVDS TX state machine used states including:

```text
IDLE
TX_CHECK
TX_FRAME
TX_GAP
DEBUG
```

Conceptually:

- `IDLE` waits for work;
- `TX_CHECK` determines whether a complete/valid frame is available;
- `TX_FRAME` clocks a frame onto the LVDS interface;
- `TX_GAP` enforces the required inter-frame behavior/timing;
- `DEBUG` supports diagnostic operation.

The explicit `TX_CHECK` stage helps separate FIFO availability from LVDS emission.

This is preferable to immediately entering transmission whenever any data is present because incomplete sample groups can otherwise corrupt alignment.

---

# 19. FPGA clocks in the later implementation

The later implementation operated with clocks roughly around:

```text
system clock: ~64 MHz
LVDS clock:   ~64 MHz
SMI clock:    ~16 MHz
```

These were the approximate values associated with that development stage.
Current `top.v` divides the 125 MHz board clock by two for a nominal 62.5 MHz
system clock, and uses the modem LVDS receive clock at nominal 64 MHz. SMI bus
cadence depends on the host clock and programmed timing; the historical
approximately 16 MHz value is not the peripheral's source-clock setting.

The multiple clock domains make CDC and FIFO behavior central to stability.

---

# 20. RX clock debugging

A dedicated RX-clock debugging mechanism was implemented.

One historical implementation used:

```text
logic [26:0] rx_clk_counter
```

and measured/latching RX clock behavior over a comparatively long interval.

The concept was:

- increment while the RX clock condition was present;
- reset/latch when it was absent;
- expose the measured period/activity for software inspection.

The exact historical clock basis for that diagnostic was around 30.72 MHz in one experimental branch.

The important achievement was broader:

> clock presence and cadence were made observable instead of being inferred indirectly from failed sample streams.

This is a useful pattern for future board bring-up.

---

# 21. FPGA FIFO behavior

The FPGA contains buffering between the SMI side and LVDS side.

The host software also maintains its own FIFO(s).

The complete system therefore contains several elasticity stages:

```text
DSP producer
   ↓
software TX FIFO
   ↓
TX writer / SMI
   ↓
FPGA FIFO
   ↓
LVDS TX FSM
   ↓
RF215
```

FIFO status is diagnostically important.

For example, a host-side FIFO stuck at:

```text
64/64
```

indicates a consumer path failing to drain fast enough.

This can point to:

- SMI throughput;
- writer thread scheduling;
- SMI driver failure;
- FPGA-side backpressure;
- synchronization preventing valid consumption.

---

# 22. Audio-to-RF TX pipeline

The final software pipeline was approximately:

```text
48 kS/s audio
    ↓
NBFM modulator
    ↓
4 MSPS complex IQ16
    ↓
rf10 FIFO
    ↓
TX writer
    ↓
SMI
    ↓
FPGA FIFO
    ↓
LVDS
    ↓
AT86RF215
```

The normal producer cadence was:

```text
10 ms
```

This means the producer should execute about:

```text
100 times per second
```

on a fully keeping-up system.

---

# 23. Main sample rates

Common operating sample rates included:

```text
4 MSPS    primary IQ rate
2 MSPS    tested
500 kSPS  tested
48 kS/s   audio
```

The 4 MSPS path became the main working configuration.

A major reason for using a structured producer/writer architecture was to decouple DSP generation from the timing variability of SMI transfers.

---

# 24. NBFM implementation

The application included a functional NBFM transmitter.

Representative settings included:

```text
deviation: 2500 Hz
baseband tone: 600 Hz
```

Additional signaling tones were used:

```text
start tone: 2525 Hz for 250 ms
stop tone:  2475 Hz for 250 ms
```

A TX bandwidth cutoff around:

```text
80 kHz
```

was also used.

The existence of a working end-to-end NBFM path is significant because it validates:

- audio ingestion;
- DSP timing;
- IQ generation;
- FIFO cadence;
- SMI TX;
- FPGA framing;
- LVDS;
- RF215 TX;
- RF output quality.

---

# 25. Software thread architecture

The later application separated major responsibilities into threads.

Representative threads included:

```text
dsp_producer
tx_writer
rx_reader
nbfm_demod
audio_writer
```

This separation was important.

A single monolithic loop made the system too sensitive to occasional blocking or scheduling delays.

---

# 26. Real-time scheduling work

Real-time behavior was improved using Linux scheduling techniques.

Representative priorities included:

```text
producer ~40
writer   ~80
```

The writer was given the higher priority because starving the hardware-facing consumer can cause the FIFO to fill and the stream to lose timing.

Additional measures included:

```text
mlockall()
CPU affinity
```

The purpose was to reduce:

- page faults;
- scheduler migration;
- unpredictable memory latency;
- long service gaps.

The system did not rely on "Linux is fast enough most of the time."

It was intentionally shaped around the latency requirements of a continuous SDR stream.

---

# 27. Producer cadence results

On a Raspberry Pi 4, the producer could maintain the intended cadence:

```text
~100 frames/s
```

On a Raspberry Pi Zero 2 W, a representative result was closer to:

```text
~75 frames/s
```

This gap mattered because 75/s is not merely "25% slower" in an abstract benchmark.

For a fixed-rate streaming system it means the producer cannot continuously supply a 100 Hz schedule without buffering or dropping/re-anchoring.

---

# 28. rf10 FIFO concept

A software FIFO referred to as `rf10` held approximately:

```text
8 × 10 ms
```

of data.

That gives roughly:

```text
80 ms
```

of buffering.

This size was large enough to absorb short scheduling disturbances while still keeping latency bounded.

It also provides an explicit diagnostic window:

- slowly decreasing fill indicates producer starvation;
- slowly increasing fill indicates consumer/SMI underperformance;
- sudden discontinuities indicate synchronization or reset events.

---

# 29. TX writer role

The TX writer's job was deliberately separated from modulation/DSP.

Its responsibilities included:

- consume already-produced IQ frames;
- maintain SMI output cadence;
- avoid DSP computation in the hardware-facing path;
- react correctly to FIFO underrun/stream reset;
- support synchronization/re-anchor behavior.

This design made performance analysis much clearer.

If the producer maintained 100 Hz but the FIFO still filled, the SMI/writer side was implicated.

If the FIFO drained, the producer/DSP side was implicated.

---

# 30. RX path

The receive side similarly used:

```text
SMI
↓
rx_reader
↓
demodulation / DSP
↓
audio_writer
```

with NBFM demodulation as one supported application.

The RX side was also the most sensitive indicator of framing integrity.

An RX synchronization failure often occurred before any useful demodulation could happen.

---

# 31. ALSA / virtual audio testing

Linux audio-path testing used tools including:

```text
snd-aloop
speaker-test
arecord
aplay
```

A typical loop test used:

```text
48 kHz
S16_LE
mono
```

and a simple:

```text
440 Hz
```

test tone.

This allowed audio faults to be separated from RF/IQ faults.

---

# 32. GNU Radio / SoapySDR work

SoapySDR tooling was used to verify device enumeration and streaming.

`SoapySDRUtil` could identify both:

```text
S1G
HiF
```

channels.

GNU Radio resampling was also used, including conversions such as:

```text
2e6 → 48k
4e6 → 48k
```

This provided an independent way to inspect RX behavior outside the custom application.

---

# 33. AT86RF215 register configuration

A set of working modem register values was established.

Representative values include:

```text
IQIFC0 = 0x33
IQIFC1 = 0x11
IQIFC2 = 0x8B

TXDFE  = 0x81
RXDFE  = 0x81

RF09 STATE TX = 0x04
RF24 STATE RX = 0x02

PAC    = 0x72
PADFE  = 0x40
IRQM   = 0x3F
RF_CFG = 0x08
RF_CLKO = 0x1A
```

These values became useful as a known-working baseline.

When debugging a new or failing board, register comparison against this baseline can reveal whether the fault is:

- configuration;
- SPI communication;
- PLL/clock;
- state-machine progression;
- IQ interface.

---

# 34. RF channel usage

Both AT86RF215 RF paths were used:

```text
S1G
HiF
```

Typical TX experiments often used approximately:

```text
430.1 MHz
```

Transmit power settings tested included:

```text
-10 dBm
-3 dBm
0 dBm
```

---

# 35. CW/special-mode testing

CW or special transmit modes were important diagnostic tools because they reduce the data-path complexity.

A CW test can answer:

- is the RF synthesizer locking?
- is the RF output path alive?
- is the frequency roughly correct?
- does the TX chain work independently of IQ streaming quality?

Tests around:

```text
430 MHz
999 MHz
```

showed both boards approximately:

```text
~2 kHz low
```

This common offset suggested that this particular frequency error was not unique to the bad board.

A measured ~6 dB difference in one comparison was later understood in terms of attenuation/path differences rather than necessarily a core RF fault.

---

# 36. Noise observations

One RX/TX observation included a noise pedestal roughly:

```text
~20 kHz wide
~ -100 dB
```

This became part of the qualitative baseline for judging whether a transmitted or received spectrum looked healthy.

The problematic board's TX was notably less clean than the good board.

---

# 37. Shutdown / SIGSEGV issue

A software lifecycle bug was identified where shutdown could produce:

```text
SIGSEGV
```

if cleanup attempted to join or otherwise manipulate threads that had never successfully started.

The general fix principle was:

- maintain explicit thread-started state;
- only join valid/created threads;
- make shutdown idempotent;
- stop hardware-facing threads in a defined order;
- avoid cleanup code assuming full initialization.

This is relevant to future manufacturing test software because first-article failures will intentionally exercise partial-init paths.

---

# 38. Hardware fault investigation: bad board

The second CaribouLite board became a long-running hardware investigation.

Key symptoms:

- RX SMI synchronization failed;
- TX was unstable/dirty;
- the good board worked under the same software environment.

This greatly reduced the probability that the observed fault was purely the SMI driver.

---

# 39. TCXO investigation

The bad board's 26 MHz TCXO was suspected.

The fitted device was:

```text
ATX-12-F-26.000MHz-F05-T
```

This is also the exact Rev 2.8 production BOM component.

The measured oscillator frequency was approximately:

```text
26 MHz
```

so the device was not simply dead.

Nevertheless, PLL-related behavior differed between boards.

Representative PLLCF observations:

```text
good board: 0x1C
bad board:  0x1D / 0x1E
```

That difference increased suspicion around the clock/PLL path.

An attempted TCXO replacement ultimately damaged/destroyed the bad board.

While unfortunate, the investigation established an important validation requirement for newly reproduced boards:

> clock quality must be validated by more than measuring "26 MHz is present."

Future checks should include:

- frequency;
- waveform/amplitude;
- supply quality;
- startup;
- loading;
- RF215 PLL behavior;
- comparison of relevant registers against the known-good board.

---

# 40. Hardware/software ambiguity lesson

The bad-board investigation fundamentally changed how SMI sync faults are interpreted.

Before this, an error such as:

```text
SMI data synchronization failed
```

could easily lead directly to driver debugging.

After the A/B experience, the correct diagnosis flow became:

```text
SMI sync fault
    ↓
verify host/kernel/module
    ↓
verify FPGA load/self-test
    ↓
verify clocking
    ↓
compare against known-good board on same host
    ↓
only then classify software vs hardware
```

This lesson is especially relevant to community reports because casual users may have no second board for A/B testing.

---

# 41. RFFC507x work

The board's mixer/synthesizer path was also investigated.

A device ID of approximately:

```text
0x1140
```

was associated with an RFFC5072 readback.

Some code also referenced:

```text
0x11C0
```

associated with RFFC5071 handling.

This highlighted the importance of checking actual hardware identification rather than assuming a single device variant.

Low-level SPI work included direct register inspection.

Registers examined during experiments included addresses around:

```text
0x0000
0x0004
0x0006
0x0008
0x0009
0x000A
0x000B
0x0012
0x0013
0x0014
0x0017
0x0018
0x0019
```

An FTDI/SPI/Python path was used in some debugging to interrogate devices independently of the full driver stack.

---

# 42. Open-source FPGA toolchain

The FPGA development target was:

```text
Lattice iCE40 LP1K QN84
```

with an open-source toolchain:

```text
yosys
nextpnr
icestorm
```

This was strategically valuable because the design remains reproducible without dependence on a proprietary FPGA environment.

The broader future redesign idea considered ECP5 + Ethernet, but that belongs to a later-generation project, not the current Rev 2.8 reproduction.

---

# 43. Known good vs bad board comparison as an engineering asset

The strongest diagnostic tool throughout the project was not a software feature.

It was:

```text
one known-good physical board
+
one problematic physical board
+
identical host/software
```

This allowed:

- register A/B comparisons;
- RF spectrum A/B comparisons;
- SMI behavior A/B comparisons;
- clock/PLL A/B comparisons;
- elimination of host-side causes.

The remaining known-good board should therefore be treated as a laboratory reference and not modified unnecessarily.

---

# 44. Implications for reproduced Rev 2.8 boards

The accumulated FPGA/SMI knowledge provides a much stronger manufacturing-validation capability than a normal open-source reproduction project.

A newly built board can be validated at several layers.

## Layer 1 — passive/manufacturing

- shorts;
- rails;
- visual assembly;
- orientation;
- soldering.

## Layer 2 — clocks

- 26 MHz TCXO;
- 125 MHz oscillator;
- FPGA clock domains.

## Layer 3 — digital control

- FPGA programming;
- FPGA registers;
- SPI;
- AT86RF215 register access;
- RFFC5072 communication.

## Layer 4 — SMI

- basic reads/writes;
- FIFO behavior;
- synchronization markers;
- sustained streaming.

## Layer 5 — LVDS

- frame alignment;
- TX_EN behavior;
- RX sample integrity.

## Layer 6 — RF

- CW;
- frequency accuracy;
- TX spectrum;
- S1G receive;
- HiF receive;
- modulation quality.

This is an unusually complete first-article test stack.

---

# 45. Recommended regression tests for FPGA/SMI development

Any future change to FPGA or SMI code should at minimum verify:

```text
1. FPGA load succeeds.
2. Register read/write succeeds.
3. RF09 and RF24 state transitions succeed.
4. RX synchronization locks.
5. RX synchronization survives sustained streaming.
6. TX FIFO does not monotonically fill.
7. Producer stays close to intended cadence.
8. TX writer keeps up with producer.
9. S1G RX works.
10. HiF RX works.
11. CW TX works.
12. 4 MSPS NBFM TX works.
13. Clean shutdown works.
14. Restart without power cycle works.
15. SMI reset/re-anchor path works.
```

Prefer running these tests on:

```text
Pi 4 + known-good Rev 2.8
```

before trying less forgiving platforms.

---

# 46. Suggested telemetry to preserve in future code

For future maintainability, runtime status should expose:

- producer frames/s;
- TX writer frames/s;
- RX reader frames/s;
- software FIFO fill level;
- FPGA FIFO status if readable;
- SMI synchronization status;
- re-anchor count;
- underrun count;
- overrun count;
- AT86RF215 RF state;
- selected channel/frequency;
- actual sample rate;
- thread scheduling policy/priority;
- clock diagnostics where available.

These metrics make a streaming SDR debuggable without an oscilloscope for every problem.

---

# 47. Suggested SMI-driver engineering principles

The experience so far suggests the SMI driver should follow these principles:

### Explicit initialization

Do not assume reset defaults.

### Explicit reset/recovery

Provide a controlled way to reset SMI and restart streaming.

### Kernel-version awareness

Fail clearly when built/installed for the wrong kernel.

### Expose useful state

Make transfer/synchronization failures observable.

### Avoid hidden timing assumptions

Document SMI timing register values and why they are selected.

### Keep streaming and control paths conceptually separate

A working control transaction does not prove IQ streaming is healthy.

### Recovery over process restart

Where possible, make loss of alignment recoverable through re-anchor/reset rather than forcing a reboot.

---

# 48. Suggested FPGA engineering principles

### Preserve explicit framing

The I/Q sync markers are valuable and should not be removed casually.

### Keep clock-domain crossings visible

SMI and LVDS timing domains should be bridged with explicit FIFOs/handshakes.

### Size counters from state requirements

Avoid frame-index width bugs.

### Prefer synthesizer-portable HDL

Stay compatible with yosys/nextpnr where possible.

### Expose diagnostics

Clock state, FIFO state and FSM state should be readable during debugging.

### Do not overload sample bits casually

Because `I[0]` participates in TX enable with EEC enabled, "sample formatting" has control implications.

---

# 49. What was achieved

The project progressed from a difficult, poorly documented hardware/software platform into a system whose critical data path is now understood end to end.

The achievements include:

- reliable CaribouLite operation on modern Raspberry Pi systems;
- open-source FPGA modification capability;
- practical knowledge of the LVDS IQ interface;
- working TX/RX SMI streaming;
- software-side stream synchronization/recovery;
- multi-threaded real-time DSP/SMI architecture;
- working NBFM transmission and demodulation;
- successful use at 4 MSPS;
- robust register-level RF215 configuration;
- RFFC507x investigation and device identification;
- clear performance characterization on Pi Zero 2 W and Pi 4;
- diagnosis of external Pi/kernel/module cases;
- hardware-vs-software fault classification based on A/B testing;
- identification of clock/PLL behavior as a serious hardware-debug vector;
- a known-good reference configuration suitable for validating newly manufactured boards.

---

# 50. Why this matters for project revival

The public CaribouLite production files alone would make reproduction possible.

But the production files do not capture the operational knowledge required to answer questions such as:

- what should a correct LVDS stream look like?
- how should SMI synchronization be recognized?
- what does a healthy RF215 register set look like?
- how fast should the producer run?
- what does a FIFO filling to 64/64 imply?
- how does a real hardware fault differ from a driver fault?
- which clock behavior deserves suspicion?
- what constitutes a clean NBFM TX result?
- how should a reproduced board be tested against an original?

The accumulated engineering work answers many of these questions.

That makes the current project more than a board reproduction.

It creates the possibility of a maintainable, testable continuation of CaribouLite.

---

# 51. Known areas still worth documenting from source code

This document captures the engineering knowledge available from the development history, but a future source-code audit should extract and freeze the exact current implementation details for:

- final SMI timing register programming;
- exact DMA/transfer setup;
- ioctl/API structure;
- final FPGA FIFO depths;
- exact CDC implementation;
- current bitstream build options;
- exact re-anchor algorithm;
- expected marker-search behavior;
- error counters;
- current kernel compatibility patches;
- final CPU-affinity masks;
- exact SCHED_FIFO priorities;
- current shutdown sequence;
- all FPGA memory-mapped register addresses;
- the final RF215 initialization table.

Those should eventually be turned into a formal:

```text
CaribouLite FPGA/SMI Architecture Specification
```

derived directly from the source revision under review. The current driver,
firmware and TX guides linked at the top already capture several of these
details; the remaining architecture specification is still future work.

---

# 52. Historical reference settings (2026-09-12)

A compact set of values from the original history. Use the current-source
guides linked at the top for present defaults and supported configurations:

```text
Primary IQ rate:        4 MSPS
Audio rate:             48 kS/s
NBFM deviation:         2500 Hz
Baseband tone:          600 Hz
Start tone:             2525 Hz / 250 ms
Stop tone:              2475 Hz / 250 ms
TX BW cutoff:           ~80 kHz

Producer cadence:       10 ms
Pi 4 performance:       ~100 frames/s
Pi Zero 2 W:            ~75 frames/s

Software FIFO:          ~8 × 10 ms
Producer RT priority:   ~40
Writer RT priority:     ~80

FPGA sys clock:         ~64 MHz
LVDS clock:             ~64 MHz
SMI clock:              ~16 MHz

Typical TX frequency:   430.1 MHz
Typical TX power tests: -10 / -3 / 0 dBm
```

Working RF215 examples:

```text
IQIFC0 = 0x33
IQIFC1 = 0x11
IQIFC2 = 0x8B

TXDFE  = 0x81
RXDFE  = 0x81

PAC    = 0x72
PADFE  = 0x40
IRQM   = 0x3F
RF_CFG = 0x08
RF_CLKO = 0x1A

RF09 TX state = 0x04
RF24 RX state = 0x02
EEC = 1
```

Final IQ framing reference:

```text
31          30 29                    16 15          14 13                     0
+--------------+------------------------+--------------+------------------------+
| I SYNC = 10  |        I[13:0]         | Q SYNC = 01  |        Q[13:0]         |
+--------------+------------------------+--------------+------------------------+
```

and, with EEC enabled:

```text
TX_EN = I[0]
```

---

# 53. Final engineering perspective

The most important outcome is not any individual register value or HDL state.

It is the fact that the CaribouLite data path is no longer a black box.

We now understand it as an interacting system:

```text
Linux scheduling
   +
DSP cadence
   +
software FIFO
   +
SMI peripheral
   +
kernel driver
   +
FPGA FIFO
   +
clock-domain crossing
   +
LVDS framing
   +
RF215 IQ interface
   +
RF clocks / PLL
```

A failure that appears at the top can originate at any layer below it.

The project became successful when debugging stopped treating those layers independently and started testing the complete chain with known-good references and explicit instrumentation.

That systems-level understanding is one of the strongest assets available for reviving CaribouLite.
