# no-qt-host-export — this package must not name a logos-qt-host store path.
#
# WHY THIS CHECK EXISTS
#
# logos-qt-sdk::logos_qt_sdk links logos-qt-host, but logos-qt-sdk must not
# decide WHICH logos-qt-host. It used to, by two routes at once:
#
#   1. nix/lib.nix propagated qtHost, so the qt-host logos-qt-sdk was built
#      against landed on every consumer's CMAKE_PREFIX_PATH -- ahead of the
#      consumer's own. This was the edge that WON.
#   2. cpp/CMakeLists.txt baked ${logos-qt-host_DIR} into the generated Config
#      as a find_package HINTS fallback.
#
# An earlier rev had a third route: cpp/forward-header.h.in generated eleven
# headers that #include an absolute store path. That one is preprocessor-level,
# so no CMAKE_PREFIX_PATH ordering could have beaten it. It went with the header
# dedup. All three are the same shape -- text naming a store path in this
# package's output -- which is why one scan is the right assertion rather than
# three targeted ones: it also catches route four.
#
# WHAT IT PREVENTS. Consumers were SPLIT between two logos-qt-host prefixes
# inside a single `nix build`, with no diagnostic. When logos-qt-host gained
# Q_INVOKABLE currentCallerJson, logos_host_qt linked the stale one and
# QMetaObject::invokeMethod(logosAPI, "currentCallerJson") failed at RUNTIME
# with "No such method" in every module process, collapsing current_caller() to
# {"kind":"unknown"} fleet-wide -- on a green build.
#
# LIMITS, HONESTLY. This is a scan of a PREFIX, so it proves what this package
# exports, not what a consumer ends up linking. A statically linked consumer
# keeps no store path at all; asserting THAT is the job of the qt-host-identity
# metaobject check in the consumer repos (logos-module-loader-qt,
# logos-module-client, logos-liblogos). This check is the upstream half.
#
# A check that is not run by CI is dead code. Wire it into the workflow, not
# just into `checks`.
{ pkgs, subject, script ? ./no_qt_host_export.py }:

pkgs.runCommand "no-qt-host-export"
{
  nativeBuildInputs = [ pkgs.python3 ];
  passthru = { inherit subject; };
} ''
  set -euo pipefail

  python3 ${script} ${subject} | tee report.txt
  rc=''${PIPESTATUS[0]}

  if [ "$rc" -eq 2 ]; then
    echo ""
    echo "no-qt-host-export could not make a meaningful assertion (see above)."
    echo "That is a FAILURE, not a skip: a check whose pass verdict is 'found"
    echo "nothing' cannot distinguish 'nothing wrong' from 'looked at nothing'."
    exit 1
  fi
  [ "$rc" -eq 0 ] || exit 1

  mkdir -p $out
  cp report.txt $out/report.txt
''
