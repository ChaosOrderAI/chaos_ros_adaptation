#!/usr/bin/env bash
# Mint a throwaway CA and client certificates for local fleet uplink testing.
# Usage: fleet_test_certs.sh OUT_DIR NAME... (e.g. /tmp/fleet-certs go2-01 go2-02 platform)
# Produces OUT_DIR/ca.crt and OUT_DIR/<name>.crt / .key per name.
set -eo pipefail
out="${1:?usage: fleet_test_certs.sh OUT_DIR NAME...}"
shift
mkdir -p "$out"
ca_key="$out/ca.key"
ca_crt="$out/ca.crt"
openssl req -x509 -newkey rsa:2048 -nodes -days 30 \
    -keyout "$ca_key" -out "$ca_crt" -subj "/CN=chaos-fleet-test-ca" >/dev/null 2>&1
printf 'basicConstraints=CA:FALSE\nkeyUsage=digitalSignature,keyEncipherment\nextendedKeyUsage=clientAuth,serverAuth\n' > "$out/ext.cnf"
for name in "$@"; do
    openssl req -newkey rsa:2048 -nodes \
        -keyout "$out/$name.key" -out "$out/$name.csr" -subj "/CN=$name" >/dev/null 2>&1
    openssl x509 -req -in "$out/$name.csr" -CA "$ca_crt" -CAkey "$ca_key" \
        -CAcreateserial -days 30 -extfile "$out/ext.cnf" -out "$out/$name.crt" >/dev/null 2>&1
    rm -f "$out/$name.csr"
done
echo "wrote $ca_crt and certs for: $*"
