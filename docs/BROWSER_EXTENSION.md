# VKey Browser extension

VKey can accept a per-domain route from the companion
[VKey Browser](https://github.com/phatMT97/VKey-Browser) extension.

The extension offers three routes:

- **Default:** retain VKey's normal per-application behavior.
- **English:** temporarily bypass Vietnamese processing for the focused domain
  without changing the user's logical V/E setting.
- **TSF compatibility:** route the domain through VKey TSF. This is intended for
  browser editors that duplicate or attach replacement text after emoji, such
  as the XenForo cases reported in issue #92. VKey's TSF application support
  must be enabled; otherwise this route safely falls back to the normal hook.

## Installation and connection

Official VKey packages include `VKeyBrowserHost.exe` beside `VKey.exe` or
`VKeyClassic.exe`. At normal startup VKey writes same-user native-messaging
manifests under `%APPDATA%\VKey\native-messaging` and registers them for Chrome,
Edge, Brave, Vivaldi, and Firefox. Registration is idempotent and does nothing
when the companion binary is absent.

For an unpacked extension, load the `VKey-Browser` repository from the browser's
extension developer page, then restart VKey once so the native host manifests
are refreshed.

## Privacy and failure behavior

Only these values cross the extension/native boundary: protocol version,
browser executable name, focused state, effective route, and hostname. Full
URLs, paths, query strings, page contents, and keystrokes are neither requested
nor transmitted. The native host rejects messages over 8 KiB, unknown fields,
unsupported executable names, and non-hostname characters.

The host publishes a fixed-size versioned seqlock mapping. HookEngine normally
checks one 32-bit generation value per key; JSON parsing and browser APIs stay
outside the keyboard hook. State expires after five seconds if a browser or
extension crashes, and an app-level exclusion still has higher precedence than
a domain route.

## Address-bar limitation (#100)

Browser extension APIs do not expose text typed in the normal address bar.
Consequently the extension can apply a hostname route after navigation or tab
focus, but cannot detect `google.com` while the user is still typing it into the
omnibox. Implementations based on UI Automation were deliberately rejected:
they are browser-specific, fragile, and too expensive to place near the input
hook.

## TSF visual behavior

VKey registers its `ITfDisplayAttributeProvider` and applies its
`TF_LS_NONE` GUID atom to every live composition range. Supported hosts should
therefore not draw the usual composition underline/highlight. A host that
ignores TSF display attributes may still impose its own visual treatment.
