# AirConnect visual system

Native media utility, not a dashboard. The receiver identity and real connection
state are the focus. The video lives in a separate quiet viewer.

- AMOLED dark: true black #000000, inset #101010, text #F1F3F6, secondary #A8B0BB,
  blue #347FD6, green #69B990 for confirmed discovery.
- Light: white #FFFFFF, canvas #F7F8FA, text #1D2530, secondary #5C6775,
  blue #246BC5. Borders organize controls; no decorative shadows or gradients.
- Segoe UI: 28 receiver identity, 23 settings headings, 17 section titles,
  14 body and controls, 12 supporting text. Segoe MDL2 Assets for system glyphs.
- Main: compact brand row; receiver identity with a hand-drawn display mark;
  clear connection instructions; network status and help in a quiet footer.
- Settings: a native owner-drawn category rail, structured form content, real
  native edits/combos/toggles, and a fixed Apply/Cancel footer. Keyboard focus
  stays visible. Main content has no card grid or sidebar navigation.
- Viewer: picture first, native controls appear on pointer/keyboard activity.

Main and settings canvases, navigation and supported native title bars use true
black in the default AMOLED theme. The viewer has one 38 DIP ellipsis button,
with an owner-drawn native black popup and standard menu keyboard behavior.
Controls hide after inactivity. Portrait/landscape client geometry follows the
incoming picture; normal placements persist and are clamped to visible monitors.
