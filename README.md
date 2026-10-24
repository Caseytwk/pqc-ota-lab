# Securing Firmware OTA Updates with PQC: Lab

Workshop lab for signing ESP32 firmware updates with ECDSA P-256 and ML-DSA-44 (FIPS 204), delivering them over the air to an emulated ESP32, and proving the device rejects tampered updates.

**Attendees:** follow the workshop notes. Everything you need is already in this Codespace. Open a terminal with Ctrl+` and start with `cd ~/lab`.

---

## What is in here

| Path | Purpose |
| --- | --- |
| `run_device.sh` | Boot the emulated ESP32 (Ctrl+A then X to stop) |
| `provision.sh` | Write a public key into the device trust anchor |
| `reset_device.sh` | Restore factory firmware v1.0.0, keep the trust anchor |
| `device/` | Factory flash image and eFuse image for QEMU |
| `updates/firmware_v2.bin` | Unsigned v2.0.0 update payload |
| `ota/` | Served by the update server on port 8070 |
| `keys/` | Attendee keys (git ignored) |
| `firmware/` | ESP-IDF v5.5 source of the device firmware |
| `tests/e2e_test.sh` | Full workshop flow in QEMU, both schemes, valid and tampered |
| `tests/host_verify/` | Native build of the device verify code, for testing against OpenSSL |
| `fallback/` | Offline Docker image for attendees who cannot use Codespaces |

## How the device verifies an update

1. Polls `http://10.0.2.2:8070/firmware.bin` (QEMU's address for the Codespace)
2. Reads the app descriptor and skips the download if the version is already running
3. Streams the image into the inactive OTA slot. Nothing is activated yet
4. Downloads `firmware.sig` and verifies it over the image as written to flash (memory mapped), using the trust anchor from the `keys` partition
5. PASS: finalises the image, sets the boot partition, reboots. FAIL: erases the slot and stays on the current version

The signature check runs before ESP-IDF's own image checksum, so a tampered image is rejected by the signature, not by a checksum an attacker could recompute.

| Scheme | Trust anchor | Signature | Library |
| --- | --- | --- | --- |
| ECDSA P-256 | 64 B raw point | DER over SHA-256 (`openssl dgst -sha256 -sign`) | mbedTLS |
| ML-DSA-44 | 1,312 B raw key | 2,420 B, pure mode, empty context (`openssl pkeyutl -sign -rawin`) | PQClean `clean` |

Verify time is measured with `esp_timer` and includes hashing the whole image. Under QEMU the numbers are not representative of real hardware.

---

## Organiser setup (once)

1. **Create the repo.** Push this folder to a new **public** GitHub repo named `pqc-ota-lab`
2. **Build and test.** Actions tab, run **Build and test lab**. It builds both firmware versions in the `espressif/idf:v5.5.6` container, runs `tests/e2e_test.sh` in QEMU, and commits the tested `device/factory.bin` and `updates/firmware_v2.bin` to `main`
3. **Enable prebuilds.** Settings, Codespaces, Set up prebuild: branch `main`, region Southeast Asia, trigger on push
4. **Share the link.** `https://codespaces.new/<your-github-user>/pqc-ota-lab`. Put it on Notion page 01
5. **Rehearse.** Open a Codespace from that link with a fresh personal GitHub account and run both lab parts

## Rebuilding firmware locally

Inside an ESP-IDF v5.5 environment: `./firmware/build.sh`

## Testing the verify code without an ESP32

```bash
git clone --depth 1 -b v3.6.7 --recurse-submodules https://github.com/Mbed-TLS/mbedtls /tmp/mbedtls
tests/host_verify/build.sh /tmp/mbedtls
tests/host_verify/build/test_verify keys/.provisioned ota/firmware.bin ota/firmware.sig
```

The record file is the one `provision.sh` saves. Exit code 0 means the device would accept the update.

## Offline fallback

On a machine with Docker and after step 2: `fallback/build_offline_image.sh amd64` (and `arm64` for Apple Silicon). Copy the tar files to USB.

## Credits

ML-DSA-44 implementation from [PQClean](https://github.com/PQClean/PQClean) (public domain, commit in `firmware/components/pqclean_mldsa44/PQCLEAN_COMMIT`). ESP32 emulation by [Espressif QEMU](https://github.com/espressif/qemu).
