package gateway

import (
	"context"
	"encoding/binary"
	"io"
	"log/slog"
	"testing"
	"time"
)

// fakeCloud supplies a fixed clock offset and nothing else.
type fakeCloud struct {
	offset time.Duration
	known  bool
}

func (f *fakeCloud) ServerCert(context.Context) ([]byte, error) { return nil, nil }
func (f *fakeCloud) RegisterDevice(context.Context, []byte) error {
	return nil
}
func (f *fakeCloud) Forward(context.Context, []byte) ([]byte, error) { return nil, nil }
func (f *fakeCloud) ClockOffset() (time.Duration, bool)              { return f.offset, f.known }

func gatewayWithOffset(offset time.Duration, known bool) *Gateway {
	log := slog.New(slog.NewTextHandler(io.Discard, nil))
	return New(context.Background(), nil, &fakeCloud{offset: offset, known: known}, 496, log)
}

// The point of the whole mechanism: a host running fast must not produce a
// stamp the server reads as being in the future, because the server refuses
// those outright and the device then silently falls back to relaying.
func TestReportedClockCorrectsAFastHost(t *testing.T) {
	// Host twenty seconds ahead of the server, which is what the bench board
	// was doing when every signed URL came back 401.
	g := gatewayWithOffset(-20*time.Second, true)

	reported := time.Unix(int64(binary.LittleEndian.Uint64(timeRecord(g.serverNow())[4:12])), 0)
	serverNow := time.Now().Add(-20 * time.Second)

	if !reported.Before(serverNow) {
		t.Errorf("reported %v is not behind the server's %v; the stamp would be refused",
			reported, serverNow)
	}
	// Corrected, not merely backdated: without the offset the report would sit
	// a quarter minute ahead of the server rather than just behind it.
	if serverNow.Sub(reported) > 30*time.Second {
		t.Errorf("reported %v is needlessly far behind the server's %v", reported, serverNow)
	}
}

func TestReportedClockCorrectsASlowHost(t *testing.T) {
	// A slow host is harmless for validity, but correcting it keeps the far end
	// of the window where the device expects it.
	g := gatewayWithOffset(30*time.Second, true)

	reported := time.Unix(int64(binary.LittleEndian.Uint64(timeRecord(g.serverNow())[4:12])), 0)
	serverNow := time.Now().Add(30 * time.Second)

	if d := serverNow.Sub(reported); d < 0 || d > timeSkewMargin+2*time.Second {
		t.Errorf("reported %v sits %v from the server's %v, want just under the margin",
			reported, d, serverNow)
	}
}

func TestReportedClockFallsBackToLocal(t *testing.T) {
	// Nothing observed yet, on the very first session: the local clock is all
	// there is, and the margin is the only protection.
	g := gatewayWithOffset(0, false)

	reported := time.Unix(int64(binary.LittleEndian.Uint64(timeRecord(g.serverNow())[4:12])), 0)

	if d := time.Since(reported); d < timeSkewMargin || d > timeSkewMargin+2*time.Second {
		t.Errorf("reported clock is %v behind local, want about %v", d, timeSkewMargin)
	}
}
