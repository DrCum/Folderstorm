//go:build windows

package main

import (
	"encoding/binary"
	"errors"
	"os"
	"path/filepath"
	"strings"
	"syscall"
	"testing"
	"unicode/utf16"
)

type fixture struct{ root, install, other, legacy, binary, alias, backup string }

func newFixture(t *testing.T) fixture {
	t.Helper()
	f := fixture{root: t.TempDir()}
	f.install = filepath.Join(f.root, "Install % & (测试)")
	f.other = filepath.Join(f.root, "Install % & (测试)-other")
	f.legacy = filepath.Join(f.root, "ProgramData", "Folderstorm")
	for _, dir := range []string{f.install, f.other, f.legacy} {
		if err := os.MkdirAll(dir, 0700); err != nil {
			t.Fatal(err)
		}
	}
	f.binary = filepath.Join(f.install, "fs-mcp.exe")
	for _, path := range []string{f.binary, filepath.Join(f.other, "fs-mcp.exe")} {
		if err := os.WriteFile(path, []byte("packaged sidecar fixture"), 0600); err != nil {
			t.Fatal(err)
		}
	}
	f.alias, f.backup = filepath.Join(f.legacy, "fs-mcp.exe"), filepath.Join(f.legacy, "fs-mcp.exe.replacing")
	// FileIdInfo is the ownership authority; do not turn unsupported filesystems
	// into false test failures or replace it with weaker pathname comparisons.
	dir, err := os.Open(f.install)
	if err != nil {
		t.Fatal(err)
	}
	_, identityErr := fileIdentity(syscall.Handle(dir.Fd()))
	dir.Close()
	if identityErr != nil {
		t.Skipf("filesystem does not support FileIdInfo: %v", identityErr)
	}
	i, err := inspectInstallation(f.install)
	if err != nil {
		t.Fatalf("cannot acquire installation evidence: %v", err)
	}
	i.held.close()
	return f
}

func symlinkOrSkip(t *testing.T, target, path string) {
	t.Helper()
	if err := os.Symlink(target, path); err != nil {
		if errors.Is(err, syscall.Errno(1314)) || errors.Is(err, syscall.ERROR_ACCESS_DENIED) {
			t.Skipf("symlink privilege unavailable: %v", err)
		}
		t.Fatal(err)
	}
}

