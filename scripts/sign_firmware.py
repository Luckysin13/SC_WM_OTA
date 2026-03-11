Import("env")

from pathlib import Path
import subprocess


def _sign_firmware(source, target, env):
    project_dir = Path(env["PROJECT_DIR"])
    build_dir = Path(env.subst("$BUILD_DIR"))

    firmware = build_dir / "firmware.bin"
    keyfile = project_dir / "secure_boot_signing_key.pem"
    py = Path(env["PROJECT_CORE_DIR"]) / "penv" / "bin" / "python"
    espsecure = Path(env["PROJECT_PACKAGES_DIR"]) / "tool-esptoolpy" / "espsecure.py"

    if not firmware.exists():
        print(f"[signing] Warning: firmware not found at {firmware}; skipping signing.")
        return

    if not keyfile.exists():
        print(f"[signing] Warning: signing key not found at {keyfile}; skipping signing.")
        return

    if not py.exists() or not espsecure.exists():
        print("[signing] Warning: signing toolchain not found; skipping signing.")
        return

    signed = build_dir / "firmware.signed.bin"
    cmd = [
        str(py),
        str(espsecure),
        "sign_data",
        "--version",
        "1",
        "--keyfile",
        str(keyfile),
        "--output",
        str(signed),
        str(firmware),
    ]

    print("[signing] Running:", " ".join(cmd))
    subprocess.check_call(cmd)
    signed.replace(firmware)
    print("[signing] Firmware signed successfully.")


env.AddPostAction("$BUILD_DIR/${PROGNAME}.bin", _sign_firmware)
