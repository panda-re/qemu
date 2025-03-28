PANDA_DIR_REL="$(dirname $0)"
TARGETS="x86_64-softmmu,i386-softmmu,arm-softmmu,aarch64-softmmu,ppc-softmmu,ppc64-softmmu,mips-softmmu,mipsel-softmmu,mips64-softmmu,mips64el-softmmu,loongarch64-softmmu,riscv32-softmmu,riscv64-softmmu"

"${PANDA_DIR_REL}/configure" --enable-plugins --target-list=$TARGETS
ninja