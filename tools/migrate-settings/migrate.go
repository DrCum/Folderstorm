// Command migrate-settings copies Firestorm settings into Folderstorm.
//
// Folderstorm does not read the Firestorm settings folder. This program
// copies that folder only when asked, then asks separately before copying
// per-account folders (toolbar layout and the rest of each account).
// Passwords, cookies, assistant tokens, caches, and logs are left behind.
// Existing Folderstorm files are not replaced unless that replacement was
// confirmed. An existing Folderstorm account folder is left unchanged unless
// that folder was confirmed.
package main

import (
	"fmt"
	"io"
	"os"
	"path/filepath"
	"sort"
	"strings"
)

const (
	exitOK         = 0
	exitDeclined   = 1
	exitNoSource   = 2
	exitDestExists = 3
	exitError      = 4
)

const linuxDestEnv = "FOLDERSTORM_X64_USER_DIR"

var linuxSourceEnvs = []string{
	"FIRESTORM_X64_USER_DIR",
	"FIRESTORM_USER_DIR",
	"FIRESTORMOS_X64_USER_DIR",
	"FIRESTORMOS_USER_DIR",
}

var sourceDirNames = map[string][]string{
	"linux": {".firestorm_x64", ".firestorm", ".firestormos_x64", ".firestormos"},
	"windows": {
		"Firestorm_x64",
		"Firestorm",
		"FirestormOS_x64",
		"FirestormOS",
	},
	// macOS settings use the product name. _x64 is the cache directory.
	"darwin": {"Firestorm", "FirestormOS", "Firestorm_x64", "FirestormOS_x64"},
}

var destDirName = map[string]string{
	"linux":   ".folderstorm_x64",
	"windows": "Folderstorm_x64",
	"darwin":  "Folderstorm",
}

// Siblings of user_settings that are not per-account folders.
var notAccountDirs = map[string]struct{}{
	"user_settings":   {},
	"logs":            {},
	"log":             {},
	"chatlogs":        {},
	"cache":           {},
	"texturecache":    {},
	"objectcache":     {},
	"soundcache":      {},
	"cef_cache":       {},
	"browser_profile": {},
	"data":            {},
	"skins":           {},
	"fonts":           {},
	"temp":            {},
	"tmp":             {},
}

// Options is one invocation of the migrator.
type Options struct {
	Yes               bool
	Overwrite         bool
	Accounts          bool
	OverwriteAccounts bool
	DryRun            bool
	Check             bool
	CheckAccounts     bool
	Source            string
	Dest              string
	Platform          string
	Environ           map[string]string
	Interactive       bool
	Ask               func(message string) bool
}

// Result is what main prints and which status it returns.
type Result struct {
	Code   int
	Stdout string
	Stderr string
}

type treeItem struct {
	kind string // "dir" or "file"
	rel  string
	skip string // empty means the file is copied
}

func detectPlatform(goos string) string {
	switch goos {
	case "linux":
		return "linux"
	case "darwin":
		return "darwin"
	case "windows":
		return "windows"
	default:
		return ""
	}
}

func asUserSettings(path string) string {
	if strings.EqualFold(filepath.Base(path), "user_settings") {
		return path
	}
	return filepath.Join(path, "user_settings")
}

func settingsRoot(platform, home string, env map[string]string) (string, error) {
	switch platform {
	case "windows":
		if appdata := strings.TrimSpace(env["APPDATA"]); appdata != "" {
			return appdata, nil
		}
		return filepath.Join(home, "AppData", "Roaming"), nil
	case "darwin":
		return filepath.Join(home, "Library", "Application Support"), nil
	case "linux":
		return home, nil
	default:
		return "", fmt.Errorf("%s", unsupportedPlatform)
	}
}

const unsupportedPlatform = "This operating system is not one Folderstorm packages for. " +
	"The viewer settings layout is implemented for Linux, Windows, and macOS."

