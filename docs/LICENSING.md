# Vesper — Licensing

## This repository

**Vesper is released under the GNU General Public License v3.0** (D-067,
2026-08-11) — see [`LICENSE`](../LICENSE). Taking the GPLv3 path on JUCE is what
permits `JUCE_DISPLAY_SPLASH_SCREEN=0` here (D-053).

## Commercial plan (not the licence this repository ships under)

Sable Audio holds the copyright and may offer Vesper under separate commercial
terms. What follows is a business design, not a grant, and it does not govern
this source tree:

- **Vesper** — $199 perpetual license, 3 machines, free 1.x updates
- 14-day full trial (time-limited, no audio crippling — respect the user)
- A closed-source commercial build would require a JUCE commercial seat bought
  before distribution, and a signed Steinberg VST3 agreement

## Dependencies & obligations

| Component | License | Obligation |
|---|---|---|
| JUCE 7 (pinned 7.0.12) | GPLv3 **or** commercial (per-seat). The GPLv3 path is what legitimises `JUCE_DISPLAY_SPLASH_SCREEN=0` | ✅ **Resolved 2026-08-11 (D-067): this project is released under GPLv3**, so the disabled splash (D-053) is permitted and no commercial seat is required. A future closed-source commercial build would need a JUCE commercial licence bought *before* distribution. |
| VST3 SDK | Steinberg Dual (GPL/proprietary agreement) | Sign the Steinberg VST3 agreement for closed-source distribution. |
| AU | Apple SDK terms | Ship via signed/notarized installer. |
| AAX | Avid NDA + PACE | Developer account + PACE signing before any AAX build leaves the machine. |

Trademark note: run a clearance search on "Vesper" in audio-software classes
before commercial launch (see `docs/BRANDING.md`).

## Activation design (v1.x, not yet wired)

Offline-first serial activation:

1. Purchase issues a serial `VSPR-XXXX-XXXX-XXXX` (Ed25519-signed payload:
   product, seat count, purchase date).
2. First run: user pastes serial → local verification against the embedded
   public key → writes `license.json` next to the presets.
3. No phone-home requirement; a machine-count courtesy check can run when
   online, but the plugin **never disables audio** over connectivity.
4. Trial = signed time-limited license file from the website.

Anti-goals: kernel-level DRM, iLok-only, online-only. They cost more goodwill
than they save revenue at this price point.

## Distribution

- Direct (site) at launch; Plugin Boutique / ADSR at 1.1
- Updates via in-product "version available" check (a static JSON fetch —
  no telemetry beyond the version string requested)
