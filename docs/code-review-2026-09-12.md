# CaribouLite code review — 2026-09-12

Reviewed revision: `6236ae1` on `dev`. Host: Raspberry Pi 4 Model B,
Linux `6.18.39+rpt-rpi-v8`, GCC 14.2.0.
This is a historical review and fix record. Its source locations, build results
and the September 16 reconciliation below refer to those dated revisions,
not a new audit of the current checkout. Later hardware-paced TX and driver
validation are recorded in the [TX design](tx-hardware-paced-design.md) and
[Pi 4 validation](pi4-tx-validation-2026-10-15.md).

The application and kernel module build successfully, but several error paths,
streaming APIs, and firmware build dependencies need correction before treating
this revision as a reproducible reference for five new boards. These findings
do not contradict Jan's working RX/TX setup: many involve alternative
APIs, partial failures, or rebuilding firmware.

This is a broad source and build review, not an exhaustive line-by-line audit or
RF validation. Detailed inspection concentrated on the application pipelines,
SMI kernel/userspace transport, C/C++ and Soapy streaming interfaces, FPGA HDL,
SPI teardown, and build/install scripts. GNU Radio integration and supporting
data structures received targeted inspection. Chip register tables, EEPROM
formats, production tooling, bundled third-party code, and examples have not
received exhaustive functional verification. Manufacturing files were outside
this code review's scope.

P1 means fix before depending on the affected path; P2 means a concrete
correctness/reproducibility issue to address next. Findings below are ordered
by priority, with source locations referring to the reviewed revision.

## Current status — reconciled 2026-09-16

This section supersedes historical pending-work statements below. Reconciled
against commits through `a311b17`, current FPGA source and the
[FPGA study and promotion record](fpga-gap-study-2026-09-13.md).
“Fixed” describes the reported defect, not exhaustive hardware validation.

| Issues | Current status |
| --- | --- |
| 1–9 | Fixes committed; validation and API-specific limits are recorded below. |
| 10 | Fixed: sample-gap and debug-loopback outputs are connected, validated and promoted. |
| 11 | Fixed in `4b36a25`: TX reset now selects the declared `IDLE` state. |
| 12–13 | Fixes committed, with build/failure-path and recorded live checks as applicable. |
| 14 | Fixed in `cac73f2`; physical Soapy RX checks recorded, physical Soapy TX validation deferred. |
| 15 | Fixed in `9b4d977`; builds with/without Soapy and temporary installation passed. |
| 16 | Fixed in `a311b17`; installer failure-path checks and real module build passed; system installation not performed. |

All numbered defects now have fixes. Jan confirmed successful debug-loopback
tests, and the exact tested candidate has been promoted with both embedded
headers and the loopback RTL bench. General implicit-net checking remains a
follow-up from issue 11. Historical status entries below describe each stage;
see the study's 2026-09-16 promotion entry for the current firmware.

## Findings

### 1. P1 — Multiple opens can leave the kernel using freed FIFO memory

`driver/smi_stream_dev.c:1261–1323`

Every `open()` allocates buffers into the same global `inst` and reinitializes
its FIFOs. There is no exclusive-open guard or per-file ownership. Opening a
second client replaces the first client's buffers; closing either client frees
the current buffers even while the other descriptor remains usable. Subsequent
reads/writes through that descriptor can access freed kernel memory. Opening
while DMA runs also reinitializes FIFO state before stopping the old stream.

Reject concurrent opens with `-EBUSY`, or implement explicit shared ownership
and serialize buffer lifetime with stream shutdown. Source-confirmed; a
multi-client test was deliberately not run against the live driver.

Update 2026-09-13: implemented exclusive open using a mutex and ownership flag.
The mutex covers buffer allocation and final release; another open returns
`-EBUSY` before touching shared state. Allocation failures release the mutex
and free any partial allocation. Final release stops streaming and frees the
buffers before allowing a new owner. Duplicated/inherited descriptors share
the same open file description and retain ownership until its final release.

Validation: `python3 driver/tests/test_exclusive_open.py` passed. It compiles
the actual open/release function bodies with mocked kernel services and checks
second-open rejection without changing the owner's buffers or stream,
both allocation failure paths and recovery, exclusion during allocation and
cleanup, and 100 rounds of 16 concurrent opens with exactly one winner.
A fresh module build in `/tmp/cariboulite-issue1-driver` passed for
`6.18.39+rpt-rpi-v8`. The new module has not been installed or loaded;
live exclusive-open and post-change radio validation remain pending.

Live follow-up 2026-09-13: Jan loaded the new module (sysfs source version
`12F9D2980B1137B92139E7F`) and started a second test application while the first
was transmitting through option 11. The second application's log confirms
`/dev/smi` open failed with `Device or resource busy`. However, Jan
reported TX stopped and the first application appeared hung. Startup reaches
`cariboulite_setup_io()` before attempting SMI open; that function drives the
modem and mixer reset pins low. Thus the kernel guard rejects the second
client too late to prevent application-level hardware interference. An early
ownership claim before any hardware setup, retained through hardware cleanup,
is needed in the library as well. The exact hang mechanism is not established.
Captured logs are local under `installations/issue1-validation/`.

