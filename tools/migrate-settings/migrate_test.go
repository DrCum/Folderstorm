package main

import (
	"os"
	"path/filepath"
	"strings"
	"testing"
)

func runOpts(t *testing.T, home, platform string, opts Options, answers ...bool) Result {
	t.Helper()
	opts.Platform = platform
	if opts.Environ == nil {
		opts.Environ = map[string]string{}
	}
	opts.Environ["HOME"] = home
	opts.Environ["USERPROFILE"] = home
	remaining := append([]bool(nil), answers...)
	if len(answers) > 0 || opts.Interactive {
		opts.Ask = func(string) bool {
			if len(remaining) == 0 {
				t.Fatalf("unexpected prompt")
			}
			answer := remaining[0]
			remaining = remaining[1:]
			return answer
		}
	}
	result := Run(opts)
	if len(remaining) != 0 {
		t.Fatalf("unused answers: %d", len(remaining))
	}
	return result
}

func seedSettings(t *testing.T, home string) string {
	t.Helper()
	source := filepath.Join(home, ".firestorm_x64", "user_settings")
	mustMkdir(t, filepath.Join(source, "presets", "graphic"))
	mustWrite(t, filepath.Join(source, "settings.xml"), "prefs")
	mustWrite(t, filepath.Join(source, "key_bindings.xml"), "keys")
	mustWrite(t, filepath.Join(source, "password.dat"), "secret-password")
	mustWrite(t, filepath.Join(source, "bin_conf.dat"), "secret-store")
	mustWrite(t, filepath.Join(source, "fs-mcp-4242.json"), `{"token":"secret"}`)
	mustWrite(t, filepath.Join(source, "presets", "graphic", "photo.xml"), "preset")
	mustWrite(t, filepath.Join(source, "cache", "texture.dat"), "cached")
	mustWrite(t, filepath.Join(source, "browser_profile", "Cookies"), "cookie")
	mustWrite(t, filepath.Join(source, "logs", "viewer.log"), "log")
	if err := os.Symlink(filepath.Join(source, "password.dat"), filepath.Join(source, "linked-secret.xml")); err != nil {
		t.Fatal(err)
	}
	mustWrite(t, filepath.Join(home, ".firestorm_x64", "cache", "huge.bin"), "nope")
	return source
}

func seedAccount(t *testing.T, home, name string) string {
	t.Helper()
	dir := filepath.Join(home, ".firestorm_x64", name)
	mustWrite(t, filepath.Join(dir, "toolbars.xml"), "toolbars-"+name)
	mustWrite(t, filepath.Join(dir, "settings_per_account.xml"), "per-account")
	mustWrite(t, filepath.Join(dir, "notes", "todo.txt"), "note")
	mustWrite(t, filepath.Join(dir, "password.dat"), "account-secret")
	mustWrite(t, filepath.Join(dir, "plugin_cookies.xml"), "cookies")
	mustWrite(t, filepath.Join(dir, "fs-mcp-9.json"), `{"token":"account"}`)
	mustWrite(t, filepath.Join(dir, "logs", "chat.log"), "chat")
	mustWrite(t, filepath.Join(dir, "cache", "blob"), "cached")
	mustWrite(t, filepath.Join(dir, "browser_profile", "Cookies"), "cookie")
	return dir
}

func mustMkdir(t *testing.T, path string) {
	t.Helper()
	if err := os.MkdirAll(path, 0o755); err != nil {
		t.Fatal(err)
	}
}

func mustWrite(t *testing.T, path, body string) {
	t.Helper()
	mustMkdir(t, filepath.Dir(path))
	if err := os.WriteFile(path, []byte(body), 0o644); err != nil {
		t.Fatal(err)
	}
}

func TestLinuxNamesAndEnvOverride(t *testing.T) {
	home := t.TempDir()
	custom := filepath.Join(t.TempDir(), "custom-fs")
	mustWrite(t, filepath.Join(custom, "user_settings", "settings.xml"), "custom")
	mustWrite(t, filepath.Join(home, ".firestorm_x64", "user_settings", "settings.xml"), "default")
	destRoot := filepath.Join(t.TempDir(), "custom-folder")
	opts := Options{Yes: true, Environ: map[string]string{
		"HOME":                     home,
		"FIRESTORM_X64_USER_DIR":   custom,
		"FOLDERSTORM_X64_USER_DIR": destRoot,
	}}
	opts.Platform = "linux"
	result := Run(opts)
	if result.Code != exitOK {
		t.Fatal(result.Stderr)
	}
	got, err := os.ReadFile(filepath.Join(destRoot, "user_settings", "settings.xml"))
	if err != nil || string(got) != "custom" {
		t.Fatalf("dest settings = %q err %v", got, err)
	}
}

