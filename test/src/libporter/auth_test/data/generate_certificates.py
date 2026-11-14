#!/usr/bin/env python3
"""OpenSSL コマンドで相互認証テスト専用の ECDSA P-256 証明書と秘密鍵を生成します。"""

import os
from pathlib import Path
import shutil
import subprocess
import tempfile


def run_openssl(directory, *arguments):
    """失敗時に生成を中断します。"""
    try:
        subprocess.run(
            ["openssl", *arguments], cwd=directory, check=True,
            stdout=subprocess.PIPE, stderr=subprocess.PIPE,
        )
    except subprocess.CalledProcessError as error:
        raise RuntimeError(error.stderr.decode("utf-8")) from error


def generate(destination):
    """固定した有効期間と識別名を持つテスト用 PEM を生成します。鍵は毎回生成します。"""
    with tempfile.TemporaryDirectory(prefix="porter-auth-certificates-") as temporary:
        directory = Path(temporary)
        (directory / "issued").mkdir()
        (directory / "index.txt").write_text("", encoding="utf-8")
        (directory / "serial").write_text("1000\n", encoding="utf-8")
        (directory / "crlnumber").write_text("1000\n", encoding="utf-8")
        (directory / "ca.conf").write_text(
            """[ca]
default_ca = issuer
[issuer]
database = index.txt
serial = serial
crlnumber = crlnumber
new_certs_dir = issued
certificate = ca.pem
private_key = ca-key.pem
default_md = sha256
default_days = 36500
default_crl_days = 36500
unique_subject = no
policy = names
[names]
commonName = supplied
[authority]
basicConstraints = critical,CA:true
keyUsage = critical,keyCertSign,cRLSign
subjectKeyIdentifier = hash
authorityKeyIdentifier = keyid:always
[dns]
basicConstraints = critical,CA:false
keyUsage = critical,digitalSignature
extendedKeyUsage = serverAuth,clientAuth
subjectAltName = DNS:porter.test
[uri]
basicConstraints = critical,CA:false
keyUsage = critical,digitalSignature
extendedKeyUsage = serverAuth,clientAuth
subjectAltName = URI:urn:porter:peer
[wildcard]
basicConstraints = critical,CA:false
keyUsage = critical,digitalSignature
extendedKeyUsage = serverAuth,clientAuth
subjectAltName = DNS:*.porter.test
[common_name]
basicConstraints = critical,CA:false
keyUsage = critical,digitalSignature
extendedKeyUsage = serverAuth,clientAuth
""", encoding="utf-8",
        )

        def request(name, subject=None, reuse_key=None):
            key = f"{name}-key.pem"
            if reuse_key is None:
                run_openssl(directory, "genpkey", "-algorithm", "EC", "-pkeyopt", "ec_paramgen_curve:P-256", "-out", key)
            else:
                shutil.copyfile(directory / f"{reuse_key}-key.pem", directory / key)
            run_openssl(directory, "req", "-new", "-key", key, "-subj", f"/CN={subject or name}", "-out", f"{name}.csr")

        request("ca")
        run_openssl(directory, "ca", "-batch", "-selfsign", "-config", "ca.conf", "-keyfile", "ca-key.pem",
                    "-in", "ca.csr", "-out", "ca.pem", "-notext", "-extensions", "authority",
                    "-startdate", "20200101000000Z", "-enddate", "21200101000000Z")
        request("other_ca")
        run_openssl(directory, "ca", "-batch", "-selfsign", "-config", "ca.conf", "-keyfile", "other_ca-key.pem",
                    "-in", "other_ca.csr", "-out", "other_ca.pem", "-notext", "-extensions", "authority",
                    "-startdate", "20200101000000Z", "-enddate", "21200101000000Z")
        identities = [
            ("client", "dns", None), ("server", "uri", None), ("foreign", "dns", None),
            ("expired", "dns", None), ("revoked", "dns", None), ("unregistered", "dns", None),
            ("same_key", "dns", "client"), ("wildcard", "wildcard", None),
            ("common_name", "common_name", None),
        ]
        for name, extension, reuse_key in identities:
            request(name, "porter.test", reuse_key)
            start, end = "20200101000000Z", "21200101000000Z"
            if name == "expired":
                start, end = "20000101000000Z", "20010101000000Z"
            issuer = "ca"
            if name == "foreign":
                issuer = "other_ca"
            run_openssl(directory, "ca", "-batch", "-config", "ca.conf", "-cert", f"{issuer}.pem",
                        "-keyfile", f"{issuer}-key.pem", "-in", f"{name}.csr", "-out", f"{name}.pem", "-notext",
                        "-extensions", extension, "-startdate", start, "-enddate", end)
        run_openssl(directory, "ca", "-batch", "-config", "ca.conf", "-revoke", "revoked.pem")
        run_openssl(directory, "ca", "-batch", "-config", "ca.conf", "-gencrl", "-out", "crl.pem",
                    "-crldays", "36500")
        outputs = ["ca.pem", "other_ca.pem", "crl.pem"]
        for name, _, _ in identities:
            outputs += [f"{name}.pem", f"{name}-key.pem"]
        for name in outputs:
            shutil.copyfile(directory / name, destination / name)
            os.chmod(destination / name, 0o644)


if __name__ == "__main__":
    generate(Path(__file__).resolve().parent)