Follow-up implementation: library initialization now claims `/dev/smi` before
signal registration, board detection, GPIO setup, or FPGA communication.
SMI initialization duplicates this descriptor with close-on-exec semantics;
it does not perform a second open. Janship descriptor stays open until
hardware cleanup finishes, including initialization failure paths. SMI close
explicitly stops streaming because closing its duplicate alone no longer
invokes the kernel's final release. Production/minimal initialization paths
also require the SMI device to be available for this ownership claim.

Validation of the library follow-up:

- Complete local application/library/Soapy build passed.
- `python3 software/libcariboulite/tests/test_early_ownership.py` passed against
  the rebuilt application with interposed device/GPIO calls: busy rejection
  makes no GPIO setup call; injected GPIO setup failure releases ownership.
- The kernel open/release regression tests still pass.
- Live test with `/dev/smi` held open: the rebuilt application exited before
  GPIO setup with "hardware setup skipped". A duplicated descriptor retained
  ownership after closing the original; reopen succeeded after final close.
- The rebuilt single application reached the menu and quit with exit code 0.
  No transmit option was selected during these follow-up checks.

Use the rebuilt `build/cariboulite_test_app` for both sessions in the next
TX regression check, with separate stderr log files. Installed libraries under
`/usr/local` have not been replaced; older applications/libraries can still
touch hardware before their SMI open is rejected. The original two-application
TX scenario and reported hang had not yet been revalidated at that point.

Validation by Jan 2026-09-13, 16:14 local time: the repeated two-session TX
test passed. The first application logged TX activation at 16:14:27.954.
At 16:14:42.110 the second application was rejected with `Device or resource
busy; hardware setup skipped`, with no subsequent hardware initialization.
The first application logged TX deactivation at 16:14:52.392, then a normal
menu quit and completed driver release at 16:14:57.399. Jan confirmed
the test worked; the first log contains no error or warning entries.
Preserved logs: `installations/issue1-validation/passed-first.log` and
`passed-second.log`. Issue 1 is implemented and validated for this scenario;
permanent installation and committing the changes remain separate steps.

Permanent driver installation completed 2026-09-13 for the running kernel
`6.18.39+rpt-rpi-v8` only. Installed the tested compressed module at
`/lib/modules/6.18.39+rpt-rpi-v8/kernel/drivers/char/broadcom/smi_stream_dev.ko.xz`
and successfully ran `depmod -a 6.18.39+rpt-rpi-v8`. Verified the installed
bytes against the tested module and source version against the already loaded
module (`12F9D2980B1137B92139E7F`). `modprobe --show-depends` resolves the base
SMI dependency and existing parameters `6, 2, 3`. Boot module loading was
already configured. No reload or reboot was performed.
Rollback copy and installation hashes are under `installations/issue1-driver/`.
Installed userspace libraries remain unchanged; use the rebuilt local app.

### 2. P1 — RX destruction uses an uncreated or already joined thread

`software/libcariboulite/src/app_menu.c:1987–2044`

RX initialization starts audio and demodulation workers, but creates the reader
only on RX start. Destroy nevertheless unconditionally cancels and joins the
reader. Entering the RX menu and quitting without starting RX therefore uses
the zero-initialized thread handle. After RX has run, `rx_pipeline_stop()` joins
the reader and destroy cancels/joins the same handle again. These operations
have invalid thread lifetime assumptions and can crash or target a reused
handle. Reader allocation and `pthread_create()` failures are also unchecked.

Track successful creation for each worker, clear that state after join, and
make stop/destroy safe after any partial initialization. Thread cancellation
also needs cleanup handlers for FIFO mutex ownership and reader metadata.

Update 2026-09-13: RX tracks successful creation of the reader, demodulator,
and audio writer, and clears each flag after joining. Stop/destroy no longer
cancel or join an uncreated or previously joined reader. RX initialization
unwinds partially created workers and allocated resources; reader sample-buffer
allocation and thread-creation failures are checked, with hardware shutdown
after a failed thread start. FIFO waits now install mutex-unlock cancellation
handlers, and the reader frees its metadata through a cancellation handler.
These FIFO helpers are shared with TX; their timeout-clock behavior (issue 8)
is unchanged. RX partial-initialization cleanup overlaps the RX portion of
issue 9; TX and combined-monitor initialization issues remain outstanding.

Validation: full local build passed. Run
`python3 software/libcariboulite/tests/test_rx_lifecycle.py` for tests using
the actual app implementation, mocked radio/thread-lifecycle calls and ALSA
null playback. Cases cover never-started destruction, 20 start/stop cycles,
destroy while running, allocation and worker-creation failures, retry after
failed start, missing ALSA device, real cancellation of all four FIFO wait
paths, and reader metadata cleanup on cancellation.

