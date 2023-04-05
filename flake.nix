# SPDX-FileCopyrightText: Copyright (c) 2022 by Rivos Inc.
# SPDX-FileCopyrightText: Copyright (c) 2003-2022 Eelco Dolstra and the Nixpkgs/NixOS contributors
# Licensed under the MIT License, see LICENSE for details.
# SPDX-License-Identifier: MIT
{
  description = "Galois";

  inputs.nixpkgs.url = "nixpkgs/nixos-unstable";

  inputs.gem5.url = "github:picostove/gem5";

  outputs = {
    self,
    nixpkgs,
    gem5,
  }: let
    # to work with older version of flakes
    lastModifiedDate = self.lastModifiedDate or self.lastModified or "19700101";

    # Generate a user-friendly version number.
    version = builtins.substring 0 8 lastModifiedDate;

    # System types to support.
    supportedSystems = [
      "x86_64-linux"
    ];

    # Helper function to generate an attrset '{ x86_64-linux = f "x86_64-linux"; ... }'.
    forAllSystems = nixpkgs.lib.genAttrs supportedSystems;

    # Nixpkgs instantiated for supported system types.
    nixpkgsFor = forAllSystems (system:
      import nixpkgs {
        inherit system;
        overlays = [
          self.overlays.default
        ];
      });
  in {
    overlays.default = final: prev: {
      galois-benchmarks = final.callPackage ./rivos/nix {
        stdenv = final.gcc12Stdenv;
        src = self;
        inherit version;
      };
    };

    packages = forAllSystems (system: let
      pkgs = nixpkgsFor.${system};
      galois-riscv64-benchmarks = pkgs.pkgsCross.riscv64.pkgsStatic.galois-benchmarks;
      galois-benchmarks-native = pkgs.galois-benchmarks.override { stdenv = pkgs.impureUseNativeOptimizations pkgs.gcc12Stdenv; };
    in {
      inherit galois-riscv64-benchmarks galois-benchmarks-native;
      default = galois-benchmarks-native;
    });
  };
}
