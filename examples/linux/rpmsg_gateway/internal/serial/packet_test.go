package serial

import "testing"

// Wire vectors. The 0xb0 and 0xb2 cases are bytes captured from a real device
// (Zephyr on an i.MX95 M7) answering an INFO and a DEVICE_CERT prompt, so they
// pin this implementation against the C one rather than against itself.
func TestHeaderVectors(t *testing.T) {
	tests := []struct {
		name string
		byte byte
		hdr  Header
	}{
		{"ack ch0", 0x00, Header{Channel: ChInfo}},
		{"ack ch2", 0x02, Header{Channel: ChDeviceCert}},
		{"ack err ch4", 0x44, Header{Channel: ChUplink, Err: true}},
		{"data first+last ch0 (from hardware)", 0xb0,
			Header{IsData: true, First: true, Last: true, Channel: ChInfo}},
		{"data first+last ch2 (from hardware)", 0xb2,
			Header{IsData: true, First: true, Last: true, Channel: ChDeviceCert}},
		{"data first ch3", 0xa3,
			Header{IsData: true, First: true, Channel: ChDownlink}},
		{"data mid-transfer ch4", 0x84, Header{IsData: true, Channel: ChUplink}},
		{"data last ch1", 0x91, Header{IsData: true, Last: true, Channel: ChServerCert}},
	}

	for _, tt := range tests {
		t.Run(tt.name, func(t *testing.T) {
			if got := tt.hdr.Encode(); got != tt.byte {
				t.Errorf("Encode() = 0x%02x, want 0x%02x", got, tt.byte)
			}
			got, err := DecodeHeader(tt.byte)
			if err != nil {
				t.Fatalf("DecodeHeader(0x%02x) failed: %v", tt.byte, err)
			}
			if got != tt.hdr {
				t.Errorf("DecodeHeader(0x%02x) = %+v, want %+v", tt.byte, got, tt.hdr)
			}
		})
	}
}

// FIRST and LAST are reserved in an ACK and must be rejected, matching
// pouch_serial_header_decode().
func TestDecodeRejectsReservedAckBits(t *testing.T) {
	for _, b := range []byte{0x20, 0x10, 0x30} {
		if _, err := DecodeHeader(b); err == nil {
			t.Errorf("DecodeHeader(0x%02x) accepted a malformed ACK", b)
		}
	}
}

// An error on a data frame always terminates the transfer, whether or not the
// sender set LAST.
func TestDecodeErrImpliesLast(t *testing.T) {
	h, err := DecodeHeader(0xc0) // DATA | ERR, no LAST
	if err != nil {
		t.Fatalf("DecodeHeader failed: %v", err)
	}
	if !h.Err || !h.Last {
		t.Errorf("got %+v, want Err and Last both set", h)
	}
}

func TestHeaderRoundTrip(t *testing.T) {
	for b := 0; b < 256; b++ {
		h, err := DecodeHeader(byte(b))
		if err != nil {
			continue // malformed ACKs have no round trip
		}
		// An ERR data frame re-encodes with LAST, which decode already implied.
		if got := h.Encode(); got != byte(b) && !(h.IsData && h.Err) {
			t.Errorf("round trip of 0x%02x produced 0x%02x", b, got)
		}
	}
}
