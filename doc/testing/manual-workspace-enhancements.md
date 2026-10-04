# Workspace enhancement checkpoints

## Favorite workspace/layout strip

This checkpoint has XML/binding and diff checks only. No viewer build, native UI run or broad tests were performed.

When ready for the combined native pass:

1. Star a workspace and a layout in the existing switcher, then enable Show workspace favorites strip. The row appears separately below navigation/landmark favorites with distinct labels. Switch from its buttons and resize the viewer until entries overflow into More. Long names have full tooltips. More opens the existing switcher for saving/managing entries and can hide the row.
2. Repeat with navigation and landmark favorites individually hidden, UI visibility off/on, Mouselook, an alternate skin and a higher UI scale. The row should follow the configured navigation span and hide in Mouselook/hidden UI.
3. Open Preferences. Strip switching and the switcher's strip visibility control are disabled, with a tooltip explaining OK/Cancel. Toggle strip visibility from Workspaces preferences and Cancel: its original visibility returns. A saved workspace cannot change the strip visibility preference.
4. Unfavorite, rename and delete entries; stale references disappear from the row and overflow. Log out and switch accounts; labels and old menu actions must not carry over. Existing favorites remain stored per account. A failed/corrupt saved entry reports the failure.
