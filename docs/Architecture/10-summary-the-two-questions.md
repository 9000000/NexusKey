# 10. Summary: The Two Questions

Every component interaction answers one of two questions:

| Question | Answered By | When Asked |
|----------|-------------|------------|
| **"What are the rules?"** | config.toml | At word boundary |
| **"Is there news?"** | SharedState | Anytime (cheap glance) |

Everything else stays local to the engine.

---