func junction(t *testing.T, target, path string) {
	t.Helper()
	if err := os.Mkdir(path, 0700); err != nil {
		t.Fatal(err)
	}
	p, err := syscall.UTF16PtrFromString(path)
	if err != nil {
		t.Fatal(err)
	}
	h, err := syscall.CreateFile(p, syscall.GENERIC_WRITE, syscall.FILE_SHARE_READ|syscall.FILE_SHARE_WRITE|syscall.FILE_SHARE_DELETE, nil,
		syscall.OPEN_EXISTING, syscall.FILE_FLAG_OPEN_REPARSE_POINT|syscall.FILE_FLAG_BACKUP_SEMANTICS, 0)
	if err != nil {
		t.Fatal(err)
	}
	defer syscall.CloseHandle(h)
	substitute := utf16.Encode([]rune(`\??\` + target))
	printName := utf16.Encode([]rune(target))
	b := make([]byte, 16+(len(substitute)+len(printName)+2)*2)
	binary.LittleEndian.PutUint32(b, mountPointTag)
	binary.LittleEndian.PutUint16(b[4:], uint16(len(b)-8))
	binary.LittleEndian.PutUint16(b[10:], uint16(len(substitute)*2))
	binary.LittleEndian.PutUint16(b[12:], uint16((len(substitute)+1)*2))
	binary.LittleEndian.PutUint16(b[14:], uint16(len(printName)*2))
	for i, value := range substitute {
		binary.LittleEndian.PutUint16(b[16+2*i:], value)
	}
	for i, value := range printName {
		binary.LittleEndian.PutUint16(b[16+2*(len(substitute)+1+i):], value)
	}
	var count uint32
	if err := syscall.DeviceIoControl(h, 0x900a4 /* FSCTL_SET_REPARSE_POINT */, &b[0], uint32(len(b)), nil, 0, &count, nil); err != nil {
		t.Fatal(err)
	}
}

func expectStatuses(t *testing.T, r report, want ...category) {
	t.Helper()
	if len(r.Candidates) != len(want) {
		t.Fatalf("wrong report length: %+v", r)
	}
	for i, expected := range want {
		if r.Candidates[i].Status != expected {
			t.Fatalf("candidate %d: got %+v want %s", i, r.Candidates[i], expected)
		}
	}
}

func assertExists(t *testing.T, path string) {
	t.Helper()
	if _, err := os.Lstat(path); err != nil {
		t.Fatalf("expected preserved path %q: %v", path, err)
	}
}

func TestWindowsOwnedAliasAndBackupIndependentAndIdempotent(t *testing.T) {
	f := newFixture(t)
	symlinkOrSkip(t, f.binary, f.alias)
	symlinkOrSkip(t, filepath.Join(f.other, "fs-mcp.exe"), f.backup)
	expectStatuses(t, cleanupAt(f.install, f.legacy, nil), removed, notOwned)
	assertExists(t, f.binary)
	assertExists(t, f.backup)
	assertExists(t, f.legacy)
	expectStatuses(t, cleanupAt(f.install, f.legacy, nil), absent, notOwned)
	expectStatuses(t, cleanupAt(f.other, f.legacy, nil), absent, removed)
	assertExists(t, filepath.Join(f.other, "fs-mcp.exe"))
}

func TestWindowsRejectsRegularFileAndHardLink(t *testing.T) {
	f := newFixture(t)
	if err := os.WriteFile(f.alias, []byte("unrelated executable"), 0600); err != nil {
		t.Fatal(err)
	}
	if err := os.Link(f.binary, f.backup); err != nil {
		t.Fatal(err)
	}
	expectStatuses(t, cleanupAt(f.install, f.legacy, nil), notOwned, notOwned)
	assertExists(t, f.alias)
	assertExists(t, f.backup)
}

func TestWindowsRejectsDirectorySymlinkAndReparseParent(t *testing.T) {
	t.Run("directory candidate", func(t *testing.T) {
		f := newFixture(t)
		symlinkOrSkip(t, f.install, f.alias)
		expectStatuses(t, cleanupAt(f.install, f.legacy, nil), notOwned, absent)
		assertExists(t, f.alias)
		assertExists(t, f.binary)
	})
	t.Run("redirected legacy parent", func(t *testing.T) {
		f := newFixture(t)
		real := filepath.Join(f.root, "actual legacy")
		if err := os.Rename(f.legacy, real); err != nil {
			t.Fatal(err)
		}
		symlinkOrSkip(t, f.binary, filepath.Join(real, "fs-mcp.exe"))
		symlinkOrSkip(t, real, f.legacy)
		expectStatuses(t, cleanupAt(f.install, f.legacy, nil), unverified, unverified)
		assertExists(t, filepath.Join(real, "fs-mcp.exe"))
		assertExists(t, f.legacy)
	})
}

func TestWindowsRejectsHardLinkedFileInDifferentDirectory(t *testing.T) {
	f := newFixture(t)
	otherBinary := filepath.Join(f.other, "fs-mcp.exe")
	if err := os.Remove(otherBinary); err != nil {
		t.Fatal(err)
	}
	if err := os.Link(f.binary, otherBinary); err != nil {
		t.Fatal(err)
	}
	symlinkOrSkip(t, otherBinary, f.alias)
	expectStatuses(t, cleanupAt(f.install, f.legacy, nil), notOwned, absent)
	assertExists(t, f.alias)
}

func TestWindowsRelativeAndExtendedTargets(t *testing.T) {
	for _, extended := range []bool{false, true} {
		t.Run(map[bool]string{false: "relative", true: "extended Unicode path"}[extended], func(t *testing.T) {
			f := newFixture(t)
			target, err := filepath.Rel(f.legacy, f.binary)
			if err != nil {
				t.Fatal(err)
			}
			if extended {
				target, err = absolutePath(f.binary)
				if err != nil {
					t.Fatal(err)
				}
			}
			symlinkOrSkip(t, target, f.alias)
			expectStatuses(t, cleanupAt(f.install, f.legacy, nil), removed, absent)
			assertExists(t, f.binary)
		})
	}
}

func TestWindowsLeavesDanglingLinkAndMissingInstallation(t *testing.T) {
	f := newFixture(t)
	missing := filepath.Join(f.root, "missing", "fs-mcp.exe")
	symlinkOrSkip(t, missing, f.alias)
	expectStatuses(t, cleanupAt(f.install, f.legacy, nil), notOwned, absent)
	assertExists(t, f.alias)
	if err := os.Remove(f.binary); err != nil {
		t.Fatal(err)
	}
	expectStatuses(t, cleanupAt(f.install, f.legacy, nil), unverified, unverified)
	assertExists(t, f.alias)
}

func TestWindowsLeavesBusyCandidate(t *testing.T) {
	f := newFixture(t)
	symlinkOrSkip(t, f.binary, f.alias)
	p, err := syscall.UTF16PtrFromString(f.alias)
	if err != nil {
		t.Fatal(err)
	}
	h, err := syscall.CreateFile(p, readAttributes|deleteAccess, syscall.FILE_SHARE_READ|syscall.FILE_SHARE_WRITE|syscall.FILE_SHARE_DELETE, nil,
		syscall.OPEN_EXISTING, syscall.FILE_FLAG_OPEN_REPARSE_POINT|syscall.FILE_FLAG_BACKUP_SEMANTICS, 0)
	if err != nil {
		t.Fatal(err)
	}
	defer syscall.CloseHandle(h)
	expectStatuses(t, cleanupAt(f.install, f.legacy, nil), busy, absent)
	assertExists(t, f.alias)
}

func TestWindowsHeldEvidenceBlocksReplacementAndTargetMutation(t *testing.T) {
	f := newFixture(t)
	symlinkOrSkip(t, f.binary, f.alias)
	barriers := 0
	expectStatuses(t, cleanupAt(f.install, f.legacy, func() {
		barriers++
		for _, pair := range [][2]string{{f.alias, f.alias + ".moved"}, {f.legacy, f.legacy + ".moved"}, {f.install, f.install + ".moved"}, {f.binary, f.binary + ".moved"}} {
			if err := os.Rename(pair[0], pair[1]); err == nil {
				t.Fatalf("held object was renamed: %q", pair[0])
			}
		}
		if err := os.Remove(f.alias); err == nil {
			t.Fatal("inspected link could be removed and substituted")
		}
		if err := os.WriteFile(f.binary, []byte("replacement target"), 0600); err == nil {
			t.Fatal("held packaged sidecar could be overwritten")
		}
		p, err := syscall.UTF16PtrFromString(f.alias)
		if err != nil {
			t.Fatal(err)
		}
		h, err := syscall.CreateFile(p, syscall.GENERIC_WRITE, syscall.FILE_SHARE_READ|syscall.FILE_SHARE_WRITE|syscall.FILE_SHARE_DELETE, nil,
			syscall.OPEN_EXISTING, syscall.FILE_FLAG_OPEN_REPARSE_POINT|syscall.FILE_FLAG_BACKUP_SEMANTICS, 0)
		if err == nil {
			syscall.CloseHandle(h)
			t.Fatal("competing updater obtained write access to inspected reparse point")
		}
	}), removed, absent)
	if barriers != 1 {
		t.Fatalf("barrier calls=%d", barriers)
	}
	b, err := os.ReadFile(f.binary)
	if err != nil || string(b) != "packaged sidecar fixture" {
		t.Fatalf("target modified: %q %v", b, err)
	}
}

func TestWindowsCurrentRedirectorHeldAgainstFrameworkSwitch(t *testing.T) {
	f := newFixture(t)
	current := filepath.Join(f.root, "current")
	junction(t, f.install, current)
	symlinkOrSkip(t, filepath.Join(current, "fs-mcp.exe"), f.alias)
	barriers := 0
	expectStatuses(t, cleanupAt(current, f.legacy, func() {
		barriers++
		if err := os.Remove(current); err == nil {
			t.Fatal("framework current could switch after verification")
		}
		if err := os.Rename(current, current+".old"); err == nil {
			t.Fatal("framework current could be replaced after verification")
		}
	}), removed, absent)
	if barriers != 1 {
		t.Fatalf("barrier calls=%d", barriers)
	}
	assertExists(t, current)
	assertExists(t, f.binary)
}

func TestWindowsNestedCurrentRedirectorsAllHeld(t *testing.T) {
	f := newFixture(t)
	version := filepath.Join(f.root, "selected-version")
	current := filepath.Join(f.root, "current")
	junction(t, f.install, version)
	junction(t, version, current)
	symlinkOrSkip(t, filepath.Join(current, "fs-mcp.exe"), f.alias)
	expectStatuses(t, cleanupAt(current, f.legacy, func() {
		for _, path := range []string{current, version} {
			if err := os.Remove(path); err == nil {
				t.Fatalf("unpinned nested redirector %q", path)
			}
			if err := os.Rename(path, path+".old"); err == nil {
				t.Fatalf("nested redirector could switch %q", path)
			}
		}
	}), removed, absent)
	assertExists(t, current)
	assertExists(t, version)
	assertExists(t, f.binary)
}

func TestWindowsRejectsLegacyParentJunction(t *testing.T) {
	f := newFixture(t)
	real := filepath.Join(f.root, "real legacy")
	if err := os.Rename(f.legacy, real); err != nil {
		t.Fatal(err)
	}
	symlinkOrSkip(t, f.binary, filepath.Join(real, "fs-mcp.exe"))
	junction(t, real, f.legacy)
	expectStatuses(t, cleanupAt(f.install, f.legacy, nil), unverified, unverified)
	assertExists(t, filepath.Join(real, "fs-mcp.exe"))
	assertExists(t, f.legacy)
}

func TestWindowsTargetPathRejectsUnsafeNamespacesAndCase(t *testing.T) {
	for _, target := range []string{`\Device\HarddiskVolume1\fs-mcp.exe`, `\\.\C:\fs-mcp.exe`, `\\?\GLOBALROOT\Device\fs-mcp.exe`} {
		if _, err := targetPath(linkTarget{name: target}, 0); err == nil {
			t.Fatalf("accepted target namespace %q", target)
		}
	}
	if _, err := targetPath(linkTarget{name: `C:\fs-mcp.exe`, relative: true}, 0); err == nil {
		t.Fatal("accepted absolute name with relative flag")
	}
	for _, name := range []string{`C:\install\fs-mcp.exe\`, `C:\install\missing\..\fs-mcp.exe`, `..\install\missing\..\fs-mcp.exe`, `C:\install\.\fs-mcp.exe`} {
		if _, err := targetPath(linkTarget{name: name, relative: strings.HasPrefix(name, "..")}, 0); err == nil {
			t.Fatalf("accepted ambiguous stored target %q", name)
		}
	}
	f := newFixture(t)
	symlinkOrSkip(t, filepath.Join(f.install, strings.ToUpper("fs-mcp.exe")), f.alias)
	expectStatuses(t, cleanupAt(f.install, f.legacy, nil), notOwned, absent)
}

func TestWindowsForeignUNCLinkIsRefusedWithoutTargetLookup(t *testing.T) {
	f := newFixture(t)
	symlinkOrSkip(t, `\\invalid.example.test\unreachable\fs-mcp.exe`, f.alias)
	r := cleanupAt(f.install, f.legacy, nil)
	expectStatuses(t, r, notOwned, absent)
	if r.Candidates[0].Reason != "target_path_mismatch" {
		t.Fatalf("untrusted UNC target was probed: %+v", r)
	}
	assertExists(t, f.alias)
}
