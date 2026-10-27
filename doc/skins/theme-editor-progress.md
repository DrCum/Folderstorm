# Theme editor implementation

Base: PR #88, `85f620154fe0da09d2a64d7fcc0f9f352f358c49`.

The user authorized implementation through the approved staged plan, preserving
candidates awaiting human testing and continuing on new PRs. Publication and
merge still need authorization.

Gate A is ready for the authoring stage at the observed desktop scale. See
[the validation record](theme-editor-gate-a-validation.md) for exact builds,
evidence, human checks and unavailable coverage. The final feature acceptance
matrix remains open.

Native 7.2.5.82310 (`97c217e00f`) and editor 0.1.1 (`90f8a1882c`) remain retained
under `D:/Folderstorm-theme-builds/gate-a`. The latter includes the exporter alias
fix and corrected sample-button state proof; native source remains unchanged.
Neither candidate was installed or published. Shared performance source was
restored after the locked native builds; unrelated build archives remain intact.

The architectural slice includes strict shared contracts, six AnsaStorm scheme
catalogs, lossless projects/history, deterministic packages/import, immutable
library revisions, native resource/font overlays, isolated offline previews,
selection persistence and recovery. Focused checks pass: eight core groups,
40 shared manifest fixtures, 16 shared boundary probes, six recovery cases and
two-preview ownership. The user physically verified hover/held/release states
and the scoped checkbox/radio fix. The WPF Preview action also launched the
correct isolated native child.

Continue in this order:

1. Complete palette/image tools, recipes/slicing, metadata/history, reset/diff,
   project/library/import/export, comparisons and external-edit refresh.
2. Complete fonts/fallback/licenses, advanced widget/layout tools, host palette,
   Appearance launcher and explicit host preference migration.
3. Integrate self-contained editor and optional build component into NSIS and
   Velopack; complete documentation and full acceptance, including DPI.
4. Add and validate the Folderstorm/Firestorm base adapter.
5. If there is no further human steering, plan and begin customization independent
   of an installed base theme without weakening recovery or compatibility.

Managed authoring checkpoints now include pixel-art tools and shared color
pickers (0.5.1), recoverable external-editor PNG copies (0.6.0), and checked-state
family previews/transactions (0.7.0). The family checkpoint passes 32 core,
10 Windows image/codec and 10 non-visible WPF groups plus two C++ package
validations. Windowless layouts are reviewed; external-app desktop access and
full native/DPI acceptance remain open. Each candidate stays retained separately
while remaining authoring work continues. Native 82310 remains unchanged.

Computer control is off. Prefer user screenshots for small visual checks, use a
question prompt for input, and leave the user's personal viewer alone. Any future
computer-control batch must be announced, delayed at least five seconds, kept
short and explicitly disconnected when finished.
