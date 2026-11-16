"""Build the small platform-only APK with the installed Android tools."""
from pathlib import Path
import hashlib
import json
import shutil
import subprocess
import zipfile

app = Path(__file__).resolve().parent
project = app.parent
build = app / "build"
build.mkdir(exist_ok=True)
sdk = Path(r"C:\Android\sdk")
tools = sdk / "build-tools/36.0.0"
android = sdk / "platforms/android-36/android.jar"
jdk = sorted(Path(r"C:\Program Files\Microsoft").glob("jdk-21*"))[-1]
java = jdk / "bin/java.exe"
compiler = Path(r"C:\Android\android-ndk-r30\toolchains\llvm\prebuilt\windows-x86_64\bin\clang++.exe")
commands = []

def run(*args):
    command = [str(a) for a in args]
    commands.append(command)
    result = subprocess.run(command, cwd=project, capture_output=True, text=True)
    if result.returncode:
        raise RuntimeError(result.stdout + result.stderr)
    if result.stderr.strip():
        print(result.stderr.strip())
    return result.stdout

for name in ("classes", "dex", "assets"):
    directory = (build / name).resolve()
    assert directory.parent == build.resolve()
    if directory.exists():
        shutil.rmtree(directory)
    directory.mkdir()

run(compiler, "--target=aarch64-linux-android36", "-std=c++17", "-O2", "-g",
    "-Wall", "-Wextra", "-Werror", "-static-libstdc++", project / "power_logger.cpp",
    "-o", build / "assets/power-logger")
(build / "assets/control.sh").write_bytes((project / "power-logger.sh").read_text().encode())
run(jdk / "bin/javac.exe", "--release", "8", "-classpath", android,
    "-encoding", "UTF-8", "-d", build / "classes", app / "MainActivity.java")
run(jdk / "bin/jar.exe", "cf", build / "classes.jar", "-C", build / "classes", ".")
run(java, "-cp", tools / "lib/d8.jar", "com.android.tools.r8.D8", "--lib", android,
    "--min-api", "26", "--output", build / "dex", build / "classes.jar")
run(tools / "aapt2.exe", "link", "-I", android, "--manifest", app / "AndroidManifest.xml",
    "-A", build / "assets", "-o", build / "unsigned.apk")
with zipfile.ZipFile(build / "unsigned.apk", "a", zipfile.ZIP_DEFLATED) as apk:
    for file in (build / "dex").glob("*.dex"):
        apk.write(file, file.name)
run(tools / "zipalign.exe", "-f", "4", build / "unsigned.apk", build / "aligned.apk")
key = build / "local-development.jks"
if not key.exists():
    run(jdk / "bin/keytool.exe", "-genkeypair", "-keystore", key, "-storepass", "android",
        "-keypass", "android", "-alias", "s26-power", "-keyalg", "RSA", "-keysize", "2048",
        "-validity", "3650", "-dname", "CN=S26 Power Local Development", "-noprompt")
output = build / "S26-Power.apk"
run(java, "-jar", tools / "lib/apksigner.jar", "sign", "--ks", key,
    "--ks-pass", "pass:android", "--key-pass", "pass:android", "--out", output, build / "aligned.apk")
verification = run(java, "-jar", tools / "lib/apksigner.jar", "verify", "--verbose", output)
(build / "build.json").write_text(json.dumps({"commands": commands,
    "apk_sha256": hashlib.sha256(output.read_bytes()).hexdigest(),
    "verification": verification}, indent=2) + "\n")
print(verification)
print(output)
