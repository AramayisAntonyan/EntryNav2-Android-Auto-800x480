#!/bin/sh
# Copyright (c) 2026 Jiraiya43
# SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
# Installs this release on the head unit, or removes it (--uninstall). Runs ON the unit in its BusyBox shell, from the
# extracted archive folder: `sh install.sh`. Nothing is changed until every check passes; the system partition is
# writable only during the copy, and the previous files are put back if a copied file does not match. Every line
# starts with STEP, OK, WARN or FAIL; the last line is RESULT ok|fail|stopped. See docs/INSTALL.md.
#   sh install.sh [--force]     install (--force: install even if the dry run reports a blocking problem)
#   sh install.sh --uninstall   remove what this installer added
set -u
PKG=$(cd "$(dirname "$0")" && pwd)
MODE=install
FORCE=""
for a in "$@"; do
    case "$a" in
        --uninstall) MODE=uninstall ;;
        --force) FORCE=1 ;;
        *) echo "FAIL unknown option: $a"; echo "RESULT fail"; exit 1 ;;
    esac
done

BACKUP=/tmp/entryaad-backup
L=/usr/local/lib:/opt/hmi/lib:/usr/lib:/lib
PLUGINS="EntryAad CarGauges AuxCamera"
RW=0
FW_FILE=etc/firewall/entry-aad.rules
FW_LOADER=/usr/local/bin/iptables-firewall
FW=0
FW_HAD=0
IPT="busybox timeout -t 10 /usr/sbin/iptables"
# The diagnostic gateway's ports, dropped by the unit's own firewall: the engine values and fault codes need them.
RULES="OUTPUT -o vbr0 -p tcp --dport 6801 -j ACCEPT
OUTPUT -o vbr0 -p udp --dport 6811 -j ACCEPT
INPUT -i vbr0 -p udp --sport 6811 -j ACCEPT"

say() { echo "$*"; }
finish() {
    if [ "$RW" = 1 ]; then
        sync
        if mount -o remount,ro / 2>/dev/null; then say "OK / read-only again"; else say "WARN / still writable until the unit restarts"; fi
    fi
    say "RESULT $1"
    [ "$1" = ok ] && exit 0 || exit 1
}
fail() { say "FAIL $*"; finish fail; }

stop_aa() {
    if ps | grep -E 'entry-aad|aa-supervisor' | grep -v grep >/dev/null; then
        mkdir -p /tmp/entry-aa-service && touch /tmp/entry-aa-service/STOP
        i=0
        while ps | grep -E 'entry-aad|aa-supervisor' | grep -v grep >/dev/null; do
            i=$((i + 1))
            [ $i -le 15 ] || fail "Android Auto did not stop within 30 s: nothing changed (stop it from its iDrive menu)"
            sleep 2
        done
    fi
    pkill -f gauge-collector 2>/dev/null
    pkill -f car-test 2>/dev/null
}

if [ "$MODE" = uninstall ]; then
    say "STEP 1/2 stopping Android Auto"
    stop_aa
    say "OK stopped"
    say "STEP 2/2 removing the files"
    mount -o remount,rw / || fail "cannot make / writable: nothing changed"
    RW=1
    rm -rf /opt/aa
    for p in $PLUGINS; do rm -rf "/opt/hmi/data/ro/Plugins/$p"; done
    rm -f "/$FW_FILE"
    rmdir /opt/hmi/data/ro/Plugins 2>/dev/null  # only if this installer created it and nothing else is there
    sync
    say "OK files removed (settings in /var/local/data/AA kept; delete that folder to remove them too)"
    finish ok
fi

cd "$PKG" || fail "cannot read the package folder $PKG"

say "STEP 1/7 checksums of the package"
bad=$(md5sum -c MD5SUMS 2>&1 | grep -v ': OK$')
[ -z "$bad" ] || fail "package checksums: $bad"
say "OK $(grep -c . MD5SUMS) files"