func TestLinuxPrefersX64(t *testing.T) {
	home := t.TempDir()
	mustWrite(t, filepath.Join(home, ".firestorm_x64", "user_settings", "settings.xml"), "new")
	mustWrite(t, filepath.Join(home, ".firestorm", "user_settings", "settings.xml"), "old")
	result := runOpts(t, home, "linux", Options{Yes: true})
	if result.Code != exitOK {
		t.Fatal(result.Stderr)
	}
	got, err := os.ReadFile(filepath.Join(home, ".folderstorm_x64", "user_settings", "settings.xml"))
	if err != nil || string(got) != "new" {
		t.Fatalf("got %q err %v", got, err)
	}
}

func TestWindowsUsesAppDataAndIgnoresLinuxEnv(t *testing.T) {
	root := t.TempDir()
	appdata := filepath.Join(root, "Roaming")
	mustWrite(t, filepath.Join(appdata, "Firestorm_x64", "user_settings", "settings.xml"), "win")
	opts := Options{
		Yes:      true,
		Platform: "windows",
		Environ: map[string]string{
			"HOME":                     filepath.Join(root, "home"),
			"APPDATA":                  appdata,
			"FIRESTORM_X64_USER_DIR":   filepath.Join(root, "ignored"),
			"FOLDERSTORM_X64_USER_DIR": filepath.Join(root, "ignored-dest"),
		},
	}
	result := Run(opts)
	if result.Code != exitOK {
		t.Fatal(result.Stderr)
	}
	got, err := os.ReadFile(filepath.Join(appdata, "Folderstorm_x64", "user_settings", "settings.xml"))
	if err != nil || string(got) != "win" {
		t.Fatalf("got %q err %v", got, err)
	}
}

func TestMacOSSettingsHaveNoX64Suffix(t *testing.T) {
	home := t.TempDir()
	support := filepath.Join(home, "Library", "Application Support")
	mustWrite(t, filepath.Join(support, "Firestorm", "user_settings", "settings.xml"), "mac")
	mustWrite(t, filepath.Join(support, "Firestorm_x64", "user_settings", "settings.xml"), "other")
	result := runOpts(t, home, "darwin", Options{Yes: true})
	if result.Code != exitOK {
		t.Fatal(result.Stderr)
	}
	got, err := os.ReadFile(filepath.Join(support, "Folderstorm", "user_settings", "settings.xml"))
	if err != nil || string(got) != "mac" {
		t.Fatalf("got %q err %v", got, err)
	}
}

func TestSkipsEmptyCandidate(t *testing.T) {
	home := t.TempDir()
	mustMkdir(t, filepath.Join(home, ".firestorm_x64", "user_settings"))
	mustWrite(t, filepath.Join(home, ".firestorm", "user_settings", "settings.xml"), "old")
	result := runOpts(t, home, "linux", Options{Yes: true})
	if result.Code != exitOK {
		t.Fatal(result.Stderr)
	}
	got, err := os.ReadFile(filepath.Join(home, ".folderstorm_x64", "user_settings", "settings.xml"))
	if err != nil || string(got) != "old" {
		t.Fatalf("got %q err %v", got, err)
	}
}

func TestYesCopiesSettingsAndSkipsSecrets(t *testing.T) {
	home := t.TempDir()
	seedSettings(t, home)
	result := runOpts(t, home, "linux", Options{Yes: true})
	if result.Code != exitOK {
		t.Fatal(result.Stderr)
	}
	dest := filepath.Join(home, ".folderstorm_x64", "user_settings")
	if body := mustRead(t, filepath.Join(dest, "settings.xml")); body != "prefs" {
		t.Fatalf("settings %q", body)
	}
	if body := mustRead(t, filepath.Join(dest, "presets", "graphic", "photo.xml")); body != "preset" {
		t.Fatalf("preset %q", body)
	}
	for _, name := range []string{"password.dat", "bin_conf.dat", "fs-mcp-4242.json", "linked-secret.xml"} {
		if _, err := os.Lstat(filepath.Join(dest, name)); !os.IsNotExist(err) {
			t.Fatalf("%s was copied", name)
		}
	}
	if _, err := os.Lstat(filepath.Join(dest, "cache")); !os.IsNotExist(err) {
		t.Fatal("cache was copied")
	}
	if _, err := os.Lstat(filepath.Join(dest, "browser_profile")); !os.IsNotExist(err) {
		t.Fatal("browser profile was copied")
	}
	if _, err := os.Lstat(filepath.Join(dest, "logs")); !os.IsNotExist(err) {
		t.Fatal("logs were copied")
	}
	if _, err := os.Lstat(filepath.Join(home, ".folderstorm_x64", "cache")); !os.IsNotExist(err) {
		t.Fatal("sibling cache was copied")
	}
	if !strings.Contains(result.Stdout, "credential") {
		t.Fatalf("stdout %q", result.Stdout)
	}
}

