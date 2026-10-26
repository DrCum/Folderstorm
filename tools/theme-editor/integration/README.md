# Native theme integration checks

These opt-in .NET 10 drivers exercise a matching Windows viewer. The ordinary
portable C++ checks do not depend on them. Build each project with `dotnet build
-c Release`, then run the DLL with the arguments below. Redirect stdout/stderr
to a log because the viewer inherits the preview driver's output.

Use a retained runnable package, not an installed viewer with unrelated changes.
The drivers never log in or install. They publish validated themes to the real
per-user revision library, redirect each viewer's roaming/cache profile, and
strip inherited managed-worker targeting variables. Keep the GUI visible for
Computer Use or human visual checks. Ready status alone is not visual acceptance.

`prepare-proof-packages.py --source <checkout> --base <retained-Run> --dotnet
<dotnet.exe> --output <new-directory>` creates seven project/package fixtures.
Use a fresh output directory to preserve prior evidence. The button proof binds
its normal, hover and pressed images explicitly; texture names alone are not
widget state bindings. Floater tint includes native image-overlay attributes.
The explicit images are scoped to the sample's named button. Adding them to the
global button defaults also changes checkbox/radio inner buttons and can draw
an oversized hover rectangle over their labels.

The offline sample provides **Pin pressed state** and **Disable sample button**
controls. Pinning uses the real native button's pressed-image draw branch, so
its image can be inspected after releasing the mouse. Status also records actual
sample mouse-down, mouse-up and commit counts separately from this pinned state.
Use those distinctions when describing input and screenshot evidence.

| Driver | Arguments | Check |
| --- | --- | --- |
| Preview | `viewer.exe base-folder package-or-dash receipt.json stop-file` | Product preview lifecycle; `-` selects the built-in baseline. Holds up to 15 minutes or until the stop file appears. |
| Ownership | `viewer.exe noop.fstheme combined.fstheme receipt.json` | Two independent children; refreshing/closing the primary preserves its sibling and its files. |
| Recovery | `viewer.exe noop.fstheme new-evidence-directory observed-app-folder` | Six normal-startup activation, rollback, reset and fallback cases. The app folder must be observed from that viewer, for example `Folderstorm_x64`. |

Recovery records the actual default skin/font values and verifies they remain
unchanged, together with a personal color sentinel. It retains logs and before/
after files. A normal viewer can display a quit confirmation; the driver leaves
that dialog untouched and, after eight seconds, terminates only its own child
after checking PID/start time. This is not a claim of graceful normal shutdown.
The ordinary product preview has its own graceful offline Close path.

Recovery deliberately creates fresh invalid library revisions for changed-base
and corrupt-payload cases. Their exact IDs/paths are recorded in `fixture-*.json`;
after all owned readers close, the driver archives those fixture directories in
its evidence folder. Other revisions are preserved.

Use the Computer Use skill for GUI input/captures. Windows.Graphics.Capture can
omit unchanged OpenGL content even while it remains visible on screen. Preserve
that limitation in evidence; do not infer a native renderer failure from an
incomplete capture. Record executable/source/revision, window size and DPI/scale
coverage for each accepted image. Never substitute WPF rendering for native
font, slicing, image-state or tint acceptance.

`Pixels` is a windowless Windows/WPF codec probe, independent of GUI automation:

```text
dotnet run --project integration/Pixels -c Release -- <matching-viewer-or-repository-root>
```

It compiles the production ImageCodec source and checks exact RGBA/alpha/hidden-RGB
PNG round trips, a frozen display bitmap, and inherited button pixel fidelity.
It also checks TGA origin/depth/palette/RLE/malformed fixtures and decodes every
nonempty bundled TGA, writing source and decoded-pixel hashes to a fresh evidence
folder. The empty placeholder is rejected explicitly. The separate development
cross-check uses Pillow; the shipped editor uses only the managed decoder.
It never opens a window and does not establish UI or native rendering acceptance.