say "STEP 2/7 every library and function on this unit"
for f in opt/aa/entry-aad opt/aa/aa-supervisor opt/hmi/data/ro/Plugins/*/lib*.so opt/hmi/data/ro/Plugins/CarGauges/gauge-collector opt/hmi/data/ro/Plugins/CarGauges/car-test; do
    [ -f "$f" ] || continue
    miss=$(LD_TRACE_LOADED_OBJECTS=1 LD_WARN=yes LD_BIND_NOW=yes LD_LIBRARY_PATH=$L /lib/ld-linux-armhf.so.3 ./$f 2>&1 |
        grep -E 'not found|undefined symbol|not defined in file')
    [ -z "$miss" ] || fail "$f needs something this unit lacks: $miss"
done
say "OK nothing missing"

say "STEP 3/7 dry run of Android Auto's start"
compat=$(LD_LIBRARY_PATH=$L XDG_RUNTIME_DIR=/run/system/0 WAYLAND_DISPLAY=wayland-0 HOME=/tmp \
    busybox timeout -t 60 ./opt/aa/aa-supervisor --compat 2>&1 | grep -E '^@')
echo "$compat" | sed 's/^/OK /'
if echo "$compat" | grep -q '^@block'; then
    [ -n "$FORCE" ] || fail "the dry run reports a blocking problem: not installed (rerun with --force only if you know why)"
    say "WARN blocking problem overridden by --force"
fi

say "STEP 3b firewall (read only here)"
if [ ! -f "$FW_FILE" ]; then
    say "WARN the package has no $FW_FILE: firewall left as it is"
elif [ -d /etc/firewall ] && [ -f "$FW_LOADER" ] && grep -q '/etc/firewall' "$FW_LOADER" 2>/dev/null; then
    FW=1
    say "OK $FW_LOADER runs /etc/firewall at start-up: the rules will be installed"
else
    say "WARN no /etc/firewall loader on this unit: rules file not installed (rules still added until the next restart)"
fi

say "STEP 4/7 stopping Android Auto"
stop_aa
say "OK stopped"

say "STEP 5/7 backup of the installed files"
rm -rf "$BACKUP" && mkdir -p "$BACKUP/aa" || fail "cannot make $BACKUP"
[ -d /opt/aa ] && cp -a /opt/aa/. "$BACKUP/aa/" 2>/dev/null
for p in $PLUGINS; do
    if [ -d "/opt/hmi/data/ro/Plugins/$p" ]; then
        mkdir -p "$BACKUP/$p" && cp -a "/opt/hmi/data/ro/Plugins/$p/." "$BACKUP/$p/" || fail "backup of $p"
    fi
done
[ -f "/$FW_FILE" ] && { FW_HAD=1; cp -a "/$FW_FILE" "$BACKUP/entry-aad.rules" || fail "backup of /$FW_FILE"; }
say "OK backup in $BACKUP"

say "STEP 6/7 installing"
mount -o remount,rw / || fail "cannot make / writable: nothing changed"
RW=1
mkdir -p /opt/aa || fail "mkdir /opt/aa"
rm -f /opt/aa/* && cp opt/aa/* /opt/aa/ || fail "copy of /opt/aa"
for p in $PLUGINS; do
    [ -d "opt/hmi/data/ro/Plugins/$p" ] || continue
    mkdir -p "/opt/hmi/data/ro/Plugins/$p" && rm -f "/opt/hmi/data/ro/Plugins/$p/"* &&
        cp "opt/hmi/data/ro/Plugins/$p/"* "/opt/hmi/data/ro/Plugins/$p/" || fail "copy of $p"
    touch "/opt/hmi/data/ro/Plugins/$p/.installed-by-entryaad"
done
chmod 755 /opt/aa/entry-aad /opt/aa/aa-supervisor /opt/hmi/data/ro/Plugins/*/lib*.so \
    /opt/hmi/data/ro/Plugins/CarGauges/gauge-collector /opt/hmi/data/ro/Plugins/CarGauges/car-test 2>/dev/null
touch /opt/aa/.installed-by-entryaad
[ "$FW" = 1 ] && { cp "$FW_FILE" "/$FW_FILE" && chmod 644 "/$FW_FILE" || fail "copy of /$FW_FILE"; }
sync

say "STEP 7/7 verifying the installed files"
if [ "$FW" = 1 ]; then
    cp MD5SUMS "$BACKUP/MD5SUMS.check"
else
    grep -v " $FW_FILE\$" MD5SUMS >"$BACKUP/MD5SUMS.check"
fi
bad=$(cd / && md5sum -c "$BACKUP/MD5SUMS.check" 2>&1 | grep -v ': OK$')
if [ -n "$bad" ]; then
    say "FAIL installed files differ: $bad"
    say "STEP restoring the previous files"
    rm -f /opt/aa/* && cp -a "$BACKUP/aa/." /opt/aa/ 2>/dev/null
    for p in $PLUGINS; do
        if [ -d "$BACKUP/$p" ]; then
            rm -f "/opt/hmi/data/ro/Plugins/$p/"* && cp -a "$BACKUP/$p/." "/opt/hmi/data/ro/Plugins/$p/"
        else
            rm -rf "/opt/hmi/data/ro/Plugins/$p"
        fi
    done
    if [ "$FW_HAD" = 1 ]; then cp -a "$BACKUP/entry-aad.rules" "/$FW_FILE"; elif [ "$FW" = 1 ]; then rm -f "/$FW_FILE"; fi
    sync
    say "OK previous files back"
    finish fail
fi
say "OK every file verified"
rm -f /var/local/data/AA/app-guard  # a fresh build: the crash guard starts over

if [ "$FW" = 1 ] || [ -d /etc/firewall ]; then
    say "STEP firewall rules for the diagnostic gateway, in force now"
    mkdir -p /var/local/data/AA
    echo "$RULES" | while read -r rule; do
        # shellcheck disable=SC2086 # a rule's words are separate iptables arguments
        if $IPT -C $rule 2>/dev/null; then say "OK already there: $rule"
        elif $IPT -A $rule 2>/dev/null; then say "OK added: $rule"
        else say "WARN not added: $rule"; fi
    done
fi
say "OK installed; restart the unit: systemctl --force reboot"
finish ok
