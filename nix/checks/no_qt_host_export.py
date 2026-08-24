#!/usr/bin/env python3
"""
no_qt_host_export — assert a prefix names no logos-qt-host store path.

logos-qt-sdk::logos_qt_sdk LINKS logos-qt-host, but logos-qt-sdk must not decide
WHICH logos-qt-host.  Every way it used to decide was text naming a store path
in its own output -- a propagated-build-inputs entry, a find_package HINTS baked
into the generated Config, and (before the header dedup) eleven forwarding
headers that #include an absolute store path.  One scan therefore covers the
whole class, including whatever route four turns out to be.

TWO THINGS THIS GETS RIGHT THAT A `grep -r` DOES NOT:

  * It follows symlinks and reads the TARGET's content.  logos-qt-sdk is a
    symlinkJoin, so nix-support/propagated-build-inputs is an lndir symlink into
    the -lib derivation.  `grep -r` does not follow it, and neither does a
    scanner that reads only link text -- both report a prefix that is actively
    exporting a stale qt-host as CLEAN.  Measured.

  * It also reads the link TEXT, which is a second place a store path hides.

Exit 0 = PASS, 1 = FAIL (an identity is exported), 2 = FAIL (vacuous).
"""

import os
import re
import sys

STORE = re.compile(rb"/nix/store/[a-z0-9]{32}-[^\x00\s\"'()\[\]]*")


def scan(prefix, needle):
    hits = {}
    total_refs = 0
    files_read = 0

    for root, dirs, files in os.walk(prefix, followlinks=False):
        for name in files + dirs:
            path = os.path.join(root, name)

            if os.path.islink(path):
                for m in STORE.findall(os.readlink(path).encode()):
                    total_refs += 1
                    if needle in m:
                        hits.setdefault(path + "  [symlink text]", set()).add(
                            m.decode())

            real = os.path.realpath(path)
            if not os.path.isfile(real):
                continue
            try:
                with open(real, "rb") as fh:
                    data = fh.read()
            except OSError:
                continue
            files_read += 1
            found = STORE.findall(data)
            total_refs += len(found)
            for m in found:
                if needle in m:
                    hits.setdefault(path, set()).add(m.decode())

    return hits, total_refs, files_read


def main():
    if len(sys.argv) != 2:
        print("usage: no_qt_host_export.py <prefix>", file=sys.stderr)
        return 2

    prefix = sys.argv[1]
    needle = b"-logos-qt-host-"

    if not os.path.isdir(prefix):
        print(f"VACUOUS: {prefix} is not a directory; nothing was scanned.")
        return 2

    hits, total_refs, files_read = scan(prefix, needle)

    print(f"prefix:          {prefix}")
    print(f"files read:      {files_read}")
    print(f"store-path refs: {total_refs}")
    print(f"qt-host refs:    {len(hits)} file(s)")

    # Liveness.  A pass here is a "found nothing", so distinguish it from
    # "looked at nothing": every real nix prefix names SOME store path.
    if files_read == 0 or total_refs == 0:
        print()
        print("VACUOUS: the scan saw no store-path references of any kind.")
        print("Every real nix prefix names some store path, so this means the")
        print("scan read nothing -- not that nothing is wrong.")
        return 2

    if hits:
        print()
        print("FAIL: this prefix names a logos-qt-host store path.")
        print("That is an IDENTITY export: a consumer gets the qt-host THIS")
        print("package was built against rather than the one in its own")
        print("closure. The build stays green; QMetaObject::invokeMethod then")
        print('fails at runtime with "No such method".')
        for f in sorted(hits):
            print(f"  {f}")
            for v in sorted(hits[f]):
                print(f"      {v}")
        return 1

    print()
    print("PASS: no logos-qt-host store path in this prefix.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
