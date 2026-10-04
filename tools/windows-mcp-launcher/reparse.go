package main

import (
	"encoding/binary"
	"errors"
	"unicode/utf16"
)

const symlinkTag = 0xa000000c
const mountPointTag = 0xa0000003

type linkTarget struct {
	name     string
	relative bool
}

func parseSymlink(data []byte) (linkTarget, error) {
	fail := func() (linkTarget, error) { return linkTarget{}, errors.New("invalid_symlink_reparse_data") }
	if len(data) < 20 || binary.LittleEndian.Uint32(data) != symlinkTag {
		return fail()
	}
	size := int(binary.LittleEndian.Uint16(data[4:]))
	if size < 12 || size+8 > len(data) {
		return fail()
	}
	flags := binary.LittleEndian.Uint32(data[16:])
	if flags > 1 {
		return fail()
	}
	path := data[20 : size+8]
	decode := func(offset, length int) (string, bool) {
		if offset%2 != 0 || length%2 != 0 || offset > len(path) || length > len(path)-offset {
			return "", false
		}
		u := make([]uint16, length/2)
		for i := range u {
			u[i] = binary.LittleEndian.Uint16(path[offset+2*i:])
		}
		for i := 0; i < len(u); i++ {
			if u[i] == 0 {
				return "", false
			}
			if u[i] >= 0xd800 && u[i] <= 0xdbff {
				if i+1 == len(u) || u[i+1] < 0xdc00 || u[i+1] > 0xdfff {
					return "", false
				}
				i++
			} else if u[i] >= 0xdc00 && u[i] <= 0xdfff {
				return "", false
			}
		}
		return string(utf16.Decode(u)), true
	}
	name, ok := decode(int(binary.LittleEndian.Uint16(data[8:])), int(binary.LittleEndian.Uint16(data[10:])))
	if !ok || name == "" {
		return fail()
	}
	if _, ok = decode(int(binary.LittleEndian.Uint16(data[12:])), int(binary.LittleEndian.Uint16(data[14:]))); !ok {
		return fail()
	}
	return linkTarget{name, flags == 1}, nil
}

func parseMountPoint(data []byte) (linkTarget, error) {
	if len(data) < 16 || binary.LittleEndian.Uint32(data) != mountPointTag {
		return linkTarget{}, errors.New("invalid_mount_point_data")
	}
	size := int(binary.LittleEndian.Uint16(data[4:]))
	if size < 8 || size > 65531 || size+8 > len(data) {
		return linkTarget{}, errors.New("invalid_mount_point_data")
	}
	// Mount points use the same Unicode ranges as symlinks, without Flags.
	// Validate with the shared parser by inserting the missing four bytes.
	symlink := make([]byte, size+12)
	copy(symlink[:16], data[:16])
	binary.LittleEndian.PutUint32(symlink, symlinkTag)
	binary.LittleEndian.PutUint16(symlink[4:], uint16(size+4))
	copy(symlink[20:], data[16:size+8])
	return parseSymlink(symlink)
}
