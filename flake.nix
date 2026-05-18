{
  description = "pmdro";

  inputs = {
    nixpkgs.url = "github:nixos/nixpkgs/nixos-unstable";
  };

  outputs = { self, nixpkgs }:
    let
      systems = [ "x86_64-linux" "aarch64-linux" "x86_64-darwin" "aarch64-darwin" ];
      forAllSystems = f: nixpkgs.lib.genAttrs systems (system: f {
        pkgs = import nixpkgs { inherit system; };
      });
    in
    {
      packages = forAllSystems ({ pkgs }: {
        default = pkgs.stdenv.mkDerivation {
          name = "bansakako";
          src = ./.;
          nativeBuildInputs = [ pkgs.gnumake pkgs.gcc ];
          buildPhase = "make";
          installPhase = ''
            mkdir -p $out/bin
            cp bansakako $out/bin/
          '';
        };
      });

      devShells = forAllSystems ({ pkgs }: {
        default = pkgs.mkShell {
          packages = [ pkgs.gcc pkgs.gnumake ];
        };
      });
    };
}
