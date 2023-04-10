# SPDX-FileCopyrightText: Copyright (c) 2022 by Rivos Inc.
# SPDX-FileCopyrightText: Copyright (c) 2003-2022 Eelco Dolstra and the Nixpkgs/NixOS contributors
# Licensed under the MIT License, see LICENSE for details.
# SPDX-License-Identifier: MIT
{
  description = "Galois";

  inputs = {
    nixpkgs.url = "github:rivosinc/nixpkgs/rivos/nixos-22.11?allRefs=1";
    flake-parts.url = "github:hercules-ci/flake-parts";

    gem5.url = "github:picostove/gem5";
    gem5.inputs.nixpkgs.follows = "nixpkgs";

    crosspkgs.url = "github:rivosinc/crosspkgs";
    crosspkgs.inputs.nixpkgs.follows = "nixpkgs";
  };

  outputs = inputs @ {
    crosspkgs,
    flake-parts,
    ...
  }:
    flake-parts.lib.mkFlake {inherit inputs;}
    {
      imports = [
        crosspkgs.flakeModules.default
        flake-parts.flakeModules.easyOverlay
      ];
      perSystem = {
        pkgs,
        inputs',
        lib,
        ...
      }: rec {
        packages = rec {
          galois-benchmarks = pkgs.callPackage ./rivos/nix {
            src = inputs.self;
            version = inputs.self.shortRev or "dirty";
            libllvm = pkgs.libllvm.overrideAttrs (oldAttrs: {
              cmakeFlags = oldAttrs.cmakeFlags ++ [ "-DLLVM_ENABLE_RTTI=ON" ];
            });
          };
          default = galois-benchmarks;
        };
        overlayAttrs = packages;
      };
    };
}
