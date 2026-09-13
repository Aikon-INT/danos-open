# Security Policy

## Reporting a vulnerability

DANOS-Open is a network operating system control plane. Report
suspected vulnerabilities **privately** via GitHub
"Report a vulnerability" (Security Advisories) on this repository —
do not open public issues for exploitable findings.

Include: affected component (e.g. gnmi_grpc, vpp_netlink, mgrd),
reproduction steps or PoC, and the release/tag affected.

## Scope notes

- The gNMI/gRPC server runs h2c by default; deploy TLS termination
  in front (see docs/threading.md notes) for untrusted networks.
- mgrd requires elevated privileges for kernel programming; run it
  in a container or dedicated namespace.

## Supported versions

Latest tagged release only (see git tags).