func homeDir(env map[string]string) string {
	if home := strings.TrimSpace(env["HOME"]); home != "" {
		return home
	}
	if home := strings.TrimSpace(env["USERPROFILE"]); home != "" {
		return home
	}
	home, err := os.UserHomeDir()
	if err != nil {
		return ""
	}
	return home
}

func sourceCandidates(platform, home string, env map[string]string) ([]string, error) {
	var candidates []string
	if platform == "linux" {
		for _, key := range linuxSourceEnvs {
			value := strings.TrimSpace(env[key])
			if value != "" {
				candidates = append(candidates, asUserSettings(value))
			}
		}
	}
	root, err := settingsRoot(platform, home, env)
	if err != nil {
		return nil, err
	}
	for _, name := range sourceDirNames[platform] {
		candidates = append(candidates, filepath.Join(root, name, "user_settings"))
	}
	return candidates, nil
}

func destinationDir(platform, home string, env map[string]string, destArg string) (string, error) {
	if destArg != "" {
		return asUserSettings(expandUser(destArg, home)), nil
	}
	if platform == "linux" {
		if value := strings.TrimSpace(env[linuxDestEnv]); value != "" {
			return asUserSettings(value), nil
		}
	}
	root, err := settingsRoot(platform, home, env)
	if err != nil {
		return "", err
	}
	return filepath.Join(root, destDirName[platform], "user_settings"), nil
}

func expandUser(path, home string) string {
	if path == "~" {
		return home
	}
	if strings.HasPrefix(path, "~"+string(os.PathSeparator)) {
		return filepath.Join(home, path[2:])
	}
	return path
}

func dirSkipReason(name string) string {
	folded := strings.ToLower(name)
	if folded == "browser_profile" {
		return "credentials"
	}
	switch folded {
	case "cache", "texturecache", "objectcache", "soundcache", "cef_cache":
		return "cache"
	case "logs", "log", "chatlogs":
		return "logs"
	}
	if strings.HasSuffix(folded, ".old_texturecache") {
		return "cache"
	}
	return ""
}

func fileSkipReason(name string) string {
	folded := strings.ToLower(name)
	switch folded {
	case "password.dat", "bin_conf.dat", "plugin_cookies.txt", "plugin_cookies.xml":
		return "credentials"
	}
	if strings.HasPrefix(folded, "fs-mcp-") && strings.HasSuffix(folded, ".json") {
		return "credentials"
	}
	if strings.HasSuffix(folded, ".log") {
		return "logs"
	}
	return ""
}

func walkTree(root string) ([]treeItem, error) {
	info, err := os.Lstat(root)
	if err != nil {
		if os.IsNotExist(err) {
			return nil, nil
		}
		return nil, err
	}
	if !info.IsDir() || info.Mode()&os.ModeSymlink != 0 {
		return nil, nil
	}
	var items []treeItem
	err = filepath.WalkDir(root, func(path string, d os.DirEntry, walkErr error) error {
		if walkErr != nil {
			return walkErr
		}
		if path == root {
			return nil
		}
		rel, err := filepath.Rel(root, path)
		if err != nil {
			return err
		}
		if d.Type()&os.ModeSymlink != 0 {
			kind := "file"
			if d.IsDir() {
				kind = "dir"
			}
			items = append(items, treeItem{kind: kind, rel: rel, skip: "link"})
			if d.IsDir() {
				return filepath.SkipDir
			}
			return nil
		}
		if d.IsDir() {
			if reason := dirSkipReason(d.Name()); reason != "" {
				items = append(items, treeItem{kind: "dir", rel: rel, skip: reason})
				return filepath.SkipDir
			}
			items = append(items, treeItem{kind: "dir", rel: rel})
			return nil
		}
		if !d.Type().IsRegular() {
			items = append(items, treeItem{kind: "file", rel: rel, skip: "other"})
			return nil
		}
		items = append(items, treeItem{kind: "file", rel: rel, skip: fileSkipReason(d.Name())})
		return nil
	})
	return items, err
}

func copyableFiles(items []treeItem) []string {
	var files []string
	for _, item := range items {
		if item.kind == "file" && item.skip == "" {
			files = append(files, item.rel)
		}
	}
	return files
}

