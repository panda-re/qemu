{
  description = "PANDA: Platform for Architecture-Neutral Dynamic Analysis";
  inputs = {
    nixpkgs.url = "github:nixos/nixpkgs?ref=nixos-unstable";    
  };
  outputs =
    { nixpkgs, ... }:
    let
      forAllSystems = nixpkgs.lib.genAttrs nixpkgs.lib.systems.flakeExposed;
    in
    {
      packages = forAllSystems (
        system:
        let
          pkgs = nixpkgs.legacyPackages.${system};
          targetList = [
            "x86_64-softmmu"
            "i386-softmmu"
            "arm-softmmu"
            "aarch64-softmmu"
            "ppc-softmmu"
            "mips-softmmu"
            "mipsel-softmmu"
            "mips64-softmmu"
          ];
          srcWithSubprojects = pkgs.stdenv.mkDerivation {
            name = "panda-subprojects";
            src = ./.;
            nativeBuildInputs = with pkgs; [
              meson
              git
              cacert
            ];
            buildCommand = ''
              cp -r --no-preserve=mode $src $out
              cd $out
              meson subprojects download
              find subprojects -type d -name .git -prune -execdir rm -r {} +
            '';
            outputHash = "sha256-lw0kAqwoKlEYhxahnP4bV8i4j5fxKz2Dn/zVsEBiSnE=";
            outputHashAlgo = "sha256";
            outputHashMode = "recursive";
          };
          panda = pkgs.stdenv.mkDerivation (finalAttrs: {
            name = "panda";
            src = srcWithSubprojects;
            unpackPhase = ''
              cp -r --no-preserve=mode $src $TMPDIR/build
              cd build
              chmod +x configure
            '';
            nativeBuildInputs = with pkgs; [
              python3
              glib
              pkg-config
              dtc
              ninja
              autoPatchelfHook
              breakpointHook # TODO remove
            ];
            preConfigure = ''
              mkdir -pv build
              cd build
            '';
            configureScript = "../configure";
            configureFlags = [
              "--enable-plugins"
              "--disable-containers"
              "--target-list=${builtins.concatStringsSep "," targetList}"
            ];
          });
        in
        {
          default = panda;
        }
      );
    };
}
