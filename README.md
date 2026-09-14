# PON hook

## Toolchain

```bash
wget https://downloads.openwrt.org/releases/19.07.8/targets/lantiq/xway_legacy/openwrt-sdk-19.07.8-lantiq-xway_legacy_gcc-7.5.0_musl.Linux-x86_64.tar.xz
tar -xf openwrt-sdk-19.07.8-lantiq-xway_legacy_gcc-7.5.0_musl.Linux-x86_64.tar.xz
cd openwrt-sdk-19.07.8-lantiq-xway_legacy_gcc-7.5.0_musl.Linux-x86_64
export STAGING_DIR=$(pwd)/staging_dir
export PATH="$STAGING_DIR/toolchain-mips_r2_gcc-7.5.0_musl/bin:$PATH"
```

## Build

```bash
git clone https://github.com{{REPOSITORY_PATH}}.git
cd libponhook
make CC=mips-openwrt-linux-musl-gcc
```

### Debug print

```bash
make CPPFLAGS="-DDEBUG" CC=mips-openwrt-linux-musl-gcc
```

## Install

### Failsafe

```bash
ssh root@192.168.11.1 << 'EOF'
  touch /ptconf/.failsafe
  reboot
EOF
```

#### Persistent root

```bash
ssh root@192.168.11.1 << 'EOF'
  fwenv_set -8 persist_root 1
  fwenv_set bootcmd 'run ubi_init;run flash_flash'
  reboot
EOF
```

### Scaffold

```bash
ssh root@192.168.11.1 'mkdir -p /etc/omci_hook'
```

### Upload

```bash
scp -O lua/omci_hook.lua root@192.168.11.1:/etc/omci_hook/
scp -O lua/omci_frame.lua lua/omci_pipe.lua root@192.168.11.1:/usr/lib/lua/
scp -O libponhook.so libponhook.so.0 root@192.168.11.1:/usr/lib/
```

## Run

```sh
uci delete omci.pon_adapter.modules
uci add_list omci.pon_adapter.modules='libponimg'
uci add_list omci.pon_adapter.modules='libponhook'
uci add_list omci.pon_adapter.modules='libpon'
uci apply
LD_LIBRARY_PATH="$LD_LIBRARY_PATH:/opt/pon/lib/" /usr/bin/omcid &
```