Live Pi validation passed: option 12 then return before first RX; RX on/off/on;
return while RX is active; application quit with exit code 0. No TX option was
selected and received-audio quality was not assessed. Log preserved locally
at `installations/issue2-validation/rx-lifecycle.log`. The rebuilt local app
contains this change; no driver change or reload was needed for issue 2.

Audio validation by Jan: option 14 RX played through the USB speaker, while
option 12 initially did not. Option 12 still targeted `plughw:3,0` (the Pi
headphone output), a pre-existing setting. Changed only its playback destination
to `plughw:Loopback,0,0` and rebuilt. Jan confirmed option 12 RX audio now
works with the existing `arecord` loopback-to-USB-speaker routing command in
`ADDITIONAL-README.md`. The radio-path selection was unchanged.

### 3. P1 — C++ synchronous reads can overflow internal buffers

`software/libcariboulite/src/CaribouLiteRadioCpp.cpp:108–126, 178–194`

The constructor allocates one MTU of sample and metadata storage. `ReadSamples`
passes unrestricted `num_to_read` to the C reader using that storage. The C
reader processes the requested length in chunks; it does not cap the total to
one MTU. An oversized request writes beyond both internal allocations. The
float overload uses the same path. GNU Radio's `work()` forwards
`noutput_items` without applying its stored MTU limit.

Clamp the request, resize the buffers, or process bounded chunks. An isolated
test of the actual wrapper body with a mock reader and a protected guard page
reproduced SIGSEGV for a five-sample request into four-sample internal storage.
No board access was involved.

Update 2026-09-13: the C++ synchronous wrapper records the allocated read
capacity and caps each C-reader request to that capacity. Both integer and
float overloads return the actual count; the public header documents the
one-MTU-per-call limit. GNU Radio's existing work function returns that count,
so no change to its caller is needed. A zero-capacity wrapper returns without
accessing synchronous buffers.

Validation: `python3 software/libcariboulite/tests/test_cpp_read_bounds.py`
passed using the actual read-method bodies, a mock C reader, and inaccessible
guard pages immediately after the sample and metadata allocations. Cases
include below/at/above capacity, `SIZE_MAX`, integer/float conversion, metadata,
short reads, transport errors, zero-length requests and inactive/empty buffers.
The complete local C/C++ build passed. No hardware test was run for this fix;
the menu app's C RX path does not exercise these C++ wrapper methods.

### 4. P1 — Kernel TX transition takes a sleeping mutex under a spinlock

`driver/smi_stream_dev.c:359–375`

`set_state()` holds `state_lock` while acquiring `write_lock` with
`mutex_lock_interruptible()`. The write path holds that mutex across
`kfifo_from_user()`, which can fault/sleep. A concurrent TX transition can
therefore sleep in atomic context. Moving DMA shutdown outside the spinlock
has not removed this remaining problem.

Serialize the whole transition with a process-context mutex and use spinlocks
only for short state updates that cannot sleep. Also serialize concurrent
ioctl transitions across stop/start, not just the final state assignment.
Source-confirmed; no live contention test was run.

Update 2026-09-13: replaced the transition spinlock with a process-context
mutex held across the state check, TX helper cancellation, DMA shutdown,
FIFO reset, DMA startup and helper startup. Removed the ioctl's unlocked
same-state shortcut. TX timer/work helpers are cancelled before DMA shutdown
to prevent rearming during teardown. Callback state observations remain
lockless, with `WRITE_ONCE` state updates; callbacks do not take the transition
mutex. Lock order is open ownership, transition, then writer mutex. Interrupted
writer-lock acquisition restores the idle hardware address before returning.

Validation: `python3 driver/tests/test_stream_transitions.py` passed against
the actual transition body with simulated hardware and real pthread mutexes.
It covers concurrent transitions, writer contention, redundant requests,
startup/interrupted-lock failures and helper shutdown ordering. Exclusive-open
regression tests also passed. A fresh kernel-module build in
`/tmp/cariboulite-issue4-driver` passed for `6.18.39+rpt-rpi-v8`.
This module has not been installed or loaded; live RX/TX switching and kernel
diagnostics remain to be checked. Userspace rebuilding is not needed.

Live follow-up: temporarily loaded source version `697F3CF2096C282E312BA14`
with parameters `6, 2, 3`. Option 12 RX on/off/on succeeded, followed by
option 11 TX at 430.099936 MHz and -3 dBm for approximately 3.65 seconds,
then option 12 RX again. The application quit with exit code 0. Kernel logs
for the application test contained no warning, error, sleeping-in-atomic,
oops or deadlock messages. This was sequential live switching; concurrent
contention was tested with the simulated-hardware regression harness above.
Logs are preserved under `installations/issue4-validation/`. The new module
remains temporarily loaded; the installed module is still the issue 1 version.

