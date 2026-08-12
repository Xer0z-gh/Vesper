# Running Vesper system-wide via Equalizer APO (Windows host)

Vesper can process **all system audio** on the Windows host (Spotify,
games, browser — everything on a chosen output device) through Equalizer
APO. This is separate from Ableton (which loads the VST3 directly in the VM).

## The catch (why a plain drag-in fails)

Vesper is **VST3 only** (JUCE dropped VST2; Steinberg discontinued that SDK,
so a VST2 build isn't legally possible). **Stock Equalizer APO 1.4.2 — the
version installed here — hosts VST2 only, not VST3.** So Vesper won't appear
in its plugin list as-is. Two working routes:

### Route A — the VST3-capable fork, engine-swap (implemented 2026-07-18)
The fork "EqAPO64 with VST3 support" (V2.0.4) adds native VST3 hosting.
Its **installer hangs on "creating a restore point"** on this machine, so
instead of running it we extracted its payload with 7-Zip and swap the
program files into the existing 1.4.2 install (same APO GUID + location, so
the device hook and config survive).

- Fork payload extracted to: `Documents\EqAPO-VST3-extracted\`
  (fork `EqualizerAPO.dll` 835 KB vs stock 648 KB — the VST3 SDK host).
- One-shot installer: **`Desktop\Install-Vesper-EqualizerAPO.ps1`**
  (self-elevates · backs up to `Documents\EqualizerAPO-backup-<stamp>` ·
  robocopies the fork over the install, **excluding `config\`** so the
  device setup is preserved · stages `Vesper.vst3` to Common Files\VST3 ·
  prints rollback = `robocopy <backup> <install> /MIR`).

Steps:
1. Run `Desktop\Install-Vesper-EqualizerAPO.ps1` (right-click → Run with
   PowerShell; it asks for admin).
2. **Reboot** (APO engine loads on the next audio session).
3. `C:\Program Files\EqualizerAPO\Editor.exe` → add filter → **`OutProcVSTPlugin`**
   (isolated, recommended) or `VSTPlugin` → point at
   `C:\Program Files\Common Files\VST3\Vesper.vst3`.
4. Click the filter to open Vesper's editor; it now processes the device
   Equalizer APO is active on. Keep Vesper's Limiter on as a safety net.

Fork notes: VST3, like VST2, isn't universal — best with lightweight
effect plugins; the out-of-process host (`OutProcVSTPlugin:`) isolates the
plugin from the audio engine so a plugin issue can't take down system audio.

**If the restore-point hang blocks the installer another time:** temporarily
turn off System Protection (System Properties → Protection → Configure →
Disable), run the installer, re-enable — or just use the engine-swap script
above, which avoids the installer entirely.

### Route B — a VST3→VST2 wrapper in stock EQ APO
If you keep stock EQ APO, wrap Vesper with a bridge that exposes a VST2
interface (e.g. Blue Cat's PatchWork, paid). Add the wrapper as the VST
plugin in EQ APO's Configuration Editor, then load Vesper.vst3 inside it.

## Staged files (ready now)
- Host VST3 for any host: `Documents\Vesper VST3\Vesper.vst3` (Program
  Files needs admin; copy it to `C:\Program Files\Common Files\VST3\`
  yourself when convenient).
- The installer (`Desktop\Vesper-1.0.0-Setup.exe`) also installs the VST3
  to the standard host location if you run it on the host with admin.

## Safety notes
- System-wide processing affects ALL host audio — keep Vesper's output
  gain/limiter sane to avoid surprises. The true-peak limiter (on by
  default in mastering presets) is a good safety net on the master.
- Equalizer APO processes on the host; it does NOT touch the VM's Ableton.

Sources: Equalizer APO VST3 support status —
https://sourceforge.net/p/equalizerapo/tickets/275/ ·
https://sourceforge.net/projects/eqapo64-with-vst3-support/