func TestYesDoesNotCopyAccountFolders(t *testing.T) {
	home := t.TempDir()
	seedSettings(t, home)
	seedAccount(t, home, "first_last")
	result := runOpts(t, home, "linux", Options{Yes: true})
	if result.Code != exitOK {
		t.Fatal(result.Stderr)
	}
	if _, err := os.Lstat(filepath.Join(home, ".folderstorm_x64", "first_last", "toolbars.xml")); !os.IsNotExist(err) {
		t.Fatal("toolbars.xml was copied with settings")
	}
}

func TestAccountsCopyWholeFolderExceptSecrets(t *testing.T) {
	home := t.TempDir()
	seedSettings(t, home)
	seedAccount(t, home, "first_last")
	seedAccount(t, home, "first_last.osgrid")
	result := runOpts(t, home, "linux", Options{Accounts: true})
	if result.Code != exitOK {
		t.Fatal(result.Stderr)
	}
	if _, err := os.Lstat(filepath.Join(home, ".folderstorm_x64", "user_settings", "settings.xml")); !os.IsNotExist(err) {
		t.Fatal("settings were copied with accounts")
	}
	for _, name := range []string{"first_last", "first_last.osgrid"} {
		dest := filepath.Join(home, ".folderstorm_x64", name)
		if body := mustRead(t, filepath.Join(dest, "toolbars.xml")); body != "toolbars-"+name {
			t.Fatalf("toolbars %s %q", name, body)
		}
		if body := mustRead(t, filepath.Join(dest, "notes", "todo.txt")); body != "note" {
			t.Fatalf("note %q", body)
		}
		for _, skipped := range []string{"password.dat", "plugin_cookies.xml", "fs-mcp-9.json"} {
			if _, err := os.Lstat(filepath.Join(dest, skipped)); !os.IsNotExist(err) {
				t.Fatalf("%s copied into %s", skipped, name)
			}
		}
		if _, err := os.Lstat(filepath.Join(dest, "logs")); !os.IsNotExist(err) {
			t.Fatal("account logs copied")
		}
		if _, err := os.Lstat(filepath.Join(dest, "cache")); !os.IsNotExist(err) {
			t.Fatal("account cache copied")
		}
		if _, err := os.Lstat(filepath.Join(dest, "browser_profile")); !os.IsNotExist(err) {
			t.Fatal("account browser profile copied")
		}
		from := filepath.Join(home, ".firestorm_x64", name, "toolbars.xml")
		to := filepath.Join(dest, "toolbars.xml")
		if !strings.Contains(result.Stdout, from) || !strings.Contains(result.Stdout, to) {
			t.Fatalf("stdout missing toolbar paths:\n%s", result.Stdout)
		}
	}
}

func TestExistingSettingsFolderStillOfferedAndDoesNotClobber(t *testing.T) {
	home := t.TempDir()
	seedSettings(t, home)
	dest := filepath.Join(home, ".folderstorm_x64", "user_settings")
	mustMkdir(t, dest)
	check := runOpts(t, home, "linux", Options{Check: true})
	if check.Code != exitOK {
		t.Fatalf("empty dest folder check = %d", check.Code)
	}
	mustWrite(t, filepath.Join(dest, "settings.xml"), "already")
	mustWrite(t, filepath.Join(dest, "folderstorm-only.xml"), "keep")
	mustWrite(t, filepath.Join(dest, "password.dat"), "dest-secret")
	check = runOpts(t, home, "linux", Options{Check: true})
	if check.Code != exitDestExists {
		t.Fatalf("conflict check = %d", check.Code)
	}
	result := runOpts(t, home, "linux", Options{Yes: true})
	if result.Code != exitOK {
		t.Fatal(result.Stderr)
	}
	if body := mustRead(t, filepath.Join(dest, "settings.xml")); body != "already" {
		t.Fatalf("settings overwritten: %q", body)
	}
	if body := mustRead(t, filepath.Join(dest, "key_bindings.xml")); body != "keys" {
		t.Fatalf("missing file not copied: %q", body)
	}
	if body := mustRead(t, filepath.Join(dest, "folderstorm-only.xml")); body != "keep" {
		t.Fatalf("extra file lost: %q", body)
	}
	if body := mustRead(t, filepath.Join(dest, "password.dat")); body != "dest-secret" {
		t.Fatalf("dest password changed: %q", body)
	}
}

