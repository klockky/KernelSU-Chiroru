## Summary

Loading KernelSU through Chiroru 0.8.1 currently causes a kernel panic on my Duo Max. This PR adds the crash diagnostics and disassembly of the matching module.

## Environment

| Component | Version |
| --- | --- |
| Device | Yandex Station Duo Max, `YNDX-00055` (`chiron`) |
| Firmware | `5000001289` |
| Android | 11, SDK 30 |
| Kernel | `5.4.125-android11-2-g601513d4b115-ab5000001289`, `aarch64` |
| Chiroru | `0.8.1`, versionCode `51` |
| Manager | KernelSU Next `3.4.2`, versionCode `33355` |

## Observed behavior

With KernelSU mode selected, pressing **Get root** causes a reboot. With the manager installed, the reboot appears almost immediate. With the manager uninstalled, Chiroru reports root success, followed by a reboot after approximately 10 seconds.

Root previously worked with the same firmware and Chiroru version. The reason the failure started occurring has not been established.

The separate **ADB root shell** mode works: `chiroru-su` returns UID 0 in `u:r:chiroru_adb:s0`, with SELinux Enforcing. KernelSU was not loaded during diagnostic collection.

## Crash evidence

The boot reason is `kernel_panic,fatal_exception`. Five retained `SYSTEM_LAST_KMSG` entries show a fatal exception at the same module call site:

```text
check_block.constprop.0+0x25c/0x2e4 [kernelsu]
```

The latest crash occurs during the manager APK scan in module initialization:

```text
lr : crypto_destroy_tfm+0x68/0xac

Call trace:
  check_block.constprop.0+0x25c/0x2e4 [kernelsu]
  is_manager_apk+0x49c/0x500 [kernelsu]
  my_actor+0x2a0/0x454 [kernelsu]
  ext4_readdir+0x960/0xb18
  iterate_dir+0xd8/0x188
  search_manager+0x20c/0x3a0 [kernelsu]
  track_throne+0x338/0x360 [kernelsu]
  kernelsu_init+0xfc/0x154 [kernelsu]

Kernel panic - not syncing: Fatal exception
```

## Module verification

The device's unpatched `kernelsu.ko` matches the `5000001289` asset in Chiroru commit `7230a7fa1389ee66b10caab3d71b0adbce7ebece` byte for byte:

- Size: `275152` bytes.
- SHA-256: `4e11da46cd8088c7a6d8412a49764db5ef9741b6cfb012d54959603db90454b4`.
- KernelSU source revision: `10ed3691ec92e3997144ca91ded20b27c4be1efd`.

In that binary, `check_block.constprop.0` starts at `0x2ee8`. The call to `crypto_destroy_tfm` is at `0x3140`, with a return address of `0x3144`: function offset `0x25c`, matching the crash frame. This locates the failure in the certificate-hash cleanup path; the underlying cause has not yet been confirmed.

## Included diagnostics

- Latest panic excerpt from pstore.
- Relevant frames from all five retained crash entries.
- Disassembly of the certificate-hash code around the failing call.
- Module identity and SHA-256 manifest.

The retained logs were collected without triggering another KernelSU load. No modified module was built or tested.