Jan subsequently confirmed hearing both the TX tone and RX chain
activation. Permanent installation completed for `6.18.39+rpt-rpi-v8` on
2026-09-13: installed the tested issue 4 module, ran `depmod` successfully,
and verified installed bytes and source version against the tested artifact.
The same module was already loaded, so no reload was needed. Existing module
parameters remain `6, 2, 3`; dependency resolution was verified. The previous
installed module and installation hashes are in `installations/issue4-driver/`.

### 5. P1 — TX stop can wait forever before disabling transmission

`software/libcariboulite/src/app_menu.c:1762–1774, 1844–1862, 2758–2762`

Stopping TX first waits for the DSP worker to consume injected tail-tone
frames. That wait has no deadline or worker-failure check. If the TX writer
has set `nbfm_tx_active=false` after a hard error, the producer stops consuming
injections, but the pipeline's `running` flag remains true. A subsequent stop
or quit waits forever. A producer blocked on audio capture or a full TX FIFO
can cause the same problem. Hardware shutdown occurs only after that wait.

Give tone injection a bounded wait and a failure/cancellation path; always
complete hardware shutdown even when the tail tone cannot be delivered.

Update 2026-09-13: tone injection now has one monotonic one-second budget
across silence/tone/silence stages, and aborts when TX or either worker is
inactive. Failed tail injection skips the FIFO drain wait and proceeds to
hardware shutdown. Successful injection retains the 600 ms drain deadline,
which now also exits on worker failure. Failed start-tone injection stops TX
and returns an error. An injection mutex serializes publication, consumption,
and cancellation of pending frames. The bound applies to tone/drain waiting,
not to the execution time of hardware shutdown calls themselves.

`python3 software/libcariboulite/tests/test_tx_stop_deadline.py` passes using
the actual tone/stop code with simulated time and hardware: normal completion,
stalled consumption, mid-wait TX failure, inactive workers, full-FIFO drain
timeout and repeated stop. The local application build passes. Live tone and
RX/TX switching validation of this update remains pending.

Validation by Jan: tested option 11 (TX tone), option 12 (S1G RX), and option
14's TX and RX controls with no problems reported. This confirms normal live
operation with the update; stalled-worker and timeout behavior were exercised
by the simulated failure-path tests above.

### 6. P1 — Partial streaming writes are omitted from the returned count

`software/libcariboulite/src/caribou_smi/caribou_smi.c:1086–1128`

`caribou_smi_write_samples()` tracks progress inside the current chunk in
`off`, but four subsequent timeouts jump directly to `done`, bypassing the
update of `consumed_samples`. The hard-error path has the same accounting
problem. The caller retries samples already queued to DMA, corrupting the
sample sequence under backpressure.

A mock-I/O test using the actual function accepted two samples and then
returned four timeouts: the function reported zero samples consumed. Include
current-chunk progress in every return path and retain any partial-byte state.

Update 2026-09-13: streaming TX now accounts for completed samples in the
current chunk before returning on timeout or error. Byte offsets advance by
the actual accepted byte count. An incomplete sample's sent prefix is retained
in the device context; the caller retries from the first unreported sample,
and the writer skips that prefix. Successful stream transitions clear the
saved offset. The header documents retry behavior and serialization of writes
with stream transitions. The older public TX function in issue 7 is unchanged.

`python3 software/libcariboulite/tests/test_tx_write_progress.py` passes using
the actual function with scripted I/O. It checks timeout/error exits at every
partial-sample offset, exact bytes across retries, and accounting across
multiple chunks. Live TX validation remains pending.

The complete local build passed, and Jan subsequently reported successful
live TX tests. Issue 6 is validated by both the scripted partial-I/O tests and
Jan's normal-operation TX checks.

### 7. P2 — Public TX API inserts samples and reports more than requested

`software/libcariboulite/src/caribou_smi/caribou_smi.c:903–981`

The public C radio API calls `caribou_smi_write()`, which pads every short DMA
quarter with repeated samples and counts the padding as caller data consumed.
This changes the waveform and violates the write-count contract. C++ and
Soapy callers inherit it; the custom app's newer writer uses a different
function and is affected by finding 6 instead.

With a mocked 64-byte native batch, requesting one sample emits four and
returns four. Preserve tails between calls, or support arbitrary aligned
writes, and return only the caller's samples actually accepted.

Update 2026-09-13: the public `caribou_smi_write` entry point now delegates
to `caribou_smi_write_samples`, reusing issue 6's partial-write accounting
instead of padding to a DMA quarter. Empty writes return zero; requests larger
than `INT_MAX` are capped to match the signed return type. Short-write retry
semantics are documented in the header.

Extended `software/libcariboulite/tests/test_tx_write_progress.py` to cover
public writes of 1–20 samples with a 64-byte native batch, exact output bytes,
consecutive short calls, empty requests, and partial-byte retry through the
public entry point. These and the existing progress tests passed, as did the
complete local build. No live public-API TX test was performed; menu options
11/14 already use the newer streaming writer directly.

