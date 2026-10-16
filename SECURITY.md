# Security Policy

## Supported Versions

| Version | Supported |
|---------|-----------|
| Newest release | Yes |
| Older releases | No |

The launcher updates itself to the newest release, so a fix ships as a new release.

## Verifying a Download

Each file of a release has a Sigstore bundle signed by recomp.team@gmail.com. The `.intoto.jsonl` files hold its SLSA
provenance. To check a file, here the installer:

```
uvx sigstore verify identity --cert-identity recomp.team@gmail.com --cert-oidc-issuer https://accounts.google.com --bundle DisruptorRecomp-win-Setup.exe.sigstore.json DisruptorRecomp-win-Setup.exe
```

## Reporting a Vulnerability

If you discover a security vulnerability, please report it responsibly:

1. **Email**: [recomp.team@gmail.com](mailto:recomp.team@gmail.com)
2. **Subject**: `[DisruptorRecomp] Security: <brief description>`
3. Include steps to reproduce, the affected release and the potential impact

Please do **not** open a public GitHub issue for security vulnerabilities.

## Response Timeline

- Acknowledgement: within 48 hours
- Initial assessment: within 7 days
- Fix or mitigation: depends on severity, typically within 30 days