func hasCopyableFiles(root string) bool {
	items, err := walkTree(root)
	if err != nil || len(items) == 0 {
		return false
	}
	return len(copyableFiles(items)) > 0
}

func isAccountDirName(name string) bool {
	folded := strings.ToLower(name)
	if _, skip := notAccountDirs[folded]; skip {
		return false
	}
	if strings.HasSuffix(folded, ".old_texturecache") {
		return false
	}
	return true
}

func accountDirs(app string) []string {
	entries, err := os.ReadDir(app)
	if err != nil {
		return nil
	}
	var names []string
	for _, entry := range entries {
		if !entry.IsDir() || entry.Type()&os.ModeSymlink != 0 {
			continue
		}
		if !isAccountDirName(entry.Name()) {
			continue
		}
		if !hasCopyableFiles(filepath.Join(app, entry.Name())) {
			continue
		}
		names = append(names, entry.Name())
	}
	sort.Strings(names)
	return names
}

func chooseSource(candidates []string) string {
	for _, candidate := range candidates {
		app := filepath.Dir(candidate)
		info, err := os.Stat(app)
		if err != nil || !info.IsDir() {
			continue
		}
		if hasCopyableFiles(candidate) || len(accountDirs(app)) > 0 {
			return candidate
		}
	}
	return ""
}

func conflictingFiles(files []string, destRoot string) []string {
	var conflicts []string
	for _, rel := range files {
		if _, err := os.Lstat(filepath.Join(destRoot, rel)); err == nil {
			conflicts = append(conflicts, rel)
		}
	}
	return conflicts
}

func accountOccupied(destApp, name string) bool {
	return hasCopyableFiles(filepath.Join(destApp, name))
}

func samePath(platform, a, b string) bool {
	aa := filepath.Clean(absPath(a))
	bb := filepath.Clean(absPath(b))
	if platform == "windows" {
		return strings.EqualFold(aa, bb)
	}
	return aa == bb
}

func absPath(path string) string {
	abs, err := filepath.Abs(path)
	if err != nil {
		return path
	}
	return abs
}

func containsPath(platform, parent, child string) bool {
	if samePath(platform, parent, child) {
		return false
	}
	rel, err := filepath.Rel(filepath.Clean(absPath(parent)), filepath.Clean(absPath(child)))
	if err != nil {
		return false
	}
	if platform == "windows" {
		rel = strings.ToLower(rel)
	}
	return rel != ".." && !strings.HasPrefix(rel, ".."+string(os.PathSeparator))
}

func pathsOverlap(platform, source, dest string) bool {
	srcApp := filepath.Dir(source)
	dstApp := filepath.Dir(dest)
	return samePath(platform, source, dest) ||
		samePath(platform, srcApp, dstApp) ||
		containsPath(platform, srcApp, dstApp) ||
		containsPath(platform, dstApp, srcApp)
}

func copyFile(src, dst string, overwrite bool) (bool, error) {
	info, err := os.Lstat(dst)
	if err == nil {
		if !overwrite {
			return false, nil
		}
		if info.Mode()&os.ModeSymlink != 0 || info.IsDir() {
			if err := os.Remove(dst); err != nil {
				return false, err
			}
		}
	} else if !os.IsNotExist(err) {
		return false, err
	}
	if err := os.MkdirAll(filepath.Dir(dst), 0o755); err != nil {
		return false, err
	}
	in, err := os.Open(src)
	if err != nil {
		return false, err
	}
	defer in.Close()
	srcInfo, err := in.Stat()
	if err != nil {
		return false, err
	}
	out, err := os.OpenFile(dst, os.O_CREATE|os.O_TRUNC|os.O_WRONLY, srcInfo.Mode().Perm())
	if err != nil {
		return false, err
	}
	_, copyErr := io.Copy(out, in)
	closeErr := out.Close()
	if copyErr != nil {
		return false, copyErr
	}
	if closeErr != nil {
		return false, closeErr
	}
	return true, nil
}

