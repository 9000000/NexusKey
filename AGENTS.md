# NexusKey repository instructions

## Public/private file placement gate

Classify every new file before creating it. Tell the user which destination was
chosen and why whenever the choice is not obvious.

Put a file in the public Main repository only when users or public contributors
need it to build, run, test, understand, audit, license, or use VKey. Product
source, public tests, build metadata, CI, user documentation, security design and
license/trust metadata belong in Main.

Put a file in the private `internal/` submodule when it is maintainer or agent
working material: plans/design notes, handoffs, TODO ledgers, baselines, corpora,
private architecture/rules, benchmarks, release orchestration, credential-backed
private-repository workflows, and one-off development tools.

If uncertain, do not put the file in Main. Propose the placement to the user and
default to `internal/`. If the private submodule is unavailable, ask for it to be
initialized; never fall back to publishing private material.

Before adding a public file, classify the intended path explicitly:

```bash
python3 tools/audit/check_public_tree.py path/to/proposed-file
```

Top-level public `tools/` are deny-by-default. A genuinely user/build/CI-facing
tool needs a reviewed entry with its public purpose in
`tools/audit/public_tools_allowlist.txt`. Never add credentials, private keys or
token values to either repository.

When moving material into `internal/`, commit the submodule first and then commit
the updated `internal` gitlink in Main. Removing a public file only removes it
from the current tree; rewriting public history is a separate, exceptional
operation reserved for exposed secrets or content that legally must be purged.

This placement policy is duplicated in `internal/CLAUDE.md` so Claude receives
the same rule. Keep both copies aligned whenever the policy changes.
