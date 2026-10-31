# Security Policy

## Supported versions

| Version | Supported |
|---|---|
| 0.1.x (alpha) | Yes, security and release-blocking fixes |
| older | No |

## Reporting a vulnerability

Do **not** open a public issue for a vulnerability.

Use GitHub's **Security → Report a vulnerability** flow on the repository. If
that flow is unavailable, contact the maintainers privately through the address
listed in the repository profile. Include:

- affected version and platform;
- a minimal reproducer or archive with the triggering asset;
- the impacted subsystem (VFS, `.femarc` decoder, VP9 path, network, plugin ABI,
  cooker, renderer);
- impact assessment and any suggested fix.

## Scope

Especially relevant areas:

- `.femarc` / `.fempak` parsing, decompression and decryption;
- VFS path traversal and mapped-file lifetime;
- VP9/WebM demuxing and decoder lifetime;
- plugin ABI loading and component layout validation;
- network snapshot validation and rollback restore;
- asset cooker and release-key handling.

## Disclosure

We aim to acknowledge a report within 7 days and to publish a fixed release
with a coordinated advisory. Please give us a reasonable window before public
disclosure.
