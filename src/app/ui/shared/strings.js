// NexusKey - Localized String Dictionary (English translations)
// Vietnamese is the default language (text in HTML), only English needs mapping.
// Keys match data-i18n attributes in HTML elements.

var STRINGS = {
    en: {
        // ── Common (shared across dialogs) ──
        "close": "Close",
        "add": "+ Add",
        "delete": "- Delete",
        "application": "Application",
        "refresh_list": "Refresh list",
        "pick_window": "\uD83C\uDFAF Pick window",
        "excluded_apps_title": "Excluded Apps",
        "no_apps_found": "No applications found",
        "no_matching": "No matching results",
        "no_running_apps": "No running applications",

        // ── Settings dialog ──
        "s.pin": "Pin to top",
        "s.code_table": "Code Table",
        "s.switch_key": "Switch Key",
        "s.switch_key_title": "Switch key",
        "s.beep": "Beep Sound",
        "s.beep_tooltip": "Play sound when switching language",
        "s.smart_switch": "Smart app exclusion",
        "s.smart_switch_tooltip": "Auto-remember input mode per app",
        "s.enable_exclude": "Enable app exclusion",
        "s.list": "List",
        "s.exclude_tooltip": "Type only English for listed apps",
        "s.advanced": "Advanced settings",
        "s.tab_input": "Input",
        "s.tab_macro": "Macro",
        "s.tab_system": "System",
        "s.tab_about": "About",

        // Tab 1: Input
        "s.modern_ortho": "Place diacritics: o\u00E0, u\u00FD (not \u00F2a, \u00FAy)",
        "s.auto_caps": "Auto-capitalize first letter",
        "s.allow_zwjf": "Allow \"z w j f\" as initial consonants",
        "s.spell_check": "Spell check",
        "s.restore_key": "Auto-restore keys on wrong word",
        "s.temp_off_spell": "Disable spell check with Ctrl",
        "s.temp_off_spell_tooltip": "Hold Ctrl while typing to skip spell check",
        "s.free_marking": "Free marking",
        "s.free_marking_tooltip": "Allow free diacritics without correct spelling (e.g., tiens \u2192 ti\u1EBFn)",
        "s.remember_code": "Remember code table per app",
        "s.temp_off_alt": "Disable NexusKey with Alt",
        "s.temp_off_alt_tooltip": "Double-tap Alt to disable Vietnamese input (avoid app menu conflicts)",

        // Tab 2: Macro
        "s.enable_macro": "Enable macro",
        "s.macro_english": "Use macro in English mode",
        "s.auto_caps_macro": "Auto-capitalize from macro",
        "s.quick_telex": "Quick Telex (cc=ch, gg=gi, nn=ng,...)",
        "s.quick_start": "Quick start consonant: f\u2192ph, j\u2192gi, w\u2192qu",
        "s.quick_end": "Quick end consonant: g\u2192ng, h\u2192nh, k\u2192ch",
        "s.temp_off_macro": "Skip macro with Esc",
        "s.temp_off_macro_tooltip": "Press Esc before typing to skip macro for next word",
        "s.macro_table_btn": "Macro table...",

        // Tab 3: System
        "s.run_startup": "Run on Windows startup",
        "s.show_startup": "Show settings on startup",
        "s.run_admin": "Run as Administrator",
        "s.desktop_shortcut": "Create desktop shortcut",
        "s.bg_opacity": "Background opacity",
        "s.icon_style": "Customize icon",
        "s.english_ui": "English interface",
        "s.english_ui_tooltip": "Switch UI, menus, and notifications to English",
        "s.custom_colors": "Custom icon colors",
        "s.color_v": "Choose V color",
        "s.color_e": "Choose E color",

        // Tab 4: About
        "s.tagline": "Modern Vietnamese Input Method",
        "s.version_label": "Version:",
        "s.dev_label": "Development & UI Design:",
        "s.origin_label": "Origin:",
        "s.origin_text": "Project inspired by OpenKey source code by Mai V\u0169 Tuy\u00EAn, aiming to improve the UI, fix bugs, and add new features.",
        "s.license": "NexusKey is open-source, free, and non-profit software.",
        "s.auto_update": "Auto-check for updates",
        "s.check_now": "Check now",
        "s.update_tooltip": "Check for new version on startup",

        // ── Macro dialog ──
        "m.title": "Macro Table",
        "m.shortcut": "Shortcut",
        "m.shortcut_ph": "e.g.: btw",
        "m.content": "Full content",
        "m.content_ph": "e.g.: by the way",
        "m.import": "Import from file...",
        "m.export": "Export to file...",
        "m.edit": "+ Edit",

        // ── Excluded Apps dialog ──
        "ea.app_ph": "e.g.: notepad.exe",
        "ea.excluded_list": "Excluded applications",

        // ── Convert Tool dialog ──
        "ct.title": "Convert Tool",
        "ct.general": "General Options",
        "ct.to_upper": "Convert to UPPERCASE",
        "ct.to_lower": "Convert to lowercase",
        "ct.remove_marks": "Remove diacritics",
        "ct.auto_paste": "Auto paste + select",
        "ct.caps_first": "Capitalize first letter",
        "ct.caps_each": "Capitalize Each Word",
        "ct.alert_done": "Alert when done",
        "ct.sequential": "Sequential convert",
        "ct.sequential_tooltip": "Press hotkey multiple times to cycle through options. Only available when 'Auto paste + select' is on.",
        "ct.options": "Options",
        "ct.clipboard": "Convert from Clipboard",
        "ct.choose_file": "Choose file",
        "ct.source_file": "Source file",
        "ct.no_file": "No file selected...",
        "ct.source_title": "Select source file",
        "ct.dest_file": "Destination file",
        "ct.dest_title": "Select destination file",
        "ct.hotkey_label": "Quick conversion hotkey:",
        "ct.encoding": "Code Table",
        "ct.source": "Source",
        "ct.dest": "Destination",
        "ct.swap": "Swap encoding",
        "ct.convert": "Convert",

        // ── About dialog (standalone) ──
        "a.title": "About NexusKey",
        "a.tagline": "Modern Vietnamese Input Method",
        "a.repo_nexuskey": "NexusKey Repo",
        "a.repo_openkey": "Original OpenKey Repo",
        "a.footer": "Powered by Sciter.JS for a smooth experience.",

        // ── App Overrides dialog ──
        "ao.title": "App Override",
        "ao.special": "Special handling",
        "ao.none": "None (default)",
        "ao.skip_ime": "Skip IME check",
        "ao.force_clip": "Force clipboard",
        "ao.list_app": "Application",
        "ao.list_handling": "Handling",
        "ao.list_clipboard": "Clipboard",
        "ao.app_ph": "e.g.: myapp.exe"
    }
};