Jan subsequently reported a successful TX check. The specific API path
was not identified; public-API byte/count behavior is covered by the automated
tests above.

### 8. P2 — FIFO timeout deadlines use the wrong clock

`software/libcariboulite/src/app_menu.c:943–984, 1232–1234, 1291–1346`

Audio and RF condition variables are initialized with default attributes
(realtime clock), while their timed waits construct absolute deadlines from
`CLOCK_MONOTONIC`. The deadlines are already expired in the realtime clock,
so full/empty FIFO waits return immediately instead of honoring the requested
timeout. Infinite waits are unaffected.

An extracted audio-FIFO test requested 100 ms and returned in approximately
0.060 ms. Set the condition-variable clock to monotonic or calculate deadlines
using its actual configured clock.

Update 2026-09-13: both audio and RF FIFO condition variables now explicitly
use `CLOCK_MONOTONIC`, matching their timed-wait deadlines. Removed the
premature clock fallback macro before the time header. Extended the existing
RX lifecycle harness with real 100 ms waits for empty reads and full writes
on both FIFOs: all four measured approximately 100.1 ms. Lifecycle and
cancellation checks still passed, and the local application build passed.
Live RX/TX validation remains pending; no driver change is required.

Jan subsequently confirmed that all TX and RX tests passed with this
update. Issue 8 has automated timeout/lifecycle coverage and successful live
normal-operation validation.

### 9. P2 — Partial pipeline initialization is not unwound

`software/libcariboulite/src/app_menu.c:1749–1755, 1921–1943, 3113–3114`

TX and RX initialization return directly on thread-creation failures after
allocating resources and potentially starting earlier workers. `inited` is
still false, so the normal destroy routines do not clean up. In the standalone
menus, returning after such a failure can leave a worker referencing a pipeline
object that was on the caller's stack. The combined monitor ignores both init
return values and proceeds to display statistics from possibly destroyed FIFOs
when opening its hard-coded ALSA devices fails.

Use staged cleanup and check both initialization results before entering the
monitor. Exercise missing ALSA devices and injected thread/allocation failures.

Update 2026-09-13: completed the remaining TX/monitor work after issue 2's RX
cleanup. TX now tracks successful worker creation and uses one staged cleanup
path for FIFO, modulator, audio buffers, capture device and thread failures.
Destroy joins only created workers and clears owned pointers. Option 14's
startup helper checks both initialization results, cleans up TX if RX fails,
and prevents entering ncurses or reading statistics from failed pipelines.

The expanded `test_rx_lifecycle.py` harness covers TX allocation failures,
both TX worker-creation failures, missing capture device, repeated cleanup,
successful retry, and monitor failures at each of its four worker creations.
Existing RX lifecycle, FIFO timing and cancellation tests also pass. The full
local build passed. Live normal-operation validation remains pending.

Jan subsequently completed TX and RX checks with no problems reported.
Issue 9 has passing failure-injection tests and successful live
normal-operation validation.

### 10. P2 — FPGA TX configuration outputs are disconnected

`firmware/sys_ctrl.v:18–19, 57–59, 72–85`; `firmware/top.v:168–169, 449–451`

`debug_loopback_tx` and `tx_sample_gap` are written and, for the gap, readable
over SPI, but never assigned to their corresponding output ports. The LVDS
transmitter consumes those undriven ports. Software can read back a changed
gap register without that value reaching the transmitter.

Yosys confirms undriven `i_sample_gap` and `i_debug_lb` in the flattened design.
Connect the registers to the outputs and verify gap/loopback behavior in HDL
tests before programming hardware.

Update 2026-09-15 (reconciliation): **partially fixed** in `4b36a25`.
`sys_ctrl.v` now assigns `o_tx_sample_gap = tx_sample_gap`; `top.v` routes
that output to the LVDS transmitter. The promoted FPGA and app support
selectable 2/4 MS/s TX. The study records sequence/stop simulations across
all gap values, synthesis/routing acceptance and user-confirmed TX/RX and
rate-switching tests. See the linked study's promotion section for the exact
image and limitations.

`o_debug_loopback_tx` still has no driver, although `top.v` connects it to
`lvds_tx.i_debug_lb`. That portion remains open. Existing TX benches tie
debug input low; successful gap tests do not establish loopback behavior.

Single-attempt follow-up 2026-09-15: an isolated copy with the loopback
assignment added passed synthesis, seed-16 placement/routing, timing and
packing on its first and only build. Final LVDS Fmax was 70.19 MHz (required
64 MHz); system Fmax was 83.50 MHz (required 62.5 MHz). The candidate and
logs are preserved in `installations/loopback-one-try/`; see the FPGA study's
single-attempt section. No production HDL/image was changed or programmed.
At that stage, loopback functional validation and promotion remained
outstanding; the subsequent functional check is recorded below.

