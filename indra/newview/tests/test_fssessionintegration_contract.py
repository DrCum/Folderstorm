"""Focused contracts for the host Accessibility/controller merge; no Win32 build."""
import pathlib
import re
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[3]


class SessionIntegrationTests(unittest.TestCase):
    def setUp(self):
        self.host = (ROOT / "tools/session-host/main.cpp").read_text(encoding="utf-8")
        self.preferences = self.host[self.host.index("enum PreferencePage"):self.host.index("void editHostSettings(")]

    def test_conflicting_pages_keep_distinct_indices_and_categories(self):
        enum = re.search(r"enum PreferencePage[^\{]*\{([^}]+)\}", self.preferences)[1]
        pages = [name.strip() for name in enum.split(",")]
        navigation = self.preferences[self.preferences.index("const std::vector<EditorPage> navigation{"):self.preferences.index("auto controllerDraft=")]
        rows = re.findall(r'\{(\d+),L"([^"]+)",L"([^"]+)"', navigation)
        self.assertEqual(len(pages), len(rows))
        self.assertEqual(len(pages), len(set(pages)))
        expected = {
            "PrefAccessibility": ("Accessibility", "Accessibility"),
            "PrefDiagnostics": ("Advanced", "Diagnostics"),
            "PrefRecovery": ("Advanced", "Files & recovery"),
            "PrefAssistant": ("Character settings", "Local Assistant"),
            "PrefController": ("MCP controller", "Host access"),
        }
        for page, labels in expected.items():
            self.assertEqual(rows[pages.index(page)][1:], labels)
        categories = {name: rows[pages.index(name)][0] for name in expected}
        self.assertNotEqual(categories["PrefController"], categories["PrefAccessibility"])
        self.assertNotEqual(categories["PrefController"], categories["PrefDiagnostics"])

    def test_controller_choices_still_apply_only_from_the_accepted_draft(self):
        validate = self.preferences.index("const auto validate=")
        apply = self.preferences.index("const auto apply=")
        self.assertNotIn("controllerApply(", self.preferences[:apply])
        self.assertIn("controllerDraft=controllerConfig", self.preferences[validate:apply])
        self.assertIn("controllerConfig.revision!=controllerCapturedRevision", self.preferences[validate:apply])
        self.assertIn("controllerApply(controllerDraft)", self.preferences[apply:])
        self.assertIn("validate,navigation,apply,&preferencesPage", self.preferences)
        for field in ("controllerEnableField", "controllerMetadataField", "controllerPermissionFields", "controllerProfileFields"):
            self.assertIn(field, self.preferences[:validate])
            self.assertIn(field, self.preferences[validate:apply])

    def test_accessibility_controls_keep_the_existing_shared_draft(self):
        self.assertIn("field.mirror=source;field.page=page", self.preferences)
        self.assertIn("mirror(source,PrefAccessibility)", self.preferences)
        for field in ("toolbarLabelsField", "labelsField", "densityField", "transitionField", "shortcutField"):
            self.assertRegex(self.preferences, r"for\(const auto source:\{[^}]*\b" + field + r"\b[^}]*\}\)mirror\(source,PrefAccessibility\)")
        self.assertIn("next.experience.hostContrast=n(contrastField)", self.preferences)
        self.assertIn("next.experience.audioStateText=n(stateTextField)!=0", self.preferences)

    def test_standalone_regressions_retain_both_suites_and_content_includes(self):
        cmake = (ROOT / "indra/newview/tests/folderstorm/CMakeLists.txt").read_text(encoding="utf-8")
        checks = re.search(r"foreach\(check IN ITEMS ([^)]+)\)", cmake)[1].split()
        self.assertTrue({"controls", "assistant_features", "assistant_async"}.issubset(checks))
        for target in ("session_controller_mcp", "session_controller_dispatch", "squirty_policy"):
            self.assertIn("folderstorm_check(" + target, cmake)
        content = re.search(r"foreach\(target IN ITEMS ([^)]+)\)(.*?)endforeach\(\)", cmake, re.S)
        self.assertEqual(set(content[1].split()), {"content_policy", "content_upload", "content_read_permissions", "content_notecard_v1"})
        self.assertIn("target_include_directories(${target} PRIVATE ../..)", content[2])
        windows = cmake[cmake.index("if(WIN32)"):]
        self.assertIn("add_executable(session_native_controls", windows)
        self.assertIn("accessibility.cpp", windows)
        self.assertIn("add_test(NAME session_native_controls", windows)


if __name__ == "__main__":
    unittest.main()
