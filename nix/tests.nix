# Builds and runs the test suite
#
# qtGenerator is a nativeBuildInput because tests/qt-generator drives the
# INSTALLED binary rather than linking the generator's internals — that binary
# is what module-builder actually invokes, so it is what the goldens must pin.
#
# cppSdkInclude is a buildInput (headers only, no library) for one target:
# tests/qt-generator's single-TU compile check. A generated consumer wrapper
# includes headers from BOTH layers — logos_api.h / logos_types.h from the Qt
# host, logos_async_result.h from the base SDK — so without it the suite can
# generate wrapper text but never hand it to a compiler, which is exactly how a
# wrapper that does not compile in the umbrella's TU got shipped.
{ pkgs, common, src, protocolLib, cppGenerator, cppSdkInclude, qtGenerator, qtHost }:

pkgs.stdenv.mkDerivation {
  pname = "${common.pname}-tests";
  version = common.version;

  inherit src;

  nativeBuildInputs = common.nativeBuildInputs ++ [ cppGenerator qtGenerator ];
  buildInputs = common.buildInputs ++ [ pkgs.gtest protocolLib qtHost cppSdkInclude ];
  cmakeFlags = common.cmakeFlags;

  dontUseCmakeConfigure = true;

  buildPhase = ''
    runHook preBuild

    mkdir -p build-tests
    cd build-tests
    cmake ../tests -GNinja -DLOGOS_PROTOCOL_ROOT=${protocolLib} \
      -DLOGOS_QT_HOST_ROOT=${qtHost} -DLOGOS_CPP_SDK_ROOT=${cppSdkInclude} $cmakeFlags
    ninja
    cd ..

    runHook postBuild
  '';

  doCheck = true;
  checkPhase = ''
    runHook preCheck
    cd build-tests
    export QT_QPA_PLATFORM=offscreen
    ctest --output-on-failure

    # ── The origin-bound consumer surface links NO Qt host identity code ──
    #
    # tests/qt-generator's compile probe proves a module needs no LogosAPI
    # OBJECT. This proves the stronger thing the cdylib case actually wants: it
    # needs none of LogosAPI's CODE either. The distinction is not academic —
    # LpBridge is a header, so anything its inline members name is emitted into
    # every translation unit that reaches them, whether or not the branch can
    # run. Measured on this very archive before the seam was reshaped:
    # `LogosAPI::getTokenManager()` and `TokenManager::getToken()` were
    # undefined in a TU whose bridge is api-less by construction.
    #
    # Greps the MANGLED names (Itanium mangling spells the class inline, on both
    # ELF and Mach-O), so no c++filt is needed and the check reads the same on
    # every platform.
    echo "Checking the origin-bound TU for Qt host identity symbols..."
    if nm -u qt-generator/libqtgen_origin_umbrella_tu.a | grep -e LogosAPI -e TokenManager; then
      echo "FAIL: the origin-bound consumer TU still needs Qt host identity code." >&2
      echo "      Something on the LpBridge path names LogosAPI or TokenManager" >&2
      echo "      from a function every caller reaches, rather than from one only" >&2
      echo "      forTarget instantiates. See syncFromApi in logos_qt_lp_bridge.h." >&2
      exit 1
    fi

    # POSITIVE CONTROL. Without it the check above is satisfied by an empty
    # archive, a renamed target, or an `nm` that printed nothing — each of which
    # would retire the assertion silently. The LogosAPI-TAKING TU beside it is
    # built from the same seam and MUST still reference that code.
    if ! nm -u qt-generator/libqtgen_umbrella_tu.a | grep -q -e LogosAPI -e TokenManager; then
      echo "FAIL: control — the LogosAPI-taking TU references no LogosAPI code," >&2
      echo "      so the check above proves nothing about the origin-bound one." >&2
      exit 1
    fi
    echo "OK: the origin-bound TU needs no Qt host identity symbols (control: the LogosAPI-taking one does)"

    cd ..
    runHook postCheck
  '';

  installPhase = ''
    runHook preInstall

    mkdir -p $out/bin
    cp build-tests/qt-sdk/qt_sdk_tests $out/bin/

    runHook postInstall
  '';

  inherit (common) meta;
}
