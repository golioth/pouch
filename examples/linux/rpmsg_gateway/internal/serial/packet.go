// Package serial implements the broker half of the Pouch Serial protocol.
//
// The wire format is a one-byte header followed by an optional payload. Frame
// boundaries come from the link, not from the header: over rpmsg one message is
// exactly one frame, so there is no length prefix and no start-of-frame byte.
//
// This mirrors src/transport/serial/ in the Pouch tree. The device half runs on
// the MCU; this is what the Linux side needs to talk to it.
package serial

import "errors"

// Channel identifiers. These are wire constants: every build agrees on them
// whether or not it implements the channel.
type Channel uint8

const (
	ChInfo       Channel = 0 // device -> broker: device metadata and flags
	ChServerCert Channel = 1 // broker -> device: server certificate chain
	ChDeviceCert Channel = 2 // device -> broker: device leaf certificate
	ChDownlink   Channel = 3 // broker -> device: inbound pouches
	ChUplink     Channel = 4 // device -> broker: outbound pouches

	ChannelCount Channel = 5
)

// HeaderLen is the size of the frame header in bytes.
const HeaderLen = 1

const (
	hdrData   = 1 << 7
	hdrErr    = 1 << 6
	hdrFirst  = 1 << 5
	hdrLast   = 1 << 4
	hdrChMask = 0x0f

	// FIRST and LAST are reserved in an ACK frame and must be zero.
	hdrAckReserved = hdrFirst | hdrLast
)

// ErrMalformedHeader is returned for an ACK frame with reserved bits set.
var ErrMalformedHeader = errors.New("serial: malformed frame header")

// Header is the decoded one-byte frame header.
//
//	ACK frame  (bit 7 = 0): [ 0 | ERR |  0   |  0   | channel[3:0] ]
//	Data frame (bit 7 = 1): [ 1 | ERR | FIRST| LAST | channel[3:0] ]
type Header struct {
	IsData bool
	// Err marks a transfer abandoned by the sender (data frames, always with
	// Last) or by the receiver (ACK frames).
	Err     bool
	First   bool
	Last    bool
	Channel Channel
}

// Encode renders the header as its wire byte.
func (h Header) Encode() byte {
	b := byte(h.Channel) & hdrChMask
	if h.IsData {
		b |= hdrData
		if h.First {
			b |= hdrFirst
		}
		if h.Last {
			b |= hdrLast
		}
	}
	if h.Err {
		b |= hdrErr
	}
	return b
}

// DecodeHeader parses a wire header byte.
func DecodeHeader(b byte) (Header, error) {
	h := Header{
		IsData:  b&hdrData != 0,
		Err:     b&hdrErr != 0,
		Channel: Channel(b & hdrChMask),
	}

	if h.IsData {
		h.First = b&hdrFirst != 0
		h.Last = b&hdrLast != 0
		// An error always terminates the transfer, whether or not the sender
		// bothered to set LAST.
		if h.Err {
			h.Last = true
		}
		return h, nil
	}

	if b&hdrAckReserved != 0 {
		return Header{}, ErrMalformedHeader
	}
	return h, nil
}