func TestOverwriteReplacesSettingsFiles(t *testing.T) {
	home := t.TempDir()
	seedSettings(t, home)
	dest := filepath.Join(home, ".folderstorm_x64", "user_settings")
	mustWrite(t, filepath.Join(dest, "settings.xml"), "already")
	mustWrite(t, filepath.Join(dest, "folderstorm-only.xml"), "keep")
	mustWrite(t, filepath.Join(dest, "password.dat"), "dest-secret")
	result := runOpts(t, home, "linux", Options{Yes: true, Overwrite: true})
	if result.Code != exitOK {
		t.Fatal(result.Stderr)
	}
	if body := mustRead(t, filepath.Join(dest, "settings.xml")); body != "prefs" {
		t.Fatalf("settings %q", body)
	}
	if body := mustRead(t, filepath.Join(dest, "folderstorm-only.xml")); body != "keep" {
		t.Fatalf("extra %q", body)
	}
	if body := mustRead(t, filepath.Join(dest, "password.dat")); body != "dest-secret" {
		t.Fatalf("password %q", body)
	}
}

func TestExistingAccountFolderIsNotReplacedWithoutConfirm(t *testing.T) {
	home := t.TempDir()
	seedAccount(t, home, "first_last")
	seedAccount(t, home, "second_resident")
	dest := filepath.Join(home, ".folderstorm_x64", "first_last")
	mustWrite(t, filepath.Join(dest, "toolbars.xml"), "keep-toolbars")
	mustWrite(t, filepath.Join(dest, "folderstorm-note.txt"), "keep")
	check := runOpts(t, home, "linux", Options{CheckAccounts: true})
	if check.Code != exitDestExists {
		t.Fatalf("check-accounts = %d", check.Code)
	}
	result := runOpts(t, home, "linux", Options{Accounts: true})
	if result.Code != exitOK {
		t.Fatal(result.Stderr)
	}
	if body := mustRead(t, filepath.Join(dest, "toolbars.xml")); body != "keep-toolbars" {
		t.Fatalf("existing account changed: %q", body)
	}
	if _, err := os.Lstat(filepath.Join(dest, "notes", "todo.txt")); !os.IsNotExist(err) {
		t.Fatal("merged into an existing account folder")
	}
	if body := mustRead(t, filepath.Join(home, ".folderstorm_x64", "second_resident", "toolbars.xml")); body != "toolbars-second_resident" {
		t.Fatalf("new account not copied: %q", body)
	}
}

func TestOverwriteAccountsReplacesFolderFiles(t *testing.T) {
	home := t.TempDir()
	seedAccount(t, home, "first_last")
	dest := filepath.Join(home, ".folderstorm_x64", "first_last")
	mustWrite(t, filepath.Join(dest, "toolbars.xml"), "old")
	mustWrite(t, filepath.Join(dest, "folderstorm-note.txt"), "keep")
	mustWrite(t, filepath.Join(dest, "password.dat"), "dest-secret")
	result := runOpts(t, home, "linux", Options{Accounts: true, OverwriteAccounts: true})
	if result.Code != exitOK {
		t.Fatal(result.Stderr)
	}
	if body := mustRead(t, filepath.Join(dest, "toolbars.xml")); body != "toolbars-first_last" {
		t.Fatalf("toolbars %q", body)
	}
	if body := mustRead(t, filepath.Join(dest, "folderstorm-note.txt")); body != "keep" {
		t.Fatalf("extra %q", body)
	}
	if body := mustRead(t, filepath.Join(dest, "password.dat")); body != "dest-secret" {
		t.Fatalf("password %q", body)
	}
	if body := mustRead(t, filepath.Join(dest, "notes", "todo.txt")); body != "note" {
		t.Fatalf("note %q", body)
	}
}