Functional follow-up 2026-09-15: the isolated candidate passed a combined
sys_ctrl/LVDS TX bench covering register decoding and 1,024 gap/phase/TX/FIFO
combinations, exact serialized debug frames, no settled-loopback FIFO pulls,
exit/resumption and reset. Existing sequence/stop/FIFO-reset benches also
passed against candidate modules. Debug mode persists independently of TX
enable until its register bit is cleared or reset. See the FPGA study for
logs and limits. Candidate RTL behavior is now checked; hardware validation
and promotion remain outstanding. The production loopback output is still
unconnected, so issue 10 remains open.

Menu follow-up 2026-09-15: menu 14 now has an `L` interface-loopback toggle
and fresh decoded I/Q preview for testing the candidate. Setup disables EEC,
verifies RF09 off/RF24 RX, and enables modem EXTLB plus the FPGA pattern;
normal TX/RX/rate controls are blocked until successful cleanup. New mocked
controller and FPGA SPI tests, lifecycle regression and full build pass.
See the study's manual procedure and limits. No candidate was loaded or
hardware test performed; the production FPGA connection remains unchanged.

Hardware confirmation by Jan 2026-09-15: menu 14 displayed 1,412,895 captured
samples, zero read timeouts, and 16/16 preview pairs matching expected HiF
I=`F824`, Q=`0201`. This confirms the candidate's fixed-pattern interface
loopback through the receive decoder. The log records several successful
loopback cleanup cycles; final signal termination did not record monitor
cleanup. See the study for evidence and limits. Candidate hardware pattern
validation is now successful; production-image promotion and commit remain
outstanding, so the committed FPGA output is still unconnected.

### 11. P2 — LVDS TX reset refers to an undefined state

`firmware/lvds_tx.v:76`

Reset assigns `r_state <= INIT`, but the defined states are `IDLE`, `TX_FRAME`,
`TX_GAP`, and `LOOPBACK`. Verilog implicitly creates an undriven `INIT` wire.
Yosys reports both the implicit wire and a nonconstant asynchronous reset.
The default case may eventually recover, but the reset state is not defined
as intended. Use a declared reset state and enable checks for implicit nets.

Update 2026-09-15 (reconciliation): **fixed** in `4b36a25`.
`firmware/lvds_tx.v` resets `r_state` to the declared `IDLE` constant.
The same commit corrected the variable part-select to legal indexed syntax
and promoted the tested FPGA image. The original undefined-`INIT` finding
no longer applies to the current source.

The current sequence and active-stop simulations exercise TX after reset;
both passed again during reconciliation. The separate `tb_reset.v` also
passed, but tests the FIFO reset-clock behavior, not the LVDS TX state
register directly. Historical synthesis/routing and hardware validation are
recorded in the FPGA study. The earlier gap/reset promotion used SHA-256:
`a90a908f0e4e57a7a2f5e26e97303a3f43cadcaeb5b1be39eca4a604e0e00bfe`.
No general implicit-net rejection policy was added by that commit; broader
lint coverage remains a follow-up rather than an unfixed reset assignment.
No FPGA was programmed during this reconciliation.

### 12. P2 — Editing HDL does not trigger rebuilding the bitstream

`firmware/Makefile:5–17`

The `top.bin` target has no prerequisites. When it exists, changing an included
HDL file or `io.pcf` does not rebuild it; `build` can then regenerate and copy
a header from the stale binary. A dry run with `make -n -W lvds_tx.v top.bin`
reported that the binary was up to date.

Declare HDL/constraint dependencies and separate synthesis, routing, packing,
and header generation with proper dependencies. Routing is additionally piped
through `tee` without preserving its failure status, and uses
`--timing-allow-fail`; a nominally successful build is not timing acceptance.

Update 2026-09-14: the Makefile now declares separate dependency stages for
HDL synthesis, constrained routing, packing and both firmware headers. GNU
Make grouped targets track multi-output stages. Routing no longer allows
timing failure; tool failures propagate directly and temporary outputs are
published only after successful completion. The Makefile itself invalidates
build outputs when its recipe changes. Default `build` never programs hardware.

Validation: `python3 firmware/tests/test_build.py` passes dependency, no-op,
parallel-build, missing-report and injected failure tests at every tool stage.
A full isolated real-tool build under `/tmp/cariboulite-issue12` passed, with
final LVDS Fmax 69.57 MHz against 64 MHz and system Fmax 80.90 MHz against
62.5 MHz. A second invocation performed no work. Both generated header
payloads match the isolated binary. The rebuilt binary differs from the
previous hardware-tested image; it was not promoted or programmed. Hashes
confirm the repository's validated binary and headers were untouched.
No HDL logic, driver, or running FPGA changes were made. Issue 12's build
workflow is corrected; Jan authorized committing the fix after review.

### 13. P2 — SPI close misinterprets timed locking and destroys a locked mutex

`software/libcariboulite/src/io_utils/io_utils_spi.c:346–361`

