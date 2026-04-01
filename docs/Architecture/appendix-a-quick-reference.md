# Appendix A: Quick Reference

### A.1 File Locations

| File | Path | Purpose |
|------|------|---------|
| Config | `%APPDATA%\NexusKey\config.toml` | User settings |
| Log | `%APPDATA%\NexusKey\logs\` | Debug logs |
| Dictionary | `%APPDATA%\NexusKey\dict\` | Custom words |

### A.2 Named Objects

| Object | Name | Type |
|--------|------|------|
| Shared memory | `Local\NexusKeySharedState` | File mapping |
| Config event | `Local\NexusKeyConfigReady` | Manual-reset event |

### A.3 Magic Numbers

| Value | Meaning |
|-------|---------|
| `0x59454B4E` | 'NKEY' - SharedState magic |
| `56` | SharedState size (bytes) |

---

> **Document Version:** 1.0
> **Last Updated:** 2025-02-03
> **Status:** First Principles Complete
