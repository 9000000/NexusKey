# Test Organization Strategy

```
Tests are organized by PHONOLOGICAL CATEGORY, not by function.
This ensures we test the LANGUAGE RULES, not the implementation.
```

| Category | # Tests | Priority | Coverage |
|----------|---------|----------|----------|
| 1. Basic Vowels | 6 | High | Foundation |
| 2. Circumflex | 12 | High | aa→â, ee→ê, oo→ô |
| 3. Breve | 6 | High | aw→ă |
| 4. Horn | 15 | Critical | ow→ơ, uw→ư, auto-ươ |
| 5. Stroke | 6 | Medium | dd→đ |
| 6. Tones | 30 | High | s,f,r,x,j on all vowels |
| 7. Falling Diphthongs | 24 | Critical | ai,ao,au,ay... |
| 8. Rising Diphthongs | 12 | Critical | oa,oe,uy,uê |
| 9. Triphthongs | 16 | High | iêu,ươi,oai... |
| 10. W-Modifier Priority | 14 | Critical | 7-level priority |
| 11. Tone Placement | 18 | Critical | Horn>Modified>Diphthong>Default |
| 12. Tone Relocation | 8 | High | After horn applied |
| 13. Escape Mechanism | 14 | High | Double-key escape |
| 14. Edge Cases | 20 | Medium | Complex words |
| 15. Real Words | 40 | Critical | Dictionary validation |
| **16. Stop-Final Tone Block** | **15** | **High** | **spellCheck: ocr,atr,apr...** |
| **TOTAL** | **~235** | | |

---
