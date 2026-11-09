# RFC: manager enrollment for Chiroru late-loaded modules

**Draft / reference material only. This PR does not fix or alter the shipped kernel module.**

It contains a proposed integration contract, compilable host-only examples and tests. It does not modify `kernel/`, `uapi/`, the Chiroru loader, firmware assets, or factory patches. No kernel module was built or tested on a Station by the author of these examples. Host test success is not hardware compatibility or security acceptance of an integrated implementation.

## Diagnosis and provenance

Inspected base: `24ed77651882cba742b0436cecbbec0f176f4ec6` on `chiron`.
Related Chiroru source: `2afe0bb9b14dcc76dc3bcf7d7abcc9cdbd543a8c`.

The base initializes `ksu_manager_appid` to a device-specific numeric AppID in `kernel/manager/throne_tracker.c`. Both tracking and the package observer are disabled by vendor early returns. `kernel/hook/setuid_hook.c` recognizes the manager by AppID before installing the control FD and adjusting the Linux 5.4 seccomp state.

In operator-supplied diagnostics on firmware `5000001289`, the manager had a different AppID. The module loaded and root worked, but manager FD acquisition failed. A runtime AppID correction followed by restarting the manager restored FD installation. The relevant before/after events and the archive manifest were independently inspected; the device experiment was not repeated by the author of this proposal. Raw bugreports and temporary workaround binaries are not included.

A fixed manager AppID is an identity-confusion risk, not only a UI issue. This RFC adds no deployment or exploit utility. End-to-end privilege escalation has not been tested.

## Chosen trust boundary

The root loader is trusted. A malicious actor already controlling equivalent root access is outside this proposal's threat model. An unprivileged app must not be able to enroll itself as manager.

- Chiroru authenticates the installed manager using the firmware profile's exact package and signing-certificate policy, resolves its full Android UID and passes the resulting AppID through a privileged kernel interface.
- KernelSU authorizes that privileged operation, validates the identifier and controls manager admission/revocation. It need not parse and check the same APK a second time under this trust model.
- The kernel must derive caller credentials itself; never accept the example's `ref_caller` values from a userspace payload. Root real/effective UID checks shown here illustrate a policy and are not an assertion that the current handler already performs both checks.
- The existing `CHANGE_MANAGER_UID` handler checks real UID0. It is a potential initial-registration transport, **not** a complete lifecycle protocol: it currently has no transaction generation or stale-result rejection. Existing bootstrap-domain, namespace and SELinux behavior must be reviewed before choosing whether to reuse or restrict it.

Do not introduce a new unrestricted UID-setting API. Do not claim that a certificate was checked merely because a caller supplied a boolean.

## Proposed integration sequence

The following is pseudocode, not an existing Chiroru API:

```text
load module with manager identity initially INVALID
begin privileged enrollment; revoke old identity; receive generation ticket
query Android PackageManager for the exact profile manager
validate installed package, expected signer and supported version
resolve full UID and normalize it to AppID
commit only if the package identity is still current for that generation
restart the manager if it was already running
confirm control FD and manager-marked GET_INFO from the manager process
publish separate module_loaded / manager_registered / manager_connected results
```

A successful loader-side GET_INFO is not proof that the manager itself has acquired its FD. Merely reading back the configured AppID is not connection acceptance. A manager-side acknowledgement may require a separate change to KernelSU Next Manager; this RFC does not pretend the official APK already offers one to Chiroru.

Normal arm64 Chiroru currently permits downloading/installing the manager **after** loading the module. Either retain late registration or deliberately reorder installation; do not accidentally inherit factory's preinstalled-manager requirement.

The APK version/signing policy is model-specific. Preserve the bundled factory ARMv7 manager's distinct certificate and UAPI5; do not silently update it to the official arm64 manager. Signing history/rotation and UID/package ambiguity must be handled explicitly rather than accepting any signer or a package-name match alone.

## Lifecycle and races: required, not implemented

Enrollment works once only if package identity remains unchanged. A production design must also cover these conditions:

1. Invalidate the old identity on uninstall/replacement before an old AppID can be admitted for a different package.
2. Identify package changes through a kernel-owned mechanism that remains effective when Chiroru is stopped. **Android broadcasts to Chiroru alone are not a security revocation mechanism.**
3. Serialize identity transitions. A package change during authentication makes the result stale; an older authentication result must not overwrite a newer registration.
4. Do not scan APKs or acquire conflicting filesystem locks inside inode-rename/fsnotify callbacks. The dormant tracker is called from a rename hook; simply removing its early return is insufficient.
5. A deferred worker is appropriate for expensive reconciliation, but eventual background invalidation by itself leaves a UID-reuse window. Define and validate a fail-closed invalidation/admission boundary before claiming race safety.
6. Failure to read/parse a package snapshot, signature rejection, manager absence, worker failure, and shutdown must not grant manager privileges. The trusted loader must explicitly abort/invalidate a pending reference transaction after a failed external check; the model cannot observe that failure by itself. Do not prune the root allowlist from a partial package list.
7. Remove stale scan caches, handle allocation and observer error paths, and prove teardown quiesces callbacks/work before freeing state. Refcounts, memory ordering and generation publication are kernel-integration tasks, not solved by the host example.
8. Manager operations already using an FD must be re-authorized after revocation. Separately determine what happens to root grants/allowlist entries previously created by that manager; clearing its AppID is not retroactive removal of all privileges or seccomp changes.

