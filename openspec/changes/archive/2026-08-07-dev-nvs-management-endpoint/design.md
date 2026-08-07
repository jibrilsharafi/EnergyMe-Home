## Context

See proposal.md - Why. `factory_ns` and every other namespace live in the single shared `nvs` partition; there is no per-namespace erase/reflash short of a full-partition rewrite. Endpoints are dev-only (`#ifdef ENV_DEV`), registered in `_serveApi()` alongside `_serveShadowDevEndpoints()` / `_serveCrashTestEndpoints()`.

## Goals / Non-Goals

**Goals:**
- Browse and edit any NVS namespace/key on a live device over HTTP, without a full-partition reflash.
- Never leak credential-shaped values, even on a dev-only surface.

**Non-Goals:**
- Blob-type values and narrow int types (u8/i8/u16/i16) are listed (key + type) but not decoded to a value - not worth the branches for a recovery tool.
- No production exposure - `ENV_DEV` gating is the only access control, matching the existing shadow/crash-test dev endpoints.

## Decisions

- **Enumeration via ESP-IDF `nvs_entry_find`/`nvs_entry_next`, value read/write via Arduino `Preferences`.** The IDF iterator is the only way to discover namespaces and keys without knowing them in advance; `Preferences` is reused for actual get/put since it's already the app-wide NVS access pattern - avoids a second, parallel read/write path.
- **Redaction by key-name fragment match (`pass`, `pwd`, `token`, `secret`, `key`, `cert`, `cred`), not an explicit allowlist.** A newly added credential-shaped key is redacted by default instead of leaking until someone remembers to allowlist it. The endpoint is dev-only and gated by the same admin password it would otherwise expose, so this is defense in depth, not the primary control. Supersedes the proposal's narrower "redact `cert_pem`/`key_pem` by name" - fragment matching covers the general case those two keys were an example of.
- **String reads prime the buffer and check the return value.** `Preferences::getString()` returns 0 without touching the output buffer when the stored string exceeds it, so an unchecked call would serialize uninitialized stack into the HTTP response. Buffer is zeroed first; a 0 return renders `"<unreadable or too long>"` instead.
- **Iteration bounded by `NVS_DEBUG_MAX_ENTRIES` / `NVS_DEBUG_MAX_NAMESPACES`.** Matches the project convention that every unbounded loop needs an explicit cap.

## Risks / Trade-offs

- [Fragment-match redaction could over-redact a legitimate non-secret key containing e.g. "key" as a substring] -> Acceptable for a dev-only recovery tool; false positives just mean re-reading the value another way, not a functional break.
- [Direct NVS writes bypass whatever validation the owning module normally applies to that key] -> Scope is explicitly bench-device recovery by someone who already knows the correct value (e.g. `pcb_revision`); not exposed in production builds.
