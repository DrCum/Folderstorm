package main

import (
	"encoding/binary"
	"testing"
	"unicode/utf16"
)

func reparseFixture(name string, relative bool) []byte {
	u := utf16.Encode([]rune(name))
	b := make([]byte, 20+len(u)*2)
	binary.LittleEndian.PutUint32(b, symlinkTag)
	binary.LittleEndian.PutUint16(b[4:], uint16(len(b)-8))
	binary.LittleEndian.PutUint16(b[10:], uint16(len(u)*2))
	if relative {
		binary.LittleEndian.PutUint32(b[16:], 1)
	}
	for i, value := range u {
		binary.LittleEndian.PutUint16(b[20+2*i:], value)
	}
	return b
}

func TestParseSymlinkValidUnicodeAndRelativeNames(t *testing.T) {
	for _, name := range []string{`\??\C:\Install & % (测试)\fs-mcp.exe`, `..\install\fs-mcp.exe`, `\??\UNC\server\share\fs-mcp.exe`, `\??\Volume{abc}\fs-mcp.exe`} {
		for _, relative := range []bool{false, true} {
			got, err := parseSymlink(reparseFixture(name, relative))
			if err != nil || got.name != name || got.relative != relative {
				t.Fatalf("got=%+v err=%v", got, err)
			}
		}
	}
}

func TestParseSymlinkRejectsMalformedBuffers(t *testing.T) {
	good := reparseFixture(`\??\C:\install\fs-mcp.exe`, false)
	for n := 0; n < len(good); n++ {
		if _, err := parseSymlink(good[:n]); err == nil {
			t.Fatalf("accepted truncated buffer length %d", n)
		}
	}
	for _, mutate := range []func([]byte){
		func(b []byte) { binary.LittleEndian.PutUint32(b, mountPointTag) },
		func(b []byte) { binary.LittleEndian.PutUint16(b[4:], 11) },
		func(b []byte) { binary.LittleEndian.PutUint16(b[8:], 65534) },
		func(b []byte) { binary.LittleEndian.PutUint16(b[8:], 1) },
		func(b []byte) { binary.LittleEndian.PutUint16(b[10:], 65535) },
		func(b []byte) { binary.LittleEndian.PutUint16(b[12:], 65534) },
		func(b []byte) { binary.LittleEndian.PutUint32(b[16:], 2) },
		func(b []byte) { binary.LittleEndian.PutUint16(b[20:], 0) },
		func(b []byte) { binary.LittleEndian.PutUint16(b[20:], 0xd800) },
		func(b []byte) { binary.LittleEndian.PutUint16(b[20:], 0xdc00) },
	} {
		b := append([]byte(nil), good...)
		mutate(b)
		if _, err := parseSymlink(b); err == nil {
			t.Fatalf("accepted malformed buffer %x", b)
		}
	}
}

func FuzzParseSymlink(f *testing.F) {
	f.Add(reparseFixture(`\??\C:\install\fs-mcp.exe`, false))
	f.Add(reparseFixture(`..\install\fs-mcp.exe`, true))
	f.Add([]byte{0})
	f.Fuzz(func(t *testing.T, data []byte) { parseSymlink(data); parseMountPoint(data) })
}

func TestParseMountPointValidAndMalformed(t *testing.T) {
	good := reparseFixture(`\??\C:\install`, false)
	mount := make([]byte, len(good)-4)
	copy(mount[:16], good[:16])
	copy(mount[16:], good[20:])
	binary.LittleEndian.PutUint32(mount, mountPointTag)
	binary.LittleEndian.PutUint16(mount[4:], uint16(len(mount)-8))
	got, err := parseMountPoint(mount)
	if err != nil || got.name != `\??\C:\install` || got.relative {
		t.Fatalf("got=%+v err=%v", got, err)
	}
	for n := 0; n < len(mount); n++ {
		if _, err := parseMountPoint(mount[:n]); err == nil {
			t.Fatalf("accepted truncated mountpoint length %d", n)
		}
	}
	for _, offset := range []int{8, 10, 12, 14} {
		b := append([]byte(nil), mount...)
		binary.LittleEndian.PutUint16(b[offset:], 65535)
		if _, err := parseMountPoint(b); err == nil {
			t.Fatalf("accepted malformed mountpoint range at %d", offset)
		}
	}
}
