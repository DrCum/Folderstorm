//go:build windows

package main

import (
	"encoding/binary"
	"fmt"
	"path/filepath"
	"runtime"
	"strings"
	"syscall"
	"unsafe"
)

const (
	readAttributes     = 0x80
	synchronize        = 0x100000
	deleteAccess       = 0x10000
	openReparsePoint   = 0x00200000 // NT FILE_OPEN_REPARSE_POINT
	synchronousIO      = 0x20
	backupIntent       = 0x4000
	attributeDirectory = 0x10
	attributeReparse   = 0x400
	getReparsePoint    = 0x000900a8
)

var (
	kernel32             = syscall.NewLazyDLL("kernel32.dll")
	getFileInformationEx = kernel32.NewProc("GetFileInformationByHandleEx")
	setFileInformation   = kernel32.NewProc("SetFileInformationByHandle")
	getFinalPath         = kernel32.NewProc("GetFinalPathNameByHandleW")
	ntdll                = syscall.NewLazyDLL("ntdll.dll")
	ntCreateFile         = ntdll.NewProc("NtCreateFile")
	ntError              = ntdll.NewProc("RtlNtStatusToDosError")
	shell32              = syscall.NewLazyDLL("shell32.dll")
	knownFolderPath      = shell32.NewProc("SHGetKnownFolderPath")
	coTaskMemFree        = syscall.NewLazyDLL("ole32.dll").NewProc("CoTaskMemFree")
)

type fileID struct {
	Volume uint64
	ID     [16]byte
}
type handles []syscall.Handle

func (h *handles) add(value syscall.Handle) syscall.Handle { *h = append(*h, value); return value }
func (h *handles) close() {
	for i := len(*h) - 1; i >= 0; i-- {
		syscall.CloseHandle((*h)[i])
	}
	*h = nil
}

func nativeError(err error) error {
	switch err {
	case syscall.ERROR_FILE_NOT_FOUND, syscall.ERROR_PATH_NOT_FOUND:
		return refuse(absent, "path_missing")
	case syscall.Errno(32), syscall.Errno(33):
		return refuse(busy, "sharing_or_lock_conflict")
	case syscall.ERROR_ACCESS_DENIED, syscall.Errno(1314):
		return refuse(accessDenied, "insufficient_access")
	default:
		return refuse(unverified, "native_operation_failed")
	}
}

func fileIdentity(h syscall.Handle) (fileID, error) {
	var id fileID
	ok, _, err := getFileInformationEx.Call(uintptr(h), 18 /* FileIdInfo */, uintptr(unsafe.Pointer(&id)), unsafe.Sizeof(id))
	if ok == 0 {
		return id, nativeError(err)
	}
	return id, nil
}

func attributes(h syscall.Handle) (uint32, error) {
	var info syscall.ByHandleFileInformation
	if err := syscall.GetFileInformationByHandle(h, &info); err != nil {
		return 0, nativeError(err)
	}
	return info.FileAttributes, nil
}

// A handle-relative open avoids a new lookup through mutable ancestors. The
// caller holds parent handles; neither the inspected entry nor those parents
// permit a competing write, rename, deletion or reparse-point change.
func openRelative(parent syscall.Handle, name string, access uint32, nofollow bool) (syscall.Handle, error) {
	u, err := syscall.UTF16FromString(name)
	if err != nil || len(u)*2 > 65535 {
		return 0, refuse(unverified, "invalid_path_component")
	}
	type unicodeString struct {
		Length, MaximumLength uint16
		Buffer                *uint16
	}
	type objectAttributes struct {
		Length                                       uint32
		RootDirectory                                syscall.Handle
		ObjectName                                   *unicodeString
		Attributes                                   uint32
		SecurityDescriptor, SecurityQualityOfService uintptr
	}
	nameBuffer := unicodeString{uint16((len(u) - 1) * 2), uint16(len(u) * 2), &u[0]}
	oa := objectAttributes{RootDirectory: parent, ObjectName: &nameBuffer}
	oa.Length = uint32(unsafe.Sizeof(oa))
	var handle syscall.Handle
	var iosb struct{ Status, Information uintptr }
	options := uint32(synchronousIO | backupIntent)
	if nofollow {
		options |= openReparsePoint
	}
	status, _, _ := ntCreateFile.Call(
		uintptr(unsafe.Pointer(&handle)), uintptr(access|synchronize), uintptr(unsafe.Pointer(&oa)),
		uintptr(unsafe.Pointer(&iosb)), 0, 0, syscall.FILE_SHARE_READ,
		1 /* FILE_OPEN */, uintptr(options), 0, 0)
	runtime.KeepAlive(u)
	runtime.KeepAlive(nameBuffer)
	if int32(status) < 0 {
		code, _, _ := ntError.Call(status)
		return 0, nativeError(syscall.Errno(code))
	}
	return handle, nil
}