func TestInteractiveAsksSeparately(t *testing.T) {
	home := t.TempDir()
	seedSettings(t, home)
	seedAccount(t, home, "first_last")
	declined := runOpts(t, home, "linux", Options{Interactive: true}, false, false)
	if declined.Code != exitDeclined {
		t.Fatalf("decline = %d %s", declined.Code, declined.Stderr)
	}
	if _, err := os.Lstat(filepath.Join(home, ".folderstorm_x64")); !os.IsNotExist(err) {
		t.Fatal("declined copy wrote a folder")
	}

	home = t.TempDir()
	seedSettings(t, home)
	seedAccount(t, home, "first_last")
	settingsOnly := runOpts(t, home, "linux", Options{Interactive: true}, true, false)
	if settingsOnly.Code != exitOK {
		t.Fatal(settingsOnly.Stderr)
	}
	if _, err := os.Lstat(filepath.Join(home, ".folderstorm_x64", "first_last")); !os.IsNotExist(err) {
		t.Fatal("account copied after a no")
	}

	home = t.TempDir()
	seedSettings(t, home)
	seedAccount(t, home, "first_last")
	accountsOnly := runOpts(t, home, "linux", Options{Interactive: true}, false, true)
	if accountsOnly.Code != exitOK {
		t.Fatal(accountsOnly.Stderr)
	}
	if _, err := os.Lstat(filepath.Join(home, ".folderstorm_x64", "user_settings", "settings.xml")); !os.IsNotExist(err) {
		t.Fatal("settings copied after a no")
	}
	if body := mustRead(t, filepath.Join(home, ".folderstorm_x64", "first_last", "toolbars.xml")); body != "toolbars-first_last" {
		t.Fatalf("toolbars %q", body)
	}
}

func TestInteractiveOverwritePrompts(t *testing.T) {
	home := t.TempDir()
	seedSettings(t, home)
	dest := filepath.Join(home, ".folderstorm_x64", "user_settings")
	mustWrite(t, filepath.Join(dest, "settings.xml"), "already")
	merged := runOpts(t, home, "linux", Options{Interactive: true}, true, false)
	if merged.Code != exitOK {
		t.Fatal(merged.Stderr)
	}
	if body := mustRead(t, filepath.Join(dest, "settings.xml")); body != "already" {
		t.Fatalf("replaced without confirm: %q", body)
	}
	if body := mustRead(t, filepath.Join(dest, "key_bindings.xml")); body != "keys" {
		t.Fatalf("new file missing: %q", body)
	}

	home = t.TempDir()
	seedAccount(t, home, "first_last")
	account := filepath.Join(home, ".folderstorm_x64", "first_last")
	mustWrite(t, filepath.Join(account, "toolbars.xml"), "old")
	kept := runOpts(t, home, "linux", Options{Interactive: true}, true, false)
	if kept.Code != exitOK {
		t.Fatal(kept.Stderr)
	}
	if body := mustRead(t, filepath.Join(account, "toolbars.xml")); body != "old" {
		t.Fatalf("account replaced without confirm: %q", body)
	}
	replaced := runOpts(t, home, "linux", Options{Interactive: true}, true, true)
	if replaced.Code != exitOK {
		t.Fatal(replaced.Stderr)
	}
	if body := mustRead(t, filepath.Join(account, "toolbars.xml")); body != "toolbars-first_last" {
		t.Fatalf("account not replaced: %q", body)
	}
}

func TestRefusesWithoutYesWhenNotInteractive(t *testing.T) {
	home := t.TempDir()
	seedSettings(t, home)
	result := runOpts(t, home, "linux", Options{})
	if result.Code != exitDeclined {
		t.Fatalf("code %d", result.Code)
	}
	if !strings.Contains(result.Stderr, "--yes") || !strings.Contains(result.Stderr, "--accounts") {
		t.Fatal(result.Stderr)
	}
	if _, err := os.Lstat(filepath.Join(home, ".folderstorm_x64")); !os.IsNotExist(err) {
		t.Fatal("wrote without --yes")
	}
}

func TestCredentialOnlySourceIsNotSettings(t *testing.T) {
	home := t.TempDir()
	source := filepath.Join(home, ".firestorm_x64", "user_settings")
	mustWrite(t, filepath.Join(source, "password.dat"), "secret")
	mustWrite(t, filepath.Join(source, "cache", "blob"), "cache")
	result := runOpts(t, home, "linux", Options{Yes: true})
	if result.Code != exitNoSource {
		t.Fatalf("code %d %s", result.Code, result.Stderr)
	}
}

