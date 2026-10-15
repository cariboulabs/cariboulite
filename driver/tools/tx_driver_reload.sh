#!/usr/bin/env bash
# Runtime-only TX-demand driver reload. Installed modules remain untouched.
# Run on the target Pi, with every radio/controller application stopped:
# sudo bash driver/tools/tx_driver_reload.sh /absolute/path/smi_stream_dev.ko
set -Eeuo pipefail

check_only=0
if [[ ${1:-} == --check ]]; then
    check_only=1
    shift
fi
if [[ $# != 1 ]]; then
    echo "Usage: $0 [--check] /path/to/smi_stream_dev.ko" >&2
    exit 2
fi
candidate=$(realpath "$1")
module=smi_stream_dev
kernel=$(uname -r)
[[ -f $candidate ]] || { echo "Candidate module not found: $candidate" >&2; exit 1; }
[[ $(modinfo -F name "$candidate") == "$module" ]] || { echo "Unexpected module name" >&2; exit 1; }
vermagic=$(modinfo -F vermagic "$candidate")
[[ ${vermagic%% *} == "$kernel" ]] || {
    echo "Kernel mismatch: candidate ${vermagic%% *}, running $kernel" >&2
    exit 1
}
installed=$(modinfo -n "$module")
[[ -f $installed ]] || { echo "No installed module available for rollback: $installed" >&2; exit 1; }
[[ ${candidate} != "$(realpath "$installed")" ]] || { echo "Use a staged candidate; leave installed module untouched" >&2; exit 1; }
printf 'Candidate: %s\nKernel: %s\nRollback source: %s\n' "$candidate" "$kernel" "$installed"
sha256sum "$candidate"
if (( check_only )); then
    echo "Metadata checked. No device was opened and no module was changed."
    exit 0
fi
[[ $EUID == 0 ]] || { echo "Run this reload command with sudo in the Pi Zero terminal" >&2; exit 1; }
for program in fuser python3 rmmod insmod; do
    command -v "$program" >/dev/null || { echo "Missing program: $program" >&2; exit 1; }
done
[[ -c /dev/smi ]] || { echo "/dev/smi is missing" >&2; exit 1; }
if fuser /dev/smi >/dev/null 2>&1; then
    echo "The SMI device has an owner. Stop the radio/controller applications first." >&2
    fuser -v /dev/smi >&2 || true
    exit 1
fi
if [[ -f /sys/module/$module/refcnt ]] && [[ $(cat "/sys/module/$module/refcnt") != 0 ]]; then
    echo "The streaming module is still in use; refusing reload" >&2
    exit 1
fi
if [[ -f /sys/module/$module/srcversion ]] &&
   [[ $(cat "/sys/module/$module/srcversion") != "$(modinfo -F srcversion "$installed")" ]]; then
    echo "Running module differs from the installed rollback source; preserve the actual original module first" >&2
    exit 1
fi

probe_idle() {
    python3 - "$1" <<'PY'
import errno
import fcntl
import os
import struct
import sys
mode=sys.argv[1]
# Stable fixed-width ABI: _IOR(0x1, 13, 80), _IOW(0x1, 14, 16).
get_status=(2<<30)|(80<<16)|(1<<8)|13
set_limit=(1<<30)|(16<<16)|(1<<8)|14
fd=os.open('/dev/smi',os.O_RDWR|os.O_NONBLOCK)
try:
    # Explicit idle only: this script never requests TX or RX.
    fcntl.ioctl(fd,(1<<8)|4,0)
    data=bytearray(80)
    try:
        fcntl.ioctl(fd,get_status,data,True)
    except OSError as error:
        if mode=='old' and error.errno==errno.ENOTTY:
            print('Existing driver is idle; TX-demand ABI is unavailable as expected.')
        else:
            raise
    else:
        status=struct.unpack('=10I5Q',data)
        if status[0]!=1 or status[1]!=80 or status[2]!=0:
            raise RuntimeError('Unexpected ABI or non-idle driver status')
        if mode=='new':
            period,ring=status[7],status[8]
            if period<=0 or ring!=4*period or 2*period>status[5]:
                raise RuntimeError('Invalid DMA/FIFO geometry')
            fcntl.ioctl(fd,set_limit,struct.pack('=4I',1,16,2*period,0))
            fcntl.ioctl(fd,get_status,data,True)
            verified=struct.unpack('=10I5Q',data)
            if verified[6]!=2*period or not verified[3]&1 or verified[4]!=0:
                raise RuntimeError('Bounded admission verification failed')
            fcntl.ioctl(fd,set_limit,struct.pack('=4I',1,16,0,0))
            print(f'New TX-demand ABI verified while idle: period={period}, ring={ring}, limit={2*period}.')
        else:
            print('Existing TX-demand driver is idle.')
finally:
    os.close(fd)
PY
}
probe_idle old

backup_dir="$(dirname "$candidate")/reload-backups/$(date -u +%Y%m%dT%H%M%SZ)-$$"
mkdir -p "$backup_dir"
cp -- "$installed" "$backup_dir/$(basename "$installed")"
rollback_module="$backup_dir/smi_stream_dev-original.ko"
case "$installed" in
    *.xz) xz -dc -- "$installed" > "$rollback_module" ;;
    *.gz) gzip -dc -- "$installed" > "$rollback_module" ;;
    *.zst) zstd -dc -- "$installed" > "$rollback_module" ;;
    *) cp -- "$installed" "$rollback_module" ;;
esac
[[ $(modinfo -F name "$rollback_module") == "$module" ]] || { echo "Rollback backup verification failed" >&2; exit 1; }
[[ $(modinfo -F vermagic "$rollback_module") == "$vermagic" ]] || { echo "Rollback kernel/ABI metadata differs from candidate" >&2; exit 1; }
sha256sum "$rollback_module" > "$backup_dir/rollback-sha256.txt"
printf 'Verified rollback module: %s\n' "$rollback_module"

# The normal module reference check remains authoritative if an application
# opens /dev/smi after the earlier owner check. Never force module removal.
rmmod "$module"
rollback() {
    local result=${1:-1}
    trap - ERR INT TERM
    echo "Reload failed; restoring the saved original streaming module" >&2
    if [[ -d /sys/module/$module ]]; then
        rmmod "$module" || { echo "Rollback stopped: candidate is now in use" >&2; exit "$result"; }
    fi
    if insmod "$rollback_module"; then
        probe_idle old || true
        echo "Original module restored. Installed module files were unchanged." >&2
    else
        echo "Automatic rollback failed. Saved module: $rollback_module" >&2
    fi
    exit "$result"
}
trap 'rollback $?' ERR
trap 'rollback 130' INT
trap 'rollback 143' TERM
insmod "$candidate"
probe_idle new
trap - ERR INT TERM
printf 'Runtime TX-demand driver is active and idle. Installed modules remain unchanged.\n'
printf 'Manual rollback after stopping radio applications:\nsudo rmmod smi_stream_dev\nsudo insmod %q\n' "$rollback_module"
