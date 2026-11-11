# ADR 0009 – Plugin signatures: Ed25519 with Monocypher, checked private copy

Status: **accepted** 2026-11-11 (P5; see [docs/plugins.md](../plugins.md) "Signing", decision D3)

**Context.** Plugins are Python code run with the user's rights. Only plugins of one publisher
(the project owner) may load; unsigned ones only in an explicit developer mode. The check must
work identically on macOS, Windows and RHEL 10 without depending on a system crypto library
(Qt ships none for signatures; OpenSSL is not available on every target in the same way).

**Decision.**
- Ed25519 (RFC 8032) through [Monocypher](https://monocypher.org) 4.0.2 (CC0 / BSD-2, two C
  files, audited), fetched at build time like CDT. SHA-256 file hashes through Qt.
- Signed data: `"nfcad-plugin-signature v1\n"` followed by `"<sha256 hex>  <path>\n"` for every
  file, sorted by path. The `SIGNATURE` file (JSON) holds the file list with hashes, the signer's
  public key and the signature. Paths are relative, with `/`, and may not leave the folder.
- Trusted keys are compiled in from `keys/trusted-publishers.txt`; there is no way to add keys at
  run time (an environment variable or setting would let any local program extend the trust).
- CAD copies **exactly the signed files** into a private temporary directory, hashing the bytes
  it writes, and runs the plugin from there: files added or changed after the check never run.
- Developer mode (setting or `NFCAD_DEVELOPER_MODE=1`) also loads unsigned or invalid plugins
  from their folder; they are marked `[unsigned]` / `[signature invalid]` and a red banner is
  shown in the status bar.
- Signing happens with the command line tool `nfcad-plugin` (keygen / pubkey / sign / verify) on
  the signer's machine; the secret key (32-byte seed, file mode 0600) never enters the
  repository.

**Consequences.** No new runtime dependency; signatures can be checked independently with any
Ed25519 implementation (tested against Python `cryptography`). A signature proves origin and
integrity, not harmlessness – there is no sandbox (ADR 0008). Replacing the publisher key
needs a new release; a lost secret key means re-signing all plugins with a new one.
