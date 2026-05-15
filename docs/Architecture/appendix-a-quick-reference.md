# Appendix A: Quick Reference

### A.1 File Locations

| File | Path | Purpose |
|------|------|---------|
| Config | `%APPDATA%\VKey\config.toml` | User settings |
| Log | `%APPDATA%\VKey\logs\` | Debug logs |
| Dictionary | `%APPDATA%\VKey\dict\` | Custom words |

### A.2 Named Objects

| Object | Name | Type |
|--------|------|------|
| Shared memory | `Local\VKeySharedState` | File mapping |
| Config event | `Local\VKeyConfigReady` | Manual-reset event |

### A.3 Magic Numbers

| Value | Meaning |
|-------|---------|
| `0x59454B4E` | 'NKEY' - SharedState magic |
| `56` | SharedState size (bytes) |

---

> **Document Version:** 1.0
> **Last Updated:** 2025-02-03
> **Status:** First Principles Complete
