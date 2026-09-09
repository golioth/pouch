package gateway

import (
	"encoding/binary"
	"strings"
	"testing"
	"time"
)

// buildRecord assembles a signed-URL record the way the device does, so the
// parser is tested against the layout rather than against itself.
func buildRecord(pkg, version, url string, size uint32, digest [32]byte) []byte {
	b := make([]byte, fwURLHdrFixedLen)
	binary.LittleEndian.PutUint32(b[0:4], fwURLMagic)
	binary.LittleEndian.PutUint32(b[4:8], size)
	copy(b[8:40], digest[:])
	b[40] = byte(len(version))
	b[41] = byte(len(pkg))
	binary.LittleEndian.PutUint16(b[42:44], uint16(len(url)))
	b = append(b, version...)
	b = append(b, pkg...)
	b = append(b, url...)
	return b
}

func testDigest() [32]byte {
	var d [32]byte
	for i := range d {
		d[i] = byte(i)
	}
	return d
}

func TestParseFwURLRecord(t *testing.T) {
	digest := testDigest()
	url := "https://gw.golioth.io/.u/c/rpmsg_device@1.0.3?nb=1&na=2&cert=x&sig=y"
	raw := buildRecord("rpmsg_device", "1.0.3", url, 250948, digest)

	rec, err := parseFwURLRecord(raw)
	if err != nil {
		t.Fatalf("parse: %v", err)
	}
	if rec.pkg != "rpmsg_device" {
		t.Errorf("package %q", rec.pkg)
	}
	if rec.version != "1.0.3" {
		t.Errorf("version %q", rec.version)
	}
	if rec.size != 250948 {
		t.Errorf("size %d", rec.size)
	}
	if rec.sha256 != digest {
		t.Errorf("digest mismatch")
	}
	if rec.url != url {
		t.Errorf("url %q", rec.url)
	}
}

func TestParseFwURLRecordRejects(t *testing.T) {
	digest := testDigest()
	good := buildRecord("pkg", "1.0.0", "https://example.test/a", 100, digest)

	badMagic := append([]byte(nil), good...)
	binary.LittleEndian.PutUint32(badMagic[0:4], 0xdeadbeef)

	zeroSize := buildRecord("pkg", "1.0.0", "https://example.test/a", 0, digest)

	// A URL longer than we will accept from any device, whatever it was built
	// with. The length field says so; the bytes need not be present.
	hugeURL := append([]byte(nil), good...)
	binary.LittleEndian.PutUint16(hugeURL[42:44], uint16(fwURLMaxLen+1))

	zeroURL := append([]byte(nil), good...)
	binary.LittleEndian.PutUint16(zeroURL[42:44], 0)

	tests := []struct {
		name string
		raw  []byte
	}{
		{"empty", nil},
		{"short header", good[:20]},
		{"bad magic", badMagic},
		{"zero size", zeroSize},
		{"truncated fields", good[:len(good)-3]},
		{"url too long", hugeURL},
		{"url absent", zeroURL},
		{"plain http", buildRecord("pkg", "1.0.0", "http://example.test/a", 100, digest)},
		{"not a url", buildRecord("pkg", "1.0.0", "://///", 100, digest)},
		{"no host", buildRecord("pkg", "1.0.0", "https:///a", 100, digest)},
		{"no package", buildRecord("", "1.0.0", "https://example.test/a", 100, digest)},
		{"no version", buildRecord("pkg", "", "https://example.test/a", 100, digest)},
	}

	for _, tc := range tests {
		t.Run(tc.name, func(t *testing.T) {
			if _, err := parseFwURLRecord(tc.raw); err == nil {
				t.Fatal("accepted a record it should have refused")
			}
		})
	}
}

func TestParseFwURLRecordRefusesPlainHTTP(t *testing.T) {
	// Called out separately because it is the one rejection that is about
	// safety rather than framing: the artifact and the signature that
	// authorizes it would both travel in clear.
	raw := buildRecord("pkg", "1.0.0", "http://gw.golioth.io/.u/c/pkg@1.0.0", 100, testDigest())

	_, err := parseFwURLRecord(raw)
	if err == nil {
		t.Fatal("accepted a plain-HTTP artifact URL")
	}
	if !strings.Contains(err.Error(), "https") {
		t.Errorf("error %q does not explain the scheme requirement", err)
	}
}

func TestTimeRecord(t *testing.T) {
	now := time.Unix(1757000000, 0)
	b := timeRecord(now)

	if len(b) != timeRecordLen {
		t.Fatalf("time record is %d bytes, want %d", len(b), timeRecordLen)
	}
	if got := binary.LittleEndian.Uint32(b[0:4]); got != timeMagic {
		t.Errorf("magic 0x%08x, want 0x%08x", got, timeMagic)
	}

	// Backdated, never ahead: the device stamps "not before now" onto a signed
	// URL, and the server refuses a not-before in the future outright.
	want := uint64(now.Add(-timeSkewMargin).Unix())
	if got := binary.LittleEndian.Uint64(b[4:12]); got != want {
		t.Errorf("seconds %d, want %d", got, want)
	}
	if got := binary.LittleEndian.Uint64(b[4:12]); got >= uint64(now.Unix()) {
		t.Errorf("reported clock %d is not behind the real time %d", got, now.Unix())
	}
}