func readReparse(h syscall.Handle) ([]byte, error) {
	b := make([]byte, 16384)
	var count uint32
	if err := syscall.DeviceIoControl(h, getReparsePoint, nil, 0, &b[0], uint32(len(b)), &count, nil); err != nil {
		return nil, nativeError(err)
	}
	if count < 8 || count > uint32(len(b)) || int(binary.LittleEndian.Uint16(b[4:]))+8 > int(count) {
		return nil, refuse(unverified, "invalid_reparse_data")
	}
	return b[:count], nil
}

func absolutePath(path string) (string, error) {
	path = strings.ReplaceAll(path, "/", `\`)
	if !filepath.IsAbs(path) || strings.ContainsRune(path, 0) {
		return "", fmt.Errorf("install directory must be an absolute Windows path")
	}
	// Device namespaces and drive-relative paths are not installation paths.
	if strings.HasPrefix(path, `\\.\`) || strings.HasPrefix(strings.ToUpper(path), `\\?\GLOBALROOT\`) {
		return "", fmt.Errorf("unsupported installation path namespace")
	}
	path = filepath.Clean(path)
	if strings.HasPrefix(path, `\\?\`) {
		return path, nil
	}
	if strings.HasPrefix(path, `\\`) {
		return `\\?\UNC\` + path[2:], nil
	}
	return `\\?\` + path, nil
}

// Pin every encountered directory entry, including junctions/symlinks such as
// Velopack's current. Following a redirector happens relative to its already
// pinned parent; subsequent operations use the followed handle. A current
// switch during inspection must therefore fail sharing or leave cleanup busy.
func openDirectory(path string, held *handles) (syscall.Handle, error) {
	redirects := 0
	return openDirectoryWithRedirects(path, held, &redirects)
}

func openDirectoryWithRedirects(path string, held *handles, redirects *int) (syscall.Handle, error) {
	path, err := absolutePath(path)
	if err != nil {
		return 0, refuse(unverified, "invalid_directory_path")
	}
	volume := filepath.VolumeName(path)
	if volume == "" {
		return 0, refuse(unverified, "invalid_directory_root")
	}
	root, err := syscall.UTF16PtrFromString(volume + `\`)
	if err != nil {
		return 0, refuse(unverified, "invalid_directory_root")
	}
	h, err := syscall.CreateFile(root, readAttributes|synchronize, syscall.FILE_SHARE_READ, nil,
		syscall.OPEN_EXISTING, syscall.FILE_FLAG_BACKUP_SEMANTICS|syscall.FILE_FLAG_OPEN_REPARSE_POINT, 0)
	if err != nil {
		return 0, nativeError(err)
	}
	held.add(h)
	attr, err := attributes(h)
	if err != nil {
		return 0, err
	}
	if attr&attributeDirectory == 0 || attr&attributeReparse != 0 {
		return 0, refuse(unverified, "unverified_directory_root")
	}
	for _, component := range strings.Split(strings.TrimPrefix(path[len(volume):], `\`), `\`) {
		if component == "" {
			continue
		}
		entry, err := openRelative(h, component, readAttributes, true)
		if err != nil {
			return 0, err
		}
		held.add(entry)
		attr, err := attributes(entry)
		if err != nil {
			return 0, err
		}
		if attr&attributeDirectory == 0 {
			return 0, refuse(unverified, "parent_is_not_directory")
		}
		if attr&attributeReparse != 0 {
			*redirects = *redirects + 1
			if *redirects > 32 {
				return 0, refuse(unverified, "directory_redirect_limit")
			}
			data, err := readReparse(entry)
			if err != nil {
				return 0, err
			}
			tag := binary.LittleEndian.Uint32(data)
			if tag != symlinkTag && tag != mountPointTag {
				return 0, refuse(unverified, "unknown_directory_reparse_tag")
			}
			var target linkTarget
			if tag == symlinkTag {
				target, err = parseSymlink(data)
			} else {
				target, err = parseMountPoint(data)
			}
			if err != nil {
				return 0, refuse(unverified, "invalid_directory_reparse_data")
			}
			targetDirectory, err := targetPath(target, h)
			if err != nil {
				return 0, err
			}
			// Pin every redirector in the destination too. A chain such as
			// current -> selected-version -> app-1 must not leave the second
			// junction free to switch after ownership was checked.
			resolved, err := openDirectoryWithRedirects(targetDirectory, held, redirects)
			if err != nil {
				return 0, err
			}
			followed, err := openRelative(h, component, readAttributes, false)
			if err != nil {
				return 0, err
			}
			held.add(followed)
			attr, err := attributes(followed)
			if err != nil {
				return 0, err
			}
			if attr&attributeDirectory == 0 || attr&attributeReparse != 0 {
				return 0, refuse(unverified, "unverified_followed_directory")
			}
			resolvedID, err := fileIdentity(resolved)
			if err != nil {
				return 0, err
			}
			followedID, err := fileIdentity(followed)
			if err != nil {
				return 0, err
			}
			if followedID != resolvedID {
				return 0, refuse(unverified, "directory_redirect_identity_mismatch")
			}
			h = resolved
		} else {
			h = entry
		}
	}
	return h, nil
}

func finalPath(h syscall.Handle) (string, error) {
	for size := uint32(256); size <= 65536; {
		b := make([]uint16, size)
		count, _, err := getFinalPath.Call(uintptr(h), uintptr(unsafe.Pointer(&b[0])), uintptr(size), 0)
		if count == 0 {
			return "", nativeError(err)
		}
		if count < uintptr(size) {
			return syscall.UTF16ToString(b[:count]), nil
		}
		size = uint32(count) + 1
	}
	return "", refuse(unverified, "directory_path_too_long")
}

func programData() (string, error) {
	// FOLDERID_ProgramData. Never use the process environment as deletion input.
	id := syscall.GUID{Data1: 0x62ab5d82, Data2: 0xfdc1, Data3: 0x4dc3, Data4: [8]byte{0xa9, 0xdd, 0x07, 0x0d, 0x1d, 0x49, 0x5d, 0x97}}
	var ptr *uint16
	hr, _, _ := knownFolderPath.Call(uintptr(unsafe.Pointer(&id)), 0, 0, uintptr(unsafe.Pointer(&ptr)))
	if int32(hr) < 0 || ptr == nil {
		return "", refuse(unverified, "programdata_unavailable")
	}
	defer coTaskMemFree.Call(uintptr(unsafe.Pointer(ptr)))
	// The shell owns a terminated Unicode string; cap scanning to supported
	// Windows path length rather than constructing an unbounded unsafe slice.
	n := 0
	for n < 32768 && *(*uint16)(unsafe.Add(unsafe.Pointer(ptr), n*2)) != 0 {
		n++
	}
	if n == 32768 {
		return "", refuse(unverified, "programdata_path_too_long")
	}
	return string(syscall.UTF16ToString(unsafe.Slice(ptr, n))), nil
}

type installation struct {
	held                  handles
	dir                   syscall.Handle
	directoryID, binaryID fileID
	acceptedDirectories   map[string]bool
}

func inspectInstallation(path string) (*installation, error) {
	trustedPath, err := absolutePath(path)
	if err != nil {
		return nil, refuse(unverified, "invalid_installation_path")
	}
	i := &installation{acceptedDirectories: map[string]bool{trustedPath: true}}
	dir, err := openDirectory(path, &i.held)
	if err != nil {
		i.held.close()
		return nil, refuse(unverified, "installation_directory_unverified")
	}
	i.dir = dir
	// The trusted installer spelling and the final spelling of this pinned
	// directory are the only accepted target parents. Do this lexical check
	// before opening a candidate's target: a hostile UNC link must not cause an
	// uninstall to contact a different server or hang on an untrusted path.
	if resolved, err := finalPath(dir); err == nil {
		if resolved, err = absolutePath(resolved); err == nil {
			i.acceptedDirectories[resolved] = true
		}
	}
	i.directoryID, err = fileIdentity(dir)
	if err != nil {
		i.held.close()
		return nil, refuse(unverified, "installation_directory_unverified")
	}
	binary, err := openRelative(dir, "fs-mcp.exe", readAttributes, true)
	if err != nil {
		i.held.close()
		return nil, refuse(unverified, "packaged_sidecar_unverified")
	}
	i.held.add(binary)
	attr, err := attributes(binary)
	if err != nil || attr&(attributeDirectory|attributeReparse) != 0 {
		i.held.close()
		return nil, refuse(unverified, "packaged_sidecar_not_regular_file")
	}
	i.binaryID, err = fileIdentity(binary)
	if err != nil {
		i.held.close()
		return nil, refuse(unverified, "packaged_sidecar_unverified")
	}
	return i, nil
}

type nativeCandidate struct {
	held         handles
	link         syscall.Handle
	parent       syscall.Handle
	name         string
	installation *installation
	beforeDelete func() // deterministic Windows test barrier; nil in production
}

func (c *nativeCandidate) Close() { c.held.close() }

func targetPath(target linkTarget, legacyParent syscall.Handle) (string, error) {
	name := target.name
	// Do not clean a malformed/dangling stored target into an owned spelling,
	// or erase an intermediate component that could traverse an untrusted
	// redirector. Relative links may ascend first, then descend normally.
	if strings.HasSuffix(name, `\`) || strings.HasSuffix(name, "/") {
		return "", refuse(unverified, "target_has_trailing_separator")
	}
	descending := false
	for _, component := range strings.Split(strings.ReplaceAll(name, "/", `\`), `\`) {
		if component == "" {
			continue
		}
		if component == "." || (component == ".." && (!target.relative || descending)) {
			return "", refuse(unverified, "ambiguous_target_components")
		}
		if component != ".." {
			descending = true
		}
	}
	if target.relative {
		if filepath.IsAbs(name) || filepath.VolumeName(name) != "" || strings.HasPrefix(name, `\`) {
			return "", refuse(unverified, "invalid_relative_symlink_target")
		}
		parent, err := finalPath(legacyParent)
		if err != nil {
			return "", err
		}
		return absolutePath(filepath.Join(parent, name))
	}
	if strings.HasPrefix(name, `\??\UNC\`) {
		name = `\\?\UNC\` + name[len(`\??\UNC\`):]
	} else if strings.HasPrefix(name, `\??\`) {
		name = `\\?\` + name[len(`\??\`):]
	}
	path, err := absolutePath(name)
	if err != nil {
		return "", refuse(unverified, "unsupported_symlink_target")
	}
	return path, nil
}

func (c *nativeCandidate) Inspect() error {
	attr, err := attributes(c.link)
	if err != nil {
		return err
	}
	if attr&attributeDirectory != 0 {
		return refuse(notOwned, "candidate_is_directory")
	}
	if attr&attributeReparse == 0 {
		return refuse(notOwned, "candidate_is_not_symlink")
	}
	data, err := readReparse(c.link)
	if err != nil {
		return err
	}
	if binary.LittleEndian.Uint32(data) != symlinkTag {
		return refuse(notOwned, "candidate_is_not_file_symlink")
	}
	target, err := parseSymlink(data)
	if err != nil {
		return refuse(unverified, "invalid_symlink_reparse_data")
	}
	path, err := targetPath(target, c.parent)
	if err != nil {
		return err
	}
	// Exact case is intentionally conservative, including on case-sensitive
	// Windows directories. Uppercase spellings are left for manual migration.
	if filepath.Base(path) != "fs-mcp.exe" {
		return refuse(notOwned, "target_name_mismatch")
	}
	if !c.installation.acceptedDirectories[filepath.Dir(path)] {
		return refuse(notOwned, "target_path_mismatch")
	}
	parent, err := openDirectory(filepath.Dir(path), &c.held)
	if err != nil {
		return refuse(unverified, "target_directory_unverified")
	}
	id, err := fileIdentity(parent)
	if err != nil {
		return refuse(unverified, "target_directory_unverified")
	}
	if id != c.installation.directoryID {
		return refuse(notOwned, "target_directory_mismatch")
	}
	targetFile, err := openRelative(parent, "fs-mcp.exe", readAttributes, true)
	if err != nil {
		return refuse(unverified, "target_file_unverified")
	}
	c.held.add(targetFile)
	attr, err = attributes(targetFile)
	if err != nil || attr&(attributeDirectory|attributeReparse) != 0 {
		return refuse(unverified, "target_is_not_regular_file")
	}
	id, err = fileIdentity(targetFile)
	if err != nil {
		return refuse(unverified, "target_file_unverified")
	}
	if id != c.installation.binaryID {
		return refuse(notOwned, "target_file_mismatch")
	}
	// Also follow the exact stored link, without lexical rewriting, to prove
	// it is live and reaches the same file. Its name, parents and all accepted
	// target context remain pinned, so this cannot follow a substituted link.
	followed, err := openRelative(c.parent, c.name, readAttributes, false)
	if err != nil {
		return refuse(unverified, "stored_target_unverified")
	}
	c.held.add(followed)
	id, err = fileIdentity(followed)
	if err != nil {
		return refuse(unverified, "stored_target_unverified")
	}
	if id != c.installation.binaryID {
		return refuse(notOwned, "stored_target_file_mismatch")
	}
	return nil
}

func (c *nativeCandidate) Delete() error {
	if c.beforeDelete != nil {
		c.beforeDelete()
	}
	deleteFile := uint32(1)
	ok, _, err := setFileInformation.Call(uintptr(c.link), 4, /* FileDispositionInfo */
		uintptr(unsafe.Pointer(&deleteFile)), unsafe.Sizeof(deleteFile))
	if ok == 0 {
		return nativeError(err)
	}
	return nil
}

func cleanupOwnedLegacy(installDir string) (report, error) {
	if _, err := absolutePath(installDir); err != nil {
		return report{}, err
	}
	data, err := programData()
	if err != nil {
		return skippedReport(`Folderstorm`, err), nil
	}
	return cleanupAt(installDir, filepath.Join(data, "Folderstorm"), nil), nil
}

var legacyNames = []string{"fs-mcp.exe", "fs-mcp.exe.replacing"}

func skippedReport(parent string, err error) report {
	r := report{Candidates: make([]result, 0, len(legacyNames))}
	for _, name := range legacyNames {
		r.Candidates = append(r.Candidates, cleanupCandidate(filepath.Join(parent, name), func() (candidate, error) { return nil, err }))
	}
	return r
}

// legacyDir is private to this implementation/tests. The public CLI accepts
// no cleanup destination; production always obtains ProgramData from the shell.
func cleanupAt(installDir, legacyDir string, beforeDelete func()) report {
	i, err := inspectInstallation(installDir)
	if err != nil {
		return skippedReport(legacyDir, err)
	}
	defer i.held.close()
	var held handles
	defer held.close()
	parent, err := openDirectory(filepath.Dir(legacyDir), &held)
	if err != nil {
		return skippedReport(legacyDir, err)
	}
	legacyParent, err := openRelative(parent, filepath.Base(legacyDir), readAttributes, true)
	if err != nil {
		return skippedReport(legacyDir, err)
	}
	held.add(legacyParent)
	attr, err := attributes(legacyParent)
	if err != nil {
		return skippedReport(legacyDir, err)
	}
	if attr&attributeDirectory == 0 || attr&attributeReparse != 0 {
		return skippedReport(legacyDir, refuse(unverified, "legacy_parent_is_not_plain_directory"))
	}
	r := report{Candidates: make([]result, 0, len(legacyNames))}
	for _, name := range legacyNames {
		name := name
		r.Candidates = append(r.Candidates, cleanupCandidate(filepath.Join(legacyDir, name), func() (candidate, error) {
			link, err := openRelative(legacyParent, name, readAttributes|deleteAccess, true)
			if err != nil {
				return nil, err
			}
			return &nativeCandidate{held: handles{link}, link: link, parent: legacyParent, name: name, installation: i, beforeDelete: beforeDelete}, nil
		}))
	}
	return r
}
