# Desktop UI simulator

The UI lives in the portable `ui/` module, shared by the firmware and a desktop simulator. The simulator
renders the screens to PNG on your computer, so you can check layout and data before flashing. No hardware
needed.

```powershell
# one-off: install gcc (Windows example)
scoop install gcc
$env:PATH = "$env:USERPROFILE\scoop\apps\gcc\current\bin;$env:PATH"

pio run -d experiments/sim
.\experiments\sim\.pio\build\sim\program.exe .\experiments\sim\out   # writes out/01..04_*.png and more
```

It renders mock data by default (live usage now needs an OAuth token, which lives on the device). Validating
UI changes in the simulator before flashing is a project rule; see
[ADR-0005](../adr/0005-simulator-over-device-screenshot.md).
