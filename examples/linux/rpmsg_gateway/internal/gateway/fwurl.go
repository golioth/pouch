package gateway

import (
	"encoding/binary"
	"fmt"
	"net/url"
	"time"
)

// The signed-URL record, as src/transport/endpoints/device/fw_url.c writes it.
//
//	magic    u32le "PFU1"
//	size     u32le  artifact size
//	sha256   [32]   digest of the artifact, from the OTA manifest
//	ver_len  u8
//	pkg_len  u8
//	url_len  u16le
//	version  [ver_len]
//	package  [pkg_len]
//	url      [url_len]
//
// Unlike the relay this is never chunked: the record is small and fixed, and
// the device holds it until we collect the whole thing.
const (
	fwURLHdrFixedLen = 44
	fwURLMagic       = 0x31554650 // "PFU1" little endian

	// The device's own limit is a Kconfig option, typically 1024. This is the
	// ceiling we will accept from any device, whatever it was built with: a
	// signed URL runs to roughly 750 bytes, so anything approaching this is
	// already wrong.
	fwURLMaxLen = 4096
)

// The time record we push, matching POUCH_SERIAL_TIME_* in fw_url.h.
const (
	timeRecordLen = 12
	timeMagic     = 0x314D5450 // "PTM1" little endian
)

// fwURLRecord is a device's request that we fetch an artifact on its behalf.
type fwURLRecord struct {
	pkg     string
	version string
	size    uint32
	sha256  [32]byte
	url     string
}

func parseFwURLRecord(b []byte) (*fwURLRecord, error) {
	if len(b) < fwURLHdrFixedLen {
		return nil, fmt.Errorf("signed-URL record of %d bytes is shorter than its header", len(b))
	}
	if magic := binary.LittleEndian.Uint32(b[0:4]); magic != fwURLMagic {
		return nil, fmt.Errorf("bad signed-URL record magic 0x%08x", magic)
	}

	r := &fwURLRecord{size: binary.LittleEndian.Uint32(b[4:8])}
	copy(r.sha256[:], b[8:40])

	verLen, pkgLen := int(b[40]), int(b[41])
	urlLen := int(binary.LittleEndian.Uint16(b[42:44]))

	if r.size == 0 {
		return nil, fmt.Errorf("signed-URL record describes an empty artifact")
	}
	if urlLen == 0 || urlLen > fwURLMaxLen {
		return nil, fmt.Errorf("signed-URL record carries a %d byte URL", urlLen)
	}

	end := fwURLHdrFixedLen + verLen + pkgLen + urlLen
	if len(b) < end {
		return nil, fmt.Errorf("signed-URL record truncated in its variable fields")
	}

	off := fwURLHdrFixedLen
	r.version = string(b[off : off+verLen])
	off += verLen
	r.pkg = string(b[off : off+pkgLen])
	off += pkgLen
	r.url = string(b[off : off+urlLen])

	if r.pkg == "" || r.version == "" {
		return nil, fmt.Errorf("signed-URL record names no package or version")
	}

	// The device chooses this URL and we fetch it, so it is checked rather than
	// trusted. Plain HTTP would put the artifact - and the signature that
	// authorizes it - on the wire in clear.
	u, err := url.Parse(r.url)
	if err != nil {
		return nil, fmt.Errorf("signed-URL record carries an unparseable URL: %w", err)
	}
	if u.Scheme != "https" {
		return nil, fmt.Errorf("signed-URL record carries a %q URL, want https", u.Scheme)
	}
	if u.Host == "" {
		return nil, fmt.Errorf("signed-URL record carries a URL with no host")
	}

	return r, nil
}

// timeRecord encodes the wall clock for a device that has none of its own.
func timeRecord(now time.Time) []byte {
	b := make([]byte, timeRecordLen)
	binary.LittleEndian.PutUint32(b[0:4], timeMagic)
	binary.LittleEndian.PutUint64(b[4:12], uint64(now.Unix()))
	return b
}
