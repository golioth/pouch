package gateway

import (
	"crypto/sha256"
	"encoding/binary"
	"fmt"
)

// The firmware chunk header, as src/transport/endpoints/device/fw.c writes it.
//
//	magic    u32le "PFW2"
//	size     u32le  total image size
//	offset   u32le  absolute offset this chunk starts at
//	sha256   [32]   digest of the whole image
//	ver_len  u8
//	pkg_len  u8
//	version  [ver_len]
//	package  [pkg_len]
//
// A relay is chunked because the image arrives from the cloud over several
// sessions: each chunk carries whatever was buffered at that moment, stamped
// with where it belongs.
const (
	fwHdrFixedLen = 46
	fwMagic       = 0x32574650 // "PFW2" little endian
)

// FwStatus is the verdict the host reports after trying to apply an image.
type FwStatus uint8

const (
	FwStatusOK       FwStatus = 0 // verified and installed
	FwStatusHashFail FwStatus = 1 // SHA-256 mismatch, nothing installed
	FwStatusError    FwStatus = 2 // install or I/O error
)

func (s FwStatus) String() string {
	switch s {
	case FwStatusOK:
		return "ok"
	case FwStatusHashFail:
		return "hash-fail"
	case FwStatusError:
		return "error"
	}
	return fmt.Sprintf("unknown(%d)", uint8(s))
}

type fwChunk struct {
	size    uint32
	offset  uint32
	sha256  [32]byte
	version string
	pkg     string
	data    []byte
}

func parseFwChunk(b []byte) (*fwChunk, error) {
	if len(b) < fwHdrFixedLen {
		return nil, fmt.Errorf("firmware chunk of %d bytes is shorter than its header", len(b))
	}
	if magic := binary.LittleEndian.Uint32(b[0:4]); magic != fwMagic {
		return nil, fmt.Errorf("bad firmware chunk magic 0x%08x", magic)
	}

	c := &fwChunk{
		size:   binary.LittleEndian.Uint32(b[4:8]),
		offset: binary.LittleEndian.Uint32(b[8:12]),
	}
	copy(c.sha256[:], b[12:44])

	verLen, pkgLen := int(b[44]), int(b[45])
	end := fwHdrFixedLen + verLen + pkgLen
	if len(b) < end {
		return nil, fmt.Errorf("firmware chunk truncated in its name fields")
	}
	c.version = string(b[fwHdrFixedLen : fwHdrFixedLen+verLen])
	c.pkg = string(b[fwHdrFixedLen+verLen : end])
	c.data = b[end:]
	return c, nil
}

// fwRelay assembles an image from the chunks the device streams across
// sessions, and reports what the host made of it.
type fwRelay struct {
	pkg     string
	version string
	size    uint32
	digest  [32]byte
	image   []byte
}

// push adds a chunk. It returns true once the image is complete.
func (r *fwRelay) push(c *fwChunk) (bool, error) {
	if r.size == 0 || r.version != c.version {
		// A new image: either the first one, or the device moved on to another.
		*r = fwRelay{pkg: c.pkg, version: c.version, size: c.size, digest: c.sha256}
	}
	if c.offset != uint32(len(r.image)) {
		return false, fmt.Errorf("firmware chunk at offset %d, expected %d",
			c.offset, len(r.image))
	}
	r.image = append(r.image, c.data...)
	return uint32(len(r.image)) >= r.size, nil
}

// verdict checks the assembled image against the digest from the manifest.
func (r *fwRelay) verdict() FwStatus {
	if uint32(len(r.image)) != r.size {
		return FwStatusError
	}
	if sha256.Sum256(r.image) != r.digest {
		return FwStatusHashFail
	}
	return FwStatusOK
}

func (r *fwRelay) reset() { *r = fwRelay{} }