The timeout is the absolute timestamp one second after the Unix epoch rather
than one second from now. The code checks for negative pthread error values,
although pthread errors are positive. If another thread holds the mutex,
timeout is ignored and resources can be freed while in use. If locking succeeds,
the mutex is destroyed while still locked.

Coordinate shutdown with users, use a valid absolute deadline and correct error
checks, then release ownership before destroying the mutex.

Update 2026-09-14: SPI API entry points now protect device-mutex lifetime
with a process-wide read/write lifecycle lock, without changing the public
struct layout. Ordinary operations share lifecycle access; init, suspend and
close exclude it. Close uses CLOCK_REALTIME plus one second for its absolute
deadline and treats every nonzero pthread result as failure. Timeout leaves
the SPI device unchanged. Successful close frees chips under the device mutex,
unlocks, then destroys that mutex while API entry remains excluded. Calls
arriving after close safely observe the uninitialized device. Cancellation
is deferred until API locks are released. Lock order is lifecycle then device.

Repeated initialization now returns without reinitializing a live mutex;
the invalid post-init unlock was removed. Cleanup visits all initialized chip
slots, including holes left by removal. Transmit validates chip state under
the same device lock as removal. Owners must still preserve device storage
for all callers and stop users/retry if close times out; the lifecycle writer
lock can also wait for calls on another SPI device in this process.

Validation: `test_spi_close.py` compiles the actual SPI implementation with
mocked GPIO/SPI I/O and real pthread locks. It checks a one-second timeout
with resources preserved, successful retry after an in-flight transaction,
sparse chip cleanup, post-close rejection and 20 init/close cycles. The local
build, early-ownership regression and diff checks pass. The rebuilt app
initialized on the Pi and quit normally (exit zero), with no RF transmission.
Logs are in `/tmp/issue13-*.log`. The user subsequently confirmed all
requested TX/RX and exit tests passed and authorized committing the fix.
No FPGA or driver changes were made.

### 14. P2 — Soapy synchronous streaming ignores timeout and inactive state

`software/libcariboulite/src/soapy_api/CaribouliteStream.cpp:192–209, 290–309`

With `USE_ASYNC` disabled, reads/writes delegate directly to C functions without
using `timeout_us` or checking stream activation. Negative read errors are
converted to zero. This does not implement the timeout/error behavior documented
in the adjacent Soapy stream interface and can make callers spin or wait beyond
their requested deadline.

**Fixed and committed in `cac73f2` (2026-09-14).** The synchronous Soapy stream now
starts inactive, waits out inactive calls without touching the transport, and
becomes active only after successful hardware activation. Setup, close, and
deactivation clear its active state. Unsupported timed/burst flags are rejected;
RX output flags/timestamps are cleared because timestamps are not supplied.

New timed C/SMI entry points carry a monotonic deadline down to FIFO waiting,
including interrupted waits. They return up to one native batch and preserve
partial TX sample prefixes across retries. Existing C entry points retain their
behaviour. Soapy maps zero progress to TIMEOUT, transport failures to STREAM_ERROR,
and RX synchronization failures to CORRUPTION. The unused asynchronous reader
implementation was removed so there is one supported streaming path.

Validation: `python3 software/libcariboulite/tests/test_soapy_stream.py` compiles
actual Soapy and SMI sources with simulated radio/syscalls. Covers inactive
waits, failed activation/deactivation, restart/close, direction and flag checks,
format conversion, short counts, errors, zero/short deadlines, interrupted waits,
and byte-prefix retries. No radio is opened. Full library/app/Soapy rebuild and
legacy TX progress regression pass.

Physical RX follow-up (2026-09-14): the user reports repeated stream start/stop
and client disconnect/reconnect succeeded through SDR++ server over LAN from
macOS. `/tmp/sdrpp-soapy-test.log` shows HiF/CF32 RX setup and repeated starts and
stops; the updated "Creating stream MTU" message confirms the new stream code
was used. No stream errors were logged. The server had already stopped when
reviewed, so its loaded module path and shutdown status could not be inspected.
Startup includes ALSA device-probing warnings and a rounded-rate warning for
1333000 despite SDR++ displaying 2000000; this test does not establish the actual
sample rate or audio quality. Physical Soapy TX validation remains deferred.

### 15. P2 — Optional Soapy dependency removes the main application targets

`software/libcariboulite/CMakeLists.txt:98–102, 148–199`

When SoapySDR is missing, the top-level `return()` also skips creation of
`cariboulite_test_app`, utilities, and library installation rules. Configuration
only warns that Soapy support is being skipped, so this is misleading on a
fresh machine intended to run the standalone app.

Make only the Soapy module conditional. Keep the app, utilities, and library
installation independent of that optional dependency.

Update 2026-09-15: only Soapy module discovery-dependent setup and creation
are conditional now. Standalone application, utilities, libraries and their
installation rules remain available when SoapySDR is absent. Library/utility
installation and pkg-config metadata use `CMAKE_INSTALL_PREFIX` (default
`/usr/local`) independently of the Soapy module's search path.