func copyTree(srcRoot, dstRoot string, items []treeItem, overwrite bool) (copied, left int, err error) {
	for _, item := range items {
		if item.skip != "" {
			continue
		}
		target := filepath.Join(dstRoot, item.rel)
		if item.kind == "dir" {
			if err := os.MkdirAll(target, 0o755); err != nil {
				return copied, left, err
			}
			continue
		}
		wrote, err := copyFile(filepath.Join(srcRoot, item.rel), target, overwrite)
		if err != nil {
			return copied, left, err
		}
		if wrote {
			copied++
		} else {
			left++
		}
	}
	return copied, left, nil
}

func countSkips(items []treeItem) (credentials, caches, logs, links int) {
	for _, item := range items {
		switch item.skip {
		case "credentials":
			credentials++
		case "cache":
			caches++
		case "logs":
			logs++
		case "link":
			links++
		}
	}
	return credentials, caches, logs, links
}

func describeSkips(b *strings.Builder, items []treeItem) {
	credentials, caches, logs, links := countSkips(items)
	if credentials > 0 {
		fmt.Fprintf(b, "Left %d stored-credential path(s) behind.\n", credentials)
	}
	if caches > 0 {
		fmt.Fprintf(b, "Left %d cache path(s) behind.\n", caches)
	}
	if logs > 0 {
		fmt.Fprintf(b, "Left %d log path(s) behind.\n", logs)
	}
	if links > 0 {
		fmt.Fprintf(b, "Left %d link(s) behind.\n", links)
	}
}

func relHasBase(rel, name string) bool {
	return strings.EqualFold(filepath.Base(rel), name)
}

func treeHasFile(items []treeItem, name string) bool {
	for _, item := range items {
		if item.kind == "file" && item.skip == "" && relHasBase(item.rel, name) {
			return true
		}
	}
	return false
}

func toolbarLine(b *strings.Builder, items []treeItem, srcRoot, dstRoot string) {
	for _, item := range items {
		if item.kind == "file" && item.skip == "" && relHasBase(item.rel, "toolbars.xml") {
			fmt.Fprintf(b, "toolbars.xml\n  from %s\n  to   %s\n",
				filepath.Join(srcRoot, item.rel), filepath.Join(dstRoot, item.rel))
			return
		}
	}
}

