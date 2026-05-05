"""Tone/modifier overlay — render NfaState as visible text + compute Action.

D-1 Task 6. Layered ON TOP of Nfa/Dfa structure (built in Tasks 3-5):
  - render_state(NfaState) → visible Vietnamese text
  - Action dataclass: minimal Replace operation
  - compute_action(src_text, dst_text) → Action
  - apply_overlay(dfa, nfa) → annotate dfa.transitions with action_id
"""
from __future__ import annotations

from dataclasses import dataclass, field

from .dfa import Dfa
from .nfa import Nfa, NfaState
from .parser import KeymapRule


# ─────────────────────────────────────────────────────────────────────────
# Vietnamese tone-mark table — vowel × tone → toned char.
# ─────────────────────────────────────────────────────────────────────────

# tone names match phonotactics.toml [tones].all.
_TONE_TABLE: dict[tuple[str, str], str] = {
    # a  á à ả ã ạ
    ("a", "sắc"): "á", ("a", "huyền"): "à", ("a", "hỏi"): "ả",
    ("a", "ngã"): "ã", ("a", "nặng"): "ạ",
    # ă
    ("ă", "sắc"): "ắ", ("ă", "huyền"): "ằ", ("ă", "hỏi"): "ẳ",
    ("ă", "ngã"): "ẵ", ("ă", "nặng"): "ặ",
    # â
    ("â", "sắc"): "ấ", ("â", "huyền"): "ầ", ("â", "hỏi"): "ẩ",
    ("â", "ngã"): "ẫ", ("â", "nặng"): "ậ",
    # e
    ("e", "sắc"): "é", ("e", "huyền"): "è", ("e", "hỏi"): "ẻ",
    ("e", "ngã"): "ẽ", ("e", "nặng"): "ẹ",
    # ê
    ("ê", "sắc"): "ế", ("ê", "huyền"): "ề", ("ê", "hỏi"): "ể",
    ("ê", "ngã"): "ễ", ("ê", "nặng"): "ệ",
    # i
    ("i", "sắc"): "í", ("i", "huyền"): "ì", ("i", "hỏi"): "ỉ",
    ("i", "ngã"): "ĩ", ("i", "nặng"): "ị",
    # o
    ("o", "sắc"): "ó", ("o", "huyền"): "ò", ("o", "hỏi"): "ỏ",
    ("o", "ngã"): "õ", ("o", "nặng"): "ọ",
    # ô
    ("ô", "sắc"): "ố", ("ô", "huyền"): "ồ", ("ô", "hỏi"): "ổ",
    ("ô", "ngã"): "ỗ", ("ô", "nặng"): "ộ",
    # ơ
    ("ơ", "sắc"): "ớ", ("ơ", "huyền"): "ờ", ("ơ", "hỏi"): "ở",
    ("ơ", "ngã"): "ỡ", ("ơ", "nặng"): "ợ",
    # u
    ("u", "sắc"): "ú", ("u", "huyền"): "ù", ("u", "hỏi"): "ủ",
    ("u", "ngã"): "ũ", ("u", "nặng"): "ụ",
    # ư
    ("ư", "sắc"): "ứ", ("ư", "huyền"): "ừ", ("ư", "hỏi"): "ử",
    ("ư", "ngã"): "ữ", ("ư", "nặng"): "ự",
    # y (treated as i variant in spelling)
    ("y", "sắc"): "ý", ("y", "huyền"): "ỳ", ("y", "hỏi"): "ỷ",
    ("y", "ngã"): "ỹ", ("y", "nặng"): "ỵ",
}


# Inverse modifier map for escape recovery.
_UNMODIFIED = {
    "â": "a", "ă": "a",
    "ê": "e",
    "ô": "o", "ơ": "o",
    "ư": "u",
}


# ─────────────────────────────────────────────────────────────────────────
# Tone placement — where in the vowel sequence does the tone mark go?
# Per phonotactics.toml [tone_placement].
# ─────────────────────────────────────────────────────────────────────────


def _tone_position(vowels: tuple[str, ...], has_coda: bool) -> int:
    """Return 1-indexed position of tone-bearing vowel.

    Rules:
      1 vowel   → position 1
      2 vowels  → 1 if no coda else 2
      3 vowels  → 2 (special: uyê → 3, but uyê handled at vowel-encoding
                     time — when state has 3 vowels with last being 'ê',
                     position is 3)
    """
    n = len(vowels)
    if n == 0:
        return 0
    if n == 1:
        return 1
    if n == 2:
        return 2 if has_coda else 1
    # 3+ vowels
    if n >= 3 and len(vowels) >= 3 and vowels[2] == "ê":
        return 3   # uyê exception
    return 2


# ─────────────────────────────────────────────────────────────────────────
# Public API
# ─────────────────────────────────────────────────────────────────────────


@dataclass(frozen=True)
class Action:
    """Minimal text-replacement to apply when a transition fires.

    Translates to NextKey::Engine::Action at codegen time:
      - bs_count = number of trailing chars to delete from current buffer
      - insert_text = Vietnamese chars to append after deletion
    """

    bs_count: int = 0
    insert_text: str = ""

    def is_noop(self) -> bool:
        return self.bs_count == 0 and self.insert_text == ""