The existing observer/tracker should be treated as a donor for review, not re-enabled blindly. A complete cross-repository implementation is deliberately deferred to a follow-up.

## Executable example

`reference.h` is a standalone, serialized state-machine example. It shows:

- no trusted numeric seed;
- privileged begin/commit operations;
- immediate model invalidation at a package event;
- generation-bound, one-attempt commits;
- stale/replayed result rejection without corrupting a newer identity;
- normal Android application UID validation and per-user AppID normalization;
- fail-closed counter exhaustion and reset.

**Not supplied:** APK authentication, PackageManager bindings, actual event detection, locks/atomics, observer hooks, kernel credential acquisition, manager FD installation, syscalls, module-loading logic, or a new UAPI. Calls must be serialized by the eventual owner. The example checks pointers only through its internal valid-pointer contract, not an untrusted ABI.

The example deliberately limits IDs to Android11 ordinary application AppIDs10000..19999. It excludes isolated/system UIDs and rejects full UIDs above INT32_MAX. Confirm allocator ranges for any broader target before integration. Per-user modulo comparison characterizes the existing kernel semantics; it does not prove package availability or policy correctness across profiles.

Generation tickets are a reference design requirement, **not a request to copy this C header into kernel code or blindly add begin/commit commands**. A smaller equivalent serialized mechanism is acceptable if it preserves the stated invariants.

## Host verification

```sh
python3 docs/manager-enrollment/run_checks.py
python3 docs/manager-enrollment/run_checks.py --sanitize
python3 docs/manager-enrollment/run_checks.py --cc clang --sanitize
```

All three commands ran successfully with 16 named reference-policy check groups. GCC11.4 and Clang on Ubuntu22.04 were used; sanitizer runs enabled ASan and UBSan. The application-ID loop exercises the complete accepted AppID range.

These are model tests, not production regressions or concurrent-kernel tests. In particular, an explicit `ref_invalidate()` models a delivered event; it cannot prove a real observer delivers that event early enough.

The separate `baseline_identity_test.c` / `baseline_checks.py` use the actual unmodified `kernel/manager/manager_identity.h` with minimal host mocks:

```sh
python3 docs/manager-enrollment/baseline_checks.py .
python3 docs/manager-enrollment/baseline_checks.py . --regression
```

On the inspected base, the first exits0 (six characterization groups); the second intentionally exits1 because a fresh module trusts an unverified AppID. **That RED is the defect reproduction, not a passing repair test.** No production GREEN is claimed. The baseline harness suppresses only the existing signedness warning for uid_t versus the -1 sentinel.

Prior baseline checks in Chiroru: module-builder/lifecycle suites passed14 tests; its factory patch applied with the builder's `patch --dry-run --batch --fuzz=0`. Those results do not verify this proposal's integration.

## Why no Station test

Chiroru's documented module build requires a prepared vendor-compatible Linux5.4.125 source/output tree, generated headers/configuration and matching Module.symvers. This external input is not included in either repository and was not available in the review environment. An ordinary upstream tree or a decrypted boot image is not an equivalent build input. An AArch64 compiler is available, but that alone cannot produce a qualified module.

Consequently: no modified module has been compiled, loaded or exercised on hardware. Boot persistence, actual manager FD acknowledgement, replacement races, factory integration, exported-symbol resolution and vendor structure/crypto ABI compatibility remain unverified.

## Maintainer questions / follow-up acceptance

- Is trusted Chiroru enrollment via the existing root-only command the preferred interface, or should bootstrap authority be narrower?
- What kernel-owned event/admission boundary should enforce revocation without depending on the Chiroru process?
- Should manager registration preserve the existing AppID-across-users policy?
- Can the qualified prepared kernel tree or a reproducible recipe be supplied for build validation?
- What manager-side acknowledgement can be exposed without relying on UI scraping or loader-side success?

Before an actual fix is ready, require positive and negative certificate/package tests; manager absent/before/after load cases; reinstall/UID-reuse and race tests; teardown/error injection; factory patch/build checks; qualified module builds; and hardware GET_INFO/root/Enforcing/reboot acceptance.

FD-wrapper ENODEV, SUSFS, boot flashing and unverified PTY support are intentionally out of scope.
