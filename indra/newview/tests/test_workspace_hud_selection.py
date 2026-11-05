"""Round-trip workspace HUD rows through the production adapters and list selectors."""
import os
from pathlib import Path
import shlex
import subprocess
import tempfile
import unittest

from test_fsgestureboardcustomization import LLSD, UUID
from test_fsgestureboardsounds import VIEWER, function


class WorkspaceHUDSelection(unittest.TestCase):
    def test_refresh_preserves_exact_selected_items_and_saved_points(self):
        ui = (VIEWER / "fsworkspacecontextui.cpp").read_text()
        widget = (VIEWER.parent / "llui/llscrolllistctrl.cpp").read_text()
        selectors = function(widget, "bool LLScrollListCtrl::setSelectedByValue(")
        selectors += function(widget, "S32 LLScrollListCtrl::selectMultiple(")
        source = selectors + "\nnamespace FSWorkspaceContextUI {\n"
        for signature in ("LLView* panel(", "LLSD hudValue(", "void setOptions(",
                          "FSWorkspaceContext::Options options("):
            source += function(ui, signature)
        source += "}\n"
        with tempfile.TemporaryDirectory(prefix="workspace-hud-selection-") as temp:
            directory = Path(temp)
            (directory / "lluuid.h").write_text(UUID)
            (directory / "llsd.h").write_text(LLSD)
            (directory / "workspace_hud_adapters.inc").write_text(source)
            path = VIEWER / "tests/workspace_hud_selection_fixture.cpp"
            exe = directory / "test"
            subprocess.run(shlex.split(os.environ.get("CXX", "c++")) + [
                "-std=c++17", "-Wall", "-Wextra", "-Werror", "-I" + str(directory),
                "-I" + str(VIEWER), str(path), "-o", str(exe)], check=True)
            subprocess.run([str(exe)], check=True)


if __name__ == "__main__":
    unittest.main()
