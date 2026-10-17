#!/usr/bin/env python3
"""Set up or run a loopback-only Mosquitto broker with test mTLS certificates."""

import argparse
import json
import os
from pathlib import Path
import secrets
import shutil
import subprocess
import sys
import tempfile


ROOT = Path(__file__).resolve().parents[1]
STATE = ROOT / ".local" / "mqtt"
CERTS = STATE / "certs"
BROKER_CONFIG = STATE / "mosquitto.conf"


def openssl(*args):
    result = subprocess.run(
        ["openssl", *map(str, args)], capture_output=True, text=True, check=False
    )
    if result.returncode:
        raise RuntimeError(result.stderr.strip() or "OpenSSL failed")


def prepare_certificates():
    names = [f"{role}.{suffix}" for role in ("ca", "server", "client")
             for suffix in ("crt", "key")]
    if CERTS.exists():
        if not all((CERTS / name).is_file() for name in names):
            raise RuntimeError(f"Incomplete certificates in {CERTS}; restore or move this directory before setup")
    else:
        # Publish the complete set together; failed generation leaves no partial CA.
        with tempfile.TemporaryDirectory(prefix="certs-", dir=STATE) as temporary:
            staging = Path(temporary)
            openssl(
                "req", "-x509", "-newkey", "rsa:2048", "-nodes", "-sha256",
                "-days", "3650", "-subj", "/CN=MQTT Local Test CA",
                "-addext", "basicConstraints=critical,CA:TRUE",
                "-addext", "keyUsage=critical,keyCertSign,cRLSign",
                "-keyout", staging / "ca.key", "-out", staging / "ca.crt",
            )
            for role, common_name, purpose in (
                ("server", "localhost", "serverAuth"),
                ("client", "mqtt-local-client", "clientAuth"),
            ):
                openssl(
                    "req", "-new", "-newkey", "rsa:2048", "-nodes", "-sha256",
                    "-subj", f"/CN={common_name}",
                    "-keyout", staging / f"{role}.key",
                    "-out", staging / f"{role}.csr",
                )
                extensions = staging / f"{role}.ext"
                extensions.write_text(
                    "basicConstraints=critical,CA:FALSE\n"
                    "keyUsage=critical,digitalSignature,keyEncipherment\n"
                    f"extendedKeyUsage={purpose}\n"
                    + ("subjectAltName=DNS:localhost,IP:127.0.0.1\n" if role == "server" else ""),
                    encoding="utf-8",
                )
                openssl(
                    "x509", "-req", "-in", staging / f"{role}.csr",
                    "-CA", staging / "ca.crt", "-CAkey", staging / "ca.key",
                    "-set_serial", f"0x{secrets.token_hex(16)}", "-days", "365",
                    "-sha256", "-extfile", extensions,
                    "-out", staging / f"{role}.crt",
                )
            complete = staging / "complete"
            complete.mkdir(mode=0o700)
            for name in names:
                (staging / name).rename(complete / name)
            complete.rename(CERTS)

    openssl("verify", "-CAfile", CERTS / "ca.crt", "-purpose", "sslserver",
            "-verify_ip", "127.0.0.1", CERTS / "server.crt")
    openssl("verify", "-CAfile", CERTS / "ca.crt", "-purpose", "sslclient",
            CERTS / "client.crt")


def setup(config_path):
    # Read first so a broken input config cannot be silently replaced.
    source = config_path if config_path.exists() else ROOT / "config.example.json"
    config = json.loads(source.read_text(encoding="utf-8"))
    STATE.mkdir(parents=True, exist_ok=True, mode=0o700)
    prepare_certificates()
    BROKER_CONFIG.write_text(
        "listener 8883 127.0.0.1\n"
        f"cafile {CERTS / 'ca.crt'}\n"
        f"certfile {CERTS / 'server.crt'}\n"
        f"keyfile {CERTS / 'server.key'}\n"
        "require_certificate true\n"
        "use_identity_as_username true\n"
        "allow_anonymous false\n"
        "set_tcp_nodelay true\n"
        "persistence false\n"
        "log_dest stdout\n",
        encoding="utf-8",
    )
    config["mqtt"].update({
        "host": "127.0.0.1",
        "port": 8883,
        "ca_file": str(CERTS / "ca.crt"),
        "client_certificate_file": str(CERTS / "client.crt"),
        "client_key_file": str(CERTS / "client.key"),
        "username": "",
    })
    # Loopback traffic must not be bound to an external Wi-Fi/cellular device.
    config.setdefault("network", {}).update({"access": "system", "interface": None})
    config_path.write_text(json.dumps(config, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    print(f"Test certificates: {CERTS}")
    print(f"Adapter config: {config_path.resolve()}")
    print("Start broker: python3 scripts/local_mqtt.py start")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    commands = parser.add_subparsers(dest="action", required=True)
    init = commands.add_parser("setup", help="create test certificates and configure a local adapter")
    init.add_argument("--config", type=Path, default=ROOT / "config.local.json",
                      help="adapter config to create/update (default: config.local.json)")
    commands.add_parser("start", help="run the local broker in the foreground; Ctrl+C stops it")
    args = parser.parse_args()
    os.umask(0o077)
    dependency = "openssl" if args.action == "setup" else "mosquitto"
    executable = shutil.which(dependency)
    if executable is None:
        parser.error(f"{dependency} is not installed")
    if args.action == "setup":
        setup(args.config)
    else:
        if not BROKER_CONFIG.is_file():
            parser.error("run setup first")
        os.execv(executable, [executable, "-c", str(BROKER_CONFIG)])


if __name__ == "__main__":
    try:
        main()
    except (OSError, ValueError, RuntimeError) as error:
        print(f"Local MQTT setup failed: {error}", file=sys.stderr)
        sys.exit(1)