// Run locates Firestorm and Folderstorm settings and copies what was accepted.
func Run(opts Options) Result {
	var out strings.Builder
	if opts.Platform == "" {
		return Result{Code: exitError, Stderr: unsupportedPlatform + "\n"}
	}
	if opts.Environ == nil {
		opts.Environ = environMap(os.Environ())
	}
	home := homeDir(opts.Environ)

	var source string
	if opts.Source != "" {
		source = asUserSettings(expandUser(opts.Source, home))
	} else {
		candidates, err := sourceCandidates(opts.Platform, home, opts.Environ)
		if err != nil {
			return Result{Code: exitError, Stderr: err.Error() + "\n"}
		}
		source = chooseSource(candidates)
	}
	dest, err := destinationDir(opts.Platform, home, opts.Environ, opts.Dest)
	if err != nil {
		return Result{Code: exitError, Stderr: err.Error() + "\n"}
	}

	srcApp := ""
	if source != "" {
		srcApp = filepath.Dir(source)
	}
	dstApp := filepath.Dir(dest)
	settingsUseful := source != "" && hasCopyableFiles(source)
	var accounts []string
	if srcApp != "" {
		accounts = accountDirs(srcApp)
	}

	if opts.Check {
		if !settingsUseful {
			return Result{Code: exitNoSource}
		}
		if pathsOverlap(opts.Platform, source, dest) {
			return Result{Code: exitError, Stderr: "Source and destination are the same folder or one is inside the other.\n"}
		}
		items, err := walkTree(source)
		if err != nil {
			return Result{Code: exitError, Stderr: "Could not read settings: " + err.Error() + "\n"}
		}
		if len(conflictingFiles(copyableFiles(items), dest)) > 0 {
			return Result{Code: exitDestExists}
		}
		return Result{Code: exitOK}
	}
	if opts.CheckAccounts {
		if len(accounts) == 0 {
			return Result{Code: exitNoSource}
		}
		if settingsUseful && pathsOverlap(opts.Platform, source, dest) {
			return Result{Code: exitError, Stderr: "Source and destination are the same folder or one is inside the other.\n"}
		}
		if source != "" && pathsOverlap(opts.Platform, source, dest) {
			return Result{Code: exitError, Stderr: "Source and destination are the same folder or one is inside the other.\n"}
		}
		for _, name := range accounts {
			if accountOccupied(dstApp, name) {
				return Result{Code: exitDestExists}
			}
		}
		return Result{Code: exitOK}
	}

	if !settingsUseful && len(accounts) == 0 {
		where := "the usual Firestorm folder"
		if source != "" {
			where = source
		}
		return Result{Code: exitNoSource, Stderr: "No Firestorm settings to copy (looked for " + where + ").\n"}
	}
	if source != "" && pathsOverlap(opts.Platform, source, dest) {
		return Result{Code: exitError, Stderr: "Source and destination are the same folder or one is inside the other.\n"}
	}

	if opts.DryRun {
		if settingsUseful {
			items, err := walkTree(source)
			if err != nil {
				return Result{Code: exitError, Stderr: "Could not read settings: " + err.Error() + "\n"}
			}
			files := copyableFiles(items)
			conflicts := conflictingFiles(files, dest)
			fmt.Fprintf(&out, "From: %s\nTo:   %s\n", source, dest)
			if len(conflicts) > 0 && !opts.Overwrite {
				fmt.Fprintf(&out, "Folderstorm already has %d settings file(s). Those stay unless you pass --overwrite. Files Folderstorm does not have are still copied.\n", len(conflicts))
			}
			fmt.Fprintf(&out, "Would copy %d settings file(s).\n", len(files))
			describeSkips(&out, items)
		}
		if len(accounts) > 0 {
			if !opts.Accounts {
				fmt.Fprintf(&out, "Account folders are a separate copy. Pass --accounts to include them.\n")
			}
			for _, name := range accounts {
				srcDir := filepath.Join(srcApp, name)
				dstDir := filepath.Join(dstApp, name)
				items, err := walkTree(srcDir)
				if err != nil {
					return Result{Code: exitError, Stderr: "Could not read account folder: " + err.Error() + "\n"}
				}
				occupied := accountOccupied(dstApp, name)
				fmt.Fprintf(&out, "Account %s\n  from %s\n  to   %s\n", name, srcDir, dstDir)
				toolbarLine(&out, items, srcDir, dstDir)
				if occupied && !opts.OverwriteAccounts {
					fmt.Fprintf(&out, "Folderstorm already has this account folder. It stays unless you pass --overwrite-accounts.\n")
				} else {
					fmt.Fprintf(&out, "Would copy %d file(s) from this account folder.\n", len(copyableFiles(items)))
				}
				describeSkips(&out, items)
			}
		}
		out.WriteString("Dry run. Nothing was written.\n")
		return Result{Code: exitOK, Stdout: out.String()}
	}

	didSettings := false
	didAccounts := false

	if settingsUseful && (opts.Yes || opts.Interactive) {
		accept := opts.Yes
		if !accept && opts.Ask != nil {
			accept = opts.Ask(fmt.Sprintf(
				"Copy Firestorm settings\n  from %s\n  to   %s\nSaved passwords and caches are not copied.\nCopy these settings?",
				source, dest))
		}
		if accept {
			items, err := walkTree(source)
			if err != nil {
				return Result{Code: exitError, Stderr: "Could not read settings: " + err.Error() + "\n"}
			}
			overwrite := opts.Overwrite
			conflicts := conflictingFiles(copyableFiles(items), dest)
			if len(conflicts) > 0 && !overwrite && !opts.Yes && opts.Ask != nil {
				overwrite = opts.Ask(fmt.Sprintf(
					"Folderstorm already has settings files in %s\nReplace those files? Choosing no still copies files Folderstorm does not have. Files that exist only in Folderstorm are kept.",
					dest))
			}
			copied, left, err := copyTree(source, dest, items, overwrite)
			if err != nil {
				return Result{Code: exitError, Stderr: "Could not copy settings: " + err.Error() + "\n"}
			}
			fmt.Fprintf(&out, "From: %s\nTo:   %s\nCopied %d settings file(s).\n", source, dest, copied)
			if left > 0 {
				fmt.Fprintf(&out, "Left %d existing settings file(s) in place.\n", left)
			}
			describeSkips(&out, items)
			didSettings = true
		}
	}

	if len(accounts) > 0 && (opts.Accounts || opts.Interactive) {
		accept := opts.Accounts
		if !accept && opts.Ask != nil {
			var listed strings.Builder
			for _, name := range accounts {
				fmt.Fprintf(&listed, "  %s\n", name)
			}
			accept = opts.Ask(fmt.Sprintf(
				"Copy Firestorm account folders into Folderstorm?\nThis is separate from the main settings copy. Each folder includes toolbar layout (toolbars.xml) and the rest of that account.\nSaved passwords, cookies, assistant tokens, caches, and logs are not copied.\n%sCopy these account folders?",
				listed.String()))
		}
		if accept {
			didAccounts = true
			for _, name := range accounts {
				srcDir := filepath.Join(srcApp, name)
				dstDir := filepath.Join(dstApp, name)
				items, err := walkTree(srcDir)
				if err != nil {
					return Result{Code: exitError, Stderr: "Could not read account folder: " + err.Error() + "\n"}
				}
				occupied := accountOccupied(dstApp, name)
				replace := opts.OverwriteAccounts
				if occupied && !replace {
					confirm := opts.Interactive && opts.Ask != nil && opts.Ask(fmt.Sprintf(
						"Folderstorm already has an account folder for %s\n  %s\nReplace that account folder? Choosing no leaves it unchanged.",
						name, dstDir))
					if !confirm {
						fmt.Fprintf(&out, "Left account folder %s unchanged.\n", dstDir)
						fmt.Fprintf(&out, "Account %s\n  from %s\n  to   %s\n", name, srcDir, dstDir)
						toolbarLine(&out, items, srcDir, dstDir)
						continue
					}
					replace = true
				}
				copied, left, err := copyTree(srcDir, dstDir, items, replace)
				if err != nil {
					return Result{Code: exitError, Stderr: "Could not copy account folder: " + err.Error() + "\n"}
				}
				fmt.Fprintf(&out, "Account %s\n  from %s\n  to   %s\nCopied %d file(s) from this account folder.\n",
					name, srcDir, dstDir, copied)
				toolbarLine(&out, items, srcDir, dstDir)
				if left > 0 {
					fmt.Fprintf(&out, "Left %d existing file(s) in this account folder.\n", left)
				}
				describeSkips(&out, items)
				didAccounts = true
			}
		}
	}

	if didSettings || didAccounts {
		return Result{Code: exitOK, Stdout: out.String()}
	}
	if opts.Interactive {
		return Result{Code: exitDeclined, Stderr: "Leaving settings unchanged.\n", Stdout: out.String()}
	}
	if !opts.Yes && !opts.Accounts {
		return Result{
			Code: exitDeclined,
			Stderr: "Not copying settings. Re-run with --yes to copy settings, " +
				"--overwrite to replace existing settings files, " +
				"--accounts to copy per-account folders, and " +
				"--overwrite-accounts to replace an existing account folder.\n",
		}
	}
	where := "the usual Firestorm folder"
	if source != "" {
		where = source
	}
	return Result{Code: exitNoSource, Stderr: "No Firestorm settings to copy (looked for " + where + ").\n"}
}

func environMap(pairs []string) map[string]string {
	env := make(map[string]string, len(pairs))
	for _, pair := range pairs {
		key, value, ok := strings.Cut(pair, "=")
		if ok {
			env[key] = value
		}
	}
	return env
}
