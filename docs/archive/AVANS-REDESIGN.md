# Avans-style HMI redesign

> **HISTORICAL DESIGN NOTES.** These were written for an earlier interface and are not a specification or validation of the current native PySide6 UI. See [HMI README](../../HMI/README.md).

Changes in this build:
- Avans-inspired red / off-white / black visual identity.
- Avans-style wordmark asset in the left navigation.
- Light engineering workspace with a dark dedicated control panel.
- Higher-contrast controls, cards, status badges and tables.
- Flow velocity trend made substantially more readable:
  - thicker actual line;
  - dashed setpoint line;
  - larger axis labels;
  - cleaner grid;
  - fewer time-axis labels;
  - fixed zero-based speed scale based on configured maximum airspeed.
- Existing fixed-viewport HMI behaviour is retained; overview controls remain available without page scrolling at desktop HMI sizes.

The supplied Avans wordmark is a local HMI recreation so the interface remains fully offline. Replace `static/avans-wordmark.svg` with an approved official brand asset if the installation requires formal Avans brand compliance.
