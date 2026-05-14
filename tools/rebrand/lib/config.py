"""Loader for tools/rebrand/rules.toml."""
from __future__ import annotations
import re
import tomllib
from dataclasses import dataclass, field
from pathlib import Path
from typing import Dict, List


class ConfigError(Exception):
    pass


GUID_RE = re.compile(r"^\{[0-9A-Fa-f]{8}-[0-9A-Fa-f]{4}-[0-9A-Fa-f]{4}-[0-9A-Fa-f]{4}-[0-9A-Fa-f]{12}\}$")


@dataclass
class Config:
    old_brand: str
    new_brand: str
    keep_namespace: str
    keep_log_macro: str
    scope_include: List[str]
    scope_exclude: List[str]
    replacements: Dict[str, str] = field(default_factory=dict)
    namespace_keep_regex: List[str] = field(default_factory=list)
    brand_to_binary_regex: List[str] = field(default_factory=list)
    file_renames: Dict[str, str] = field(default_factory=dict)
    old_clsid: str = ""
    new_clsid: str = ""
    old_profile_guid: str = ""
    new_profile_guid: str = ""

    def validate_for_apply(self) -> None:
        for label, val in [
            ("new_clsid_text_service", self.new_clsid),
            ("new_profile_guid", self.new_profile_guid),
        ]:
            if val == "TBD-uuidgen" or not GUID_RE.match(val):
                raise ConfigError(
                    f"GUID '{label}' not filled or malformed: {val!r}. "
                    f"Run `uuidgen` and paste a value like "
                    f"'{{12345678-1234-1234-1234-123456789abc}}'."
                )


def _require(d: dict, key: str, ctx: str) -> object:
    if key not in d:
        raise ConfigError(f"missing required key '{key}' in {ctx}")
    return d[key]


def load_rules(path: Path) -> Config:
    with open(path, "rb") as f:
        data = tomllib.load(f)
    meta = _require(data, "meta", "rules.toml")
    scope = _require(data, "scope", "rules.toml")
    repl = _require(data, "replacements", "rules.toml")
    guids = data.get("persistent_id", {}).get("guids", {})
    ns_keep = data.get("exclude_patterns", {}).get("namespace_keep", {})
    recat = data.get("recategorize", {})

    pooled: Dict[str, str] = {}
    for section in ("brand_string", "binary_name", "runtime_ipc",
                    "persistent_id", "release_artifact"):
        for k, v in (repl.get(section) or {}).items():
            if k in pooled and pooled[k] != v:
                raise ConfigError(
                    f"replacement key collision: {k!r} maps to {pooled[k]!r} "
                    f"and {v!r}"
                )
            pooled[k] = v

    return Config(
        old_brand=str(_require(meta, "old_brand", "[meta]")),
        new_brand=str(_require(meta, "new_brand", "[meta]")),
        keep_namespace=str(_require(meta, "keep_namespace", "[meta]")),
        keep_log_macro=str(_require(meta, "keep_log_macro", "[meta]")),
        scope_include=list(scope.get("include", [])),
        scope_exclude=list(scope.get("exclude", [])),
        replacements=pooled,
        namespace_keep_regex=list(ns_keep.get("regex", [])),
        brand_to_binary_regex=list(recat.get("brand_to_binary_regex", [])),
        file_renames=dict(data.get("file_renames", {})),
        old_clsid=str(guids.get("old_clsid_text_service", "")),
        new_clsid=str(guids.get("new_clsid_text_service", "")),
        old_profile_guid=str(guids.get("old_profile_guid", "")),
        new_profile_guid=str(guids.get("new_profile_guid", "")),
    )
