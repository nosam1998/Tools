# Drift EQ Windows release and signing guide

The APO engine and controller are implemented, and the automated DLL and recovery tests pass. The current archive is an **unsigned developer preview**. It has not been loaded into the Windows audio engine or validated on physical playback devices. There is no publisher signing setup yet.

Signing is one remaining release dependency. Device integration and live audio testing are also required; obtaining a certificate does not establish that the effect works on every Windows sound device.

## What is available now

Run the DLL test harness, inspect playback devices, or open the controller using `README-APO.md`. These do not require installing the effect. The setup script rejects unsigned payloads before changing machine settings. The browser prototype can already demonstrate the sound using audio loaded into it; it does not filter other apps.

## Establish a publisher and signing route

1. **Confirm publisher eligibility before buying a certificate.** Microsoft's Hardware Developer Program registration currently requires an organization, an EV code-signing certificate, a Microsoft Entra global-administrator account, and an authorized legal contact. Business verification and agreements are part of enrollment. An Entra directory can be created during registration. The EV certificate is an enrollment requirement, not by itself a guarantee of protected-audio compatibility. See [Microsoft's registration requirements](https://learn.microsoft.com/en-us/windows-hardware/drivers/dashboard/hardware-program-register).
2. **Confirm the submission route for this APO.** Ask Hardware Developer support which package and signature attributes are required for an endpoint-effect DLL loaded by `audiodg.exe`, including protected media. Microsoft lists PETrust, DrmLevel, and DLLs among the items that attestation signing can process, but describes attestation as intended for testing and without compatibility certification. Plan retail distribution around the applicable certification requirements; do not assume an ordinary desktop-app signature or a generic attestation submission is sufficient. See [Microsoft's signing options](https://learn.microsoft.com/en-us/windows-hardware/drivers/dashboard/driver-signing-offerings).
3. **Set up controlled signing after enrollment.** Select the certificate/provider only after confirming the requirements. Keep private signing keys out of the repository and build artifacts. Use a protected signing service or hardware-backed provider and a separately authorized release job. Sign the controller as a desktop executable and follow the confirmed audio-component process for the DLL/package. Rebuild the downloadable archive and checksums after signing.

No account registration, certificate purchase, agreement acceptance, or signing-key provisioning has been performed for this project.

## Complete deployment engineering

The current setup script attaches the effect only to a compatible endpoint with a writable effects store and no conflicting endpoint/post-mix effect. It preserves stream/mode effects, records the exact properties it changes, and provides conflict-aware removal. It does not supply a universal driver extension or replace vendor effects.

For a broader release, establish a supported device/driver list and implement the required signed integration package for that scope. Microsoft's componentized APO guidance ties deployment to the audio driver; writing one APO DLL does not automatically integrate it with all drivers. The DLL already follows the documented dynamic-runtime and no-embedded-manifest guidance. See [Microsoft's APO implementation and packaging documentation](https://learn.microsoft.com/en-us/windows-hardware/drivers/audio/implementing-audio-processing-objects).

A valid Authenticode result is only the setup script's initial trust check. Validate the final signed payload in the actual Windows audio engine with the standard protected-audio configuration before describing it as installable. The preview does not weaken Windows audio or driver-signing policy.

## Evidence required before a consumer release

- Confirm installation, effect discovery/loading, enable/disable, and exact removal on representative physical devices. Include devices with vendor enhancements and devices the installer must refuse.
- Test playback, recording/calls, hotplug, sleep/resume, reboot, driver updates, multiple users, long sessions, and controller crashes. Measure CPU use, audible glitches, and latency.
- Document the behavior of exclusive/ASIO, spatial, hardware-offloaded, and protected playback separately. Only claim support for paths that pass testing.
- Validate recovery from interrupted installation and failed audio-engine loading on a disposable Windows test machine; then test the signed release package on clean retail Windows installations.
- Publish the supported-device list, signed installer/package, versioned recovery instructions, and final checksums together.

The existing automated tests validate DSP and COM behavior in an isolated test process plus setup backup/restore logic. They do not substitute for these deployment checks. The immediate owner action is to confirm publisher eligibility and the APO signing route; product testing and integration can continue in parallel once a suitable test environment is available.