Validation: fresh full builds passed with SoapySDR available and with
`-DCMAKE_DISABLE_FIND_PACKAGE_SoapySDR=TRUE`. The latter produced the app,
utilities and both libraries without a Soapy module. Installation to
`/tmp/cariboulite-issue15-install` passed; all manifest destinations stayed
within that prefix, including headers and pkg-config metadata. The enabled
build produced the Soapy module. `git diff --check` passed. Build directories
and logs are under `/tmp/cariboulite-issue15-*`. No system installation or
hardware test was performed.

### 16. P2 — Driver installer copies one kernel's module into other kernels

`driver/install.sh:50–72`

The build targets the running kernel, but installation searches all of
`/lib/modules` for base SMI modules and copies the same generated `.ko.xz` into
every matching directory. Hosts retaining multiple kernels receive a module
with the wrong version in the other kernels' trees.

Resolve the destination strictly within the kernel version used for compilation.
Also stop immediately on compilation/packaging failures before changing module
blacklists and boot-loading configuration. Installation was reviewed, not run.

Update 2026-09-15: the installer captures the running kernel release once,
passes that release and its `/lib/modules/<release>/build` headers to CMake,
and verifies the compiled module's vermagic release before installation.
CMake accepts the explicit release while retaining running-kernel discovery
for ordinary builds. Destination discovery searches only that kernel's tree,
requires one unique base-module directory, and runs `depmod -a <release>`.
Script paths are anchored to the installer location and quoted.

The installer now stops on command and pipeline failures. Compilation,
module verification and compression complete before copying the installed
module or changing blacklist/boot configuration. Compression writes a temporary
file before publishing its result. Failed copy or depmod also prevents later
configuration updates; installation is not transactional if a later step fails.

Validation: `python3 driver/tests/test_install.py` passed 13 mocked installer
scenarios, including successful installation, dependency/configure/build errors,
module metadata errors and kernel mismatch, failed/missing/ambiguous destination
lookup, compression/blob generation failures, copy failure and depmod failure.
The harness executes the actual script from outside its directory, with spaces
in its checkout path, and intercepts all privileged commands. It verifies the
single-kernel destination, explicit build parameters and depmod target, and
that early failures cannot write boot configuration or report success.
A fresh real module build in `/tmp/cariboulite-issue16-driver` passed for
`6.18.39+rpt-rpi-v8`; `modinfo` confirms matching vermagic. Shell syntax and
`git diff --check` passed. No system installation, driver reload, reboot or
RF operation was performed.

## Verification performed

- Fresh C/C++ configure and complete default build in
  `/tmp/cariboulite-review-build`: passed, including app, static/shared libraries,
  utilities, and Soapy module.
- Fresh kernel module build in `/tmp/cariboulite-review-driver`: passed and
  produced `smi_stream_dev.ko` for the running kernel. It was not installed.
- Yosys hierarchy/process checks with iCE40 primitive definitions, and actual
  `synth_ice40`: completed, with the warnings described above. Synthesis later
  optimizes undriven nets away; its final zero-problem check does not invalidate
  those earlier diagnostics. Place-and-route/timing closure was not performed.
- Parsed all 30 tracked Python/shell scripts with Python AST or `bash -n`:
  no syntax errors. This does not test script behavior or dependencies.
- `ctest -N` in the fresh main build: zero registered tests.
- Isolated source-body reproductions: FIFO timeout mismatch, public TX count
  inflation, streaming TX partial-count loss, and C++ oversized-read fault.
  Harnesses and binaries are under `/tmp/cariboulite-review-checks`.
- AddressSanitizer could not initialize its address space in this execution
  environment. The C++ boundary test instead used an OS-protected guard page
  and a mock radio read; it terminated with SIGSEGV as expected.
- Build-time `SoapySDRUtil --info` loaded the installed plugin and printed a
  missing `/dev/gpiomem` message in this tool environment. That is not evidence
  that Jan's board/setup is broken. No RF application was intentionally
  launched, no transmission was requested, and no firmware was programmed.

## Original next-work recommendations (2026-09-12)

Historical review guidance; use the current-status section above for the
remaining numbered work. The baseline capture and subsequent fixes described
below have already addressed much of this original plan.

First preserve the exact working bitstream and installed software/module hashes.
Then fix lifecycle and transport accounting in small changes, verifying each
against the reference setup. Add automated tests for the failures above before
refactoring the large app menu. Treat FPGA wiring/build fixes as a separate
change requiring HDL tests and subsequent board validation.

Further hardware-focused checks should cover repeated RX/TX switching, clean
quit before first RX, missing audio devices, backpressure, restart without power
cycling, both radio paths, and sustained streaming. The source also contains
historical code and control/register assumptions that need comparison against
the actual working bitstream; this review does not certify RF register values,
clock-domain timing, or spectral performance.

At the original review date, only this review document was added to the repository. Functional source,
existing build outputs, branch history, and hardware configuration were not
changed.
