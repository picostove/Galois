# SPDX-FileCopyrightText: Copyright (c) 2022 by Rivos Inc.
# Licensed under the MIT License, see LICENSE for details.
# SPDX-License-Identifier: MIT
{
  fetchFromGitHub,
  lib,
  stdenv,
  boost,
  cmake,
  fmt,
  libllvm,
  numactl,
  src,
  version,
}:
stdenv.mkDerivation rec {
  name = "galois";
  inherit src version;

  buildInputs = [
    boost
    fmt
    libllvm
    numactl
  ];

  nativeBuildInputs = [
    cmake
  ];

  cmakeFlags = lib.optionals stdenv.hostPlatform.isRiscV [
    "-DHAVE_HUGEPAGES_INTERNAL_EXITCODE=1"
    "-DHAVE_HUGEPAGES_INTERNAL_EXITCODE__TRYRUN_OUTPUT=none"
  ];

  postInstall = ''
    # HACK: benchmarks should get their own output.
    cp -r lonestar/analytics/cpu/*/*-cpu "$out/bin/"
  '';

  meta = {
    description = "C++ library for multi-core and multi-node parallelization";
    license = lib.licenses.bsd3;
    platforms = lib.platforms.linux;
    homepage = "https://github.com/IntelligentSoftwareSystems/Galois";
  };
}