func TestAccountOnlySource(t *testing.T) {
	home := t.TempDir()
	seedAccount(t, home, "first_last")
	check := runOpts(t, home, "linux", Options{Check: true})
	if check.Code != exitNoSource {
		t.Fatalf("settings check %d", check.Code)
	}
	checkAccounts := runOpts(t, home, "linux", Options{CheckAccounts: true})
	if checkAccounts.Code != exitOK {
		t.Fatalf("accounts check %d", checkAccounts.Code)
	}
	result := runOpts(t, home, "linux", Options{Accounts: true})
	if result.Code != exitOK {
		t.Fatal(result.Stderr)
	}
	if body := mustRead(t, filepath.Join(home, ".folderstorm_x64", "first_last", "toolbars.xml")); body != "toolbars-first_last" {
		t.Fatalf("toolbars %q", body)
	}
}

func TestSystemDirsAreNotAccounts(t *testing.T) {
	home := t.TempDir()
	mustWrite(t, filepath.Join(home, ".firestorm_x64", "user_settings", "settings.xml"), "prefs")
	mustWrite(t, filepath.Join(home, ".firestorm_x64", "logs", "viewer.log"), "log")
	mustWrite(t, filepath.Join(home, ".firestorm_x64", "cache", "blob"), "cache")
	mustWrite(t, filepath.Join(home, ".firestorm_x64", "browser_profile", "Cookies"), "cookie")
	check := runOpts(t, home, "linux", Options{CheckAccounts: true})
	if check.Code != exitNoSource {
		t.Fatalf("system dirs looked like accounts: %d", check.Code)
	}
}

func TestDryRunWritesNothing(t *testing.T) {
	home := t.TempDir()
	seedSettings(t, home)
	seedAccount(t, home, "first_last")
	result := runOpts(t, home, "linux", Options{DryRun: true})
	if result.Code != exitOK {
		t.Fatal(result.Stderr)
	}
	if !strings.Contains(result.Stdout, "Dry run") || !strings.Contains(result.Stdout, "toolbars.xml") {
		t.Fatal(result.Stdout)
	}
	if _, err := os.Lstat(filepath.Join(home, ".folderstorm_x64")); !os.IsNotExist(err) {
		t.Fatal("dry run wrote")
	}
}

func TestRefusesNestedDest(t *testing.T) {
	home := t.TempDir()
	source := seedSettings(t, home)
	result := runOpts(t, home, "linux", Options{Yes: true, Source: source, Dest: filepath.Join(source, "nested")})
	if result.Code != exitError {
		t.Fatalf("code %d", result.Code)
	}
	if !strings.Contains(result.Stderr, "inside") {
		t.Fatal(result.Stderr)
	}
}

func TestExplicitSourceAndDest(t *testing.T) {
	root := t.TempDir()
	mustWrite(t, filepath.Join(root, "from", "user_settings", "settings.xml"), "moved")
	mustWrite(t, filepath.Join(root, "from", "first_last", "toolbars.xml"), "bars")
	dest := filepath.Join(root, "to", "user_settings")
	result := runOpts(t, filepath.Join(root, "home"), "linux", Options{
		Yes:      true,
		Accounts: true,
		Source:   filepath.Join(root, "from"),
		Dest:     dest,
	})
	if result.Code != exitOK {
		t.Fatal(result.Stderr)
	}
	if body := mustRead(t, filepath.Join(dest, "settings.xml")); body != "moved" {
		t.Fatalf("settings %q", body)
	}
	if body := mustRead(t, filepath.Join(root, "to", "first_last", "toolbars.xml")); body != "bars" {
		t.Fatalf("toolbars %q", body)
	}
}

func TestUnsupportedPlatform(t *testing.T) {
	result := Run(Options{Platform: "", Check: true, Environ: map[string]string{"HOME": t.TempDir()}})
	if result.Code != exitError || !strings.Contains(result.Stderr, "Linux, Windows, and macOS") {
		t.Fatalf("%d %s", result.Code, result.Stderr)
	}
}

func TestCheckNoSourceIsQuiet(t *testing.T) {
	result := runOpts(t, t.TempDir(), "linux", Options{Check: true})
	if result.Code != exitNoSource || result.Stdout != "" || result.Stderr != "" {
		t.Fatalf("%d %q %q", result.Code, result.Stdout, result.Stderr)
	}
}

func mustRead(t *testing.T, path string) string {
	t.Helper()
	body, err := os.ReadFile(path)
	if err != nil {
		t.Fatal(err)
	}
	return string(body)
}
