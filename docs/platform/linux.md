# KotorPlus on Linux (Steam, Proton)

Works with KOTOR 1 from Steam, as a system package or the Flatpak. Run the commands from the KotorPlus folder.

## Install

1. Close KOTOR, then check the game version:

   ```
   tools/kotor-proton.sh check
   ```

   It must end in `match`. `MISMATCH` means this is not the Steam v1.03 `swkotor.exe`, and KotorPlus will not work.

2. Install. With no path it uses `out/dinput8.dll` (see [Build on Linux](../../README.md#build-on-linux)):

   ```
   tools/kotor-proton.sh install
   tools/kotor-proton.sh install /path/to/dinput8.dll
   ```

   An existing `dinput8.dll` is kept as `dinput8.dll.bak-<timestamp>`. An existing `KotorPlus.ini` is never overwritten.

## Launch option

Proton ignores the DLL without this. In Steam, open KOTOR > Properties > General > Launch Options and paste:

```
WINEDLLOVERRIDES="dinput8=n,b" %command%
```

The script never changes Steam's settings; you paste this yourself.

## Check that it loaded

Start the game, wait for the main menu, then run:

```
tools/kotor-proton.sh log
```

It prints `KotorPlus.log`. Exit code 0 (`echo $?`) means the mod loaded and every patch was applied. Otherwise it says
what is wrong:

| Message | Meaning |
|---|---|
| `no KotorPlus.log` | The DLL did not load: check the launch option, then start the game again |
| `missing from the log` | The game has not reached the main menu, or the exe is not v1.03 (run `check`) |
| `the log has a SKIP line` | A patch did not match this exe (run `check`) |

## Uninstall

Close KOTOR, then:

```
tools/kotor-proton.sh uninstall
```

This removes `dinput8.dll` and `KotorPlus.ini` (with any settings you changed) and puts the newest backup back. If you
installed several times, that backup is the previous KotorPlus build: run it again until it says
`nothing installed by this script`. Then remove the launch option in Steam.

## Other messages

- `KOTOR not found` (exit 3): Steam has no installed copy of KOTOR 1 in any of its libraries.
- `close KOTOR first` (exit 5): the game is still running.