def render_state(s: NfaState) -> str:
    """Visible Vietnamese text for an NFA state.

    Handles non-escape states. For escape states, caller should derive
    text from src + transition (see compute_escape_text).
    """
    if s.is_start:
        return ""

    if s.is_escape:
        # Escape state encodes the literal vowels + unmodified bases.
        # Render as cons + each vowel as-is + coda (no tone applied).
        return s.cons + "".join(s.vowels) + s.coda

    text = s.cons
    if not s.vowels:
        return text + s.coda

    pos = _tone_position(s.vowels, bool(s.coda))
    for i, v in enumerate(s.vowels):
        if (i + 1) == pos and s.tone != "none":
            v = _TONE_TABLE.get((v, s.tone), v)
        text += v
    text += s.coda
    return text


def compute_action(src_text: str, dst_text: str) -> Action:
    """Minimal Replace operation from src to dst, byte-aligned by codepoint.

    Common-prefix length determines how many chars survive; the rest of
    src is deleted (bs_count) and dst tail is inserted.
    """
    src_chars = list(src_text)
    dst_chars = list(dst_text)
    common = 0
    while (common < len(src_chars) and common < len(dst_chars)
           and src_chars[common] == dst_chars[common]):
        common += 1
    bs_count = len(src_chars) - common
    insert_text = "".join(dst_chars[common:])
    return Action(bs_count=bs_count, insert_text=insert_text)


@dataclass
class OverlayResult:
    """Output of apply_overlay — annotation alongside DFA."""

    # transition_actions[src_id][sym] → index into action_table
    transition_actions: dict[int, dict[str, int]] = field(default_factory=dict)
    # Deduplicated action table — many transitions share same Action.
    action_table: list[Action] = field(default_factory=list)
    # State render text for debugging + exhaustive verifier (Task 7).
    state_render: dict[int, str] = field(default_factory=dict)


def apply_overlay(dfa: Dfa, nfa: Nfa, keymap: KeymapRule) -> OverlayResult:
    """Compute Action per DFA transition.

    For each (src_state, input_symbol, dst_state) edge, derive the visible
    text on both sides (using a canonical NFA-state representative from
    each DFA state's frozenset), then compute the minimal Replace action.

    Escape-state destinations are rendered specially: from src_text + the
    raw character that triggered the escape (recovered by reverse-mapping
    the input_symbol against keymap).
    """
    result = OverlayResult()

    # Build reverse keymap: AbstractInput name → raw char (for escape).
    # When multiple raw keys map to same abstract input (e.g., 's' and 'S'
    # both → TONE_SAC), prefer lowercase.
    inv_keymap: dict[str, str] = {}
    for raw, abstract in keymap.mappings.items():
        if abstract not in inv_keymap or raw.islower():
            inv_keymap[abstract] = raw

    # Index DFA states by ID for O(1) lookup (was O(n) per transition →
    # 14 billion ops on real Vietnamese rules).
    state_by_id: dict[int, "DfaState"] = {s.state_id: s for s in dfa.states}

    # Pre-render canonical text per DFA state.
    dfa_text: dict[int, str] = {}
    for sid, ds in state_by_id.items():
        if not ds.nfa_states:
            dfa_text[sid] = ""
            continue
        canonical = next(
            (ns for ns in ds.nfa_states if not ns.is_escape),
            next(iter(ds.nfa_states)),
        )
        dfa_text[sid] = render_state(canonical)
    result.state_render = dict(dfa_text)

    # Pre-compute pure-escape flag per DFA state.
    is_pure_escape: dict[int, NfaState | None] = {}
    for sid, ds in state_by_id.items():
        esc = next((n for n in ds.nfa_states if n.is_escape), None)
        if esc is not None and not any(not n.is_escape for n in ds.nfa_states):
            is_pure_escape[sid] = esc
        else:
            is_pure_escape[sid] = None

    # Action interning.
    action_to_id: dict[Action, int] = {}

    def intern(action: Action) -> int:
        if action not in action_to_id:
            action_to_id[action] = len(result.action_table)
            result.action_table.append(action)
        return action_to_id[action]

    for src_id, edges in dfa.transitions.items():
        src_text = dfa_text.get(src_id, "")
        for sym, dst_id in edges.items():
            esc_nfa = is_pure_escape.get(dst_id)
            if esc_nfa is not None:
                dst_text = _escape_text(esc_nfa, sym, inv_keymap)
            else:
                dst_text = dfa_text.get(dst_id, "")
            action_id = intern(compute_action(src_text, dst_text))
            result.transition_actions.setdefault(src_id, {})[sym] = action_id

    return result


def _escape_text(esc: NfaState, input_sym: str, inv_keymap: dict[str, str]) -> str:
    """Render literal text for an escape state arrived at via input_sym.

    Two escape kinds:
      - INSERT_LITERAL_x  (modifier escape, e.g., 'â' + 'a' → 'aa')
        Literal = cons + base_vowels + raw_char + coda.
      - TONE_X            (tone escape, e.g., 'á' + TONE_SAC → 'as')
        Literal = cons + bare_vowel + raw_tone_char + coda.
    """
    # Strip modifiers from vowels — escape recovers base form.
    bare_vowels = "".join(_UNMODIFIED.get(v, v) for v in esc.vowels)

    if input_sym.startswith("INSERT_LITERAL_"):
        ch = input_sym[len("INSERT_LITERAL_"):]
        return esc.cons + bare_vowels + ch + esc.coda
    if input_sym.startswith("TONE_"):
        raw = inv_keymap.get(input_sym, "")
        return esc.cons + bare_vowels + raw + esc.coda
    if input_sym == "MOD_HORN":
        raw = inv_keymap.get("MOD_HORN", "")
        return esc.cons + bare_vowels + raw + esc.coda
    # Fallback — unknown escape trigger; emit bare form.
    return esc.cons + bare_vowels + esc.coda
