Import("env")

from pathlib import Path
import shutil

project_dir = Path(env["PROJECT_DIR"])
verify_key = project_dir / "signature_verification_key.bin"
signing_key = project_dir / "secure_boot_signing_key.pem"

if not verify_key.exists():
    print("[signing] Warning: signature_verification_key.bin not found; verification step may fail.")

if not signing_key.exists():
    print("[signing] Warning: secure_boot_signing_key.pem not found; firmware signing will be skipped.")

build_dir = Path(env.subst("$BUILD_DIR"))


def _prepare_signature_artifacts(target_dir: Path) -> None:
    build_key = target_dir / "signature_verification_key.bin"
    build_key_s = target_dir / "signature_verification_key.bin.S"

    target_dir.mkdir(parents=True, exist_ok=True)
    shutil.copy2(verify_key, build_key)
    build_key_s.write_text(
        ".section .rodata\n"
        ".global _binary_signature_verification_key_bin_start\n"
        ".global _binary_signature_verification_key_bin_end\n"
        "_binary_signature_verification_key_bin_start:\n"
        f".incbin \"{build_key.as_posix()}\"\n"
        "_binary_signature_verification_key_bin_end:\n"
        ".global _binary_signature_verification_key_bin_size\n"
        "_binary_signature_verification_key_bin_size = _binary_signature_verification_key_bin_end - _binary_signature_verification_key_bin_start\n"
    )
    print(f"[signing] Prepared {build_key} and {build_key_s}")


def _prepare_embed_file(source_file: Path, target_dir: Path) -> None:
    if not source_file.exists():
        return

    target_dir.mkdir(parents=True, exist_ok=True)
    target_file = target_dir / source_file.name
    target_s = target_dir / f"{source_file.name}.S"
    start_sym = f"_binary_{source_file.name.replace('.', '_')}_start"
    end_sym = f"_binary_{source_file.name.replace('.', '_')}_end"
    size_sym = f"_binary_{source_file.name.replace('.', '_')}_size"

    shutil.copy2(source_file, target_file)
    target_s.write_text(
        ".section .rodata\n"
        f".global {start_sym}\n"
        f".global {end_sym}\n"
        f"{start_sym}:\n"
        f".incbin \"{target_file.as_posix()}\"\n"
        f"{end_sym}:\n"
        f".global {size_sym}\n"
        f"{size_sym} = {end_sym} - {start_sym}\n"
    )
    print(f"[signing] Prepared {target_file} and {target_s}")

# ESP-IDF signed-app builds expect this generated assembly artifact.
# Some PlatformIO/IDF combinations don't auto-generate it, so create it here.
if verify_key.exists():
    _prepare_signature_artifacts(build_dir)
    _prepare_signature_artifacts(build_dir / "bootloader")

_prepare_embed_file(project_dir / "certs" / "server_cert.pem", build_dir)
_prepare_embed_file(project_dir / "certs" / "server_key.pem", build_dir)
