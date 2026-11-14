# CalendarManipulator

SKSE plugin for Skyrim. Lets you shorten the in-game months: once the day of the month passes `daysPerMonth`, the calendar jumps to the 1st of the next month.

## Build with ClibDT

1. Run `ClibDT.exe setup --root <your dev folder>` once, if you haven't already.
2. Import this project:
   ```
   ClibDT.exe import <path to this folder>
   ```
3. Compile it:
   ```
   ClibDT.exe compile CalendarManipulator
   ```
   Use `--target se`, `ae` or `vr` to build for one game version only, `--debug` for a debug build, and `--clean` to rebuild from scratch.

You can also do steps 2 and 3 from the ClibDT window (Compile page).

## Install

- Put the built `CalendarManipulator.dll` in `Data/SKSE/Plugins`.
- Put `contrib/SKSE/Plugins/CalendarManipulator.ini` next to it.

## Settings (`CalendarManipulator.ini`)

| Key | Default | What it does |
|---|---|---|
| `daysPerMonth` | `31` | Days each month lasts (1–31). Months that are naturally shorter are unaffected if this is larger. |
| `MonthlySeasons` | `false` | Advance the year on every Winter → Spring transition instead of at Evening Star → Morning Star. |
| `DisplayNotifications` | `true` | Show a notification on the 1st of every new month (or describe the new season when `MonthlySeasons` is on). |
