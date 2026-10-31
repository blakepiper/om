{
  description = "om — a full-terminal Devanagari mantra visualizer";

  inputs.nixpkgs.url = "github:NixOS/nixpkgs/nixos-unstable";

  outputs = { self, nixpkgs }:
    let
      systems = [ "x86_64-linux" "aarch64-linux" "x86_64-darwin" "aarch64-darwin" ];
      forAllSystems = nixpkgs.lib.genAttrs systems;
    in {
      packages = forAllSystems (system:
        let pkgs = import nixpkgs { inherit system; }; in {
          default = pkgs.stdenv.mkDerivation {
            pname = "om";
            version = "0.1.0";
            src = ./src;
            buildPhase = "$CC -std=c11 -O2 -Wall -Wextra -Wpedantic om.c -lm -o om";
            installPhase = "install -Dm755 om $out/bin/om";
            meta.mainProgram = "om";
          };
        });

      devShells = forAllSystems (system:
        let pkgs = import nixpkgs { inherit system; }; in {
          default = pkgs.mkShell {
            # mkShell supplies the C toolchain; no extra packages are required.
            OM_DEV_SHELL = "1";
          };
        });
    };
}
