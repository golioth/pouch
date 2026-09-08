package serial

import (
	"bytes"
	"io"
	"log/slog"
	"testing"
)

// The device half, built from the same channel state machine as the broker.
// Mirroring tests/pouch/serial/exchange in the Pouch tree: driving both ends
// through each other is what proves the two agree on the wire, since a
// disagreement stops the exchange converging.
type device struct {
	channels [ChannelCount]channel
}

func newDevice(info, deviceCert, uplink []byte, serverCert, downlink *bytes.Buffer) *device {
	d := &device{}
	d.channels[ChInfo] = channel{id: ChInfo, sender: &BufSender{Data: func() []byte { return info }}}
	d.channels[ChServerCert] = channel{id: ChServerCert, receiver: &collector{buf: serverCert}}
	d.channels[ChDeviceCert] = channel{id: ChDeviceCert, sender: &BufSender{Data: func() []byte { return deviceCert }}}
	d.channels[ChDownlink] = channel{id: ChDownlink, receiver: &collector{buf: downlink}}
	d.channels[ChUplink] = channel{id: ChUplink, sender: &BufSender{Data: func() []byte { return uplink }}}
	return d
}

func (d *device) recv(frame []byte) error {
	h, err := DecodeHeader(frame[0])
	if err != nil {
		return err
	}
	return d.channels[h.Channel].recv(h, frame[HeaderLen:])
}

func (d *device) frameGet(max int) []byte {
	for i := range d.channels {
		if f := d.channels[i].frameGet(max); f != nil {
			return f
		}
	}
	return nil
}

type collector struct{ buf *bytes.Buffer }

func (c *collector) Start() error        { c.buf.Reset(); return nil }
func (c *collector) Recv(p []byte) error { c.buf.Write(p); return nil }
func (c *collector) End(success bool)    {}

func discardLogger() *slog.Logger {
	return slog.New(slog.NewTextHandler(io.Discard, nil))
}

// pump runs frames between the two halves until neither has anything to say.
func pump(t *testing.T, b *Broker, d *device, maxFrame int) int {
	t.Helper()
	const maxIter = 500
	iter := 0
	for ; iter < maxIter; iter++ {
		if f := b.FrameGet(maxFrame); f != nil {
			if err := d.recv(f); err != nil {
				t.Fatalf("device rejected frame 0x%02x: %v", f[0], err)
			}
			continue
		}
		if f := d.frameGet(maxFrame); f != nil {
			if err := b.Recv(f); err != nil {
				t.Fatalf("broker rejected frame 0x%02x: %v", f[0], err)
			}
			continue
		}
		break
	}
	if iter == maxIter {
		t.Fatalf("exchange did not converge in %d iterations", maxIter)
	}
	return iter
}

func TestFullExchange(t *testing.T) {
	var (
		infoPayload       = []byte("device-info-stub")
		deviceCertPayload = []byte("device-certificate-data")
		uplinkPayload     = []byte("uplink-payload")
		serverCertPayload = []byte("server-certificate-data")
		downlinkPayload   = []byte("downlink-payload")
	)

	var gotServerCert, gotDownlink bytes.Buffer
	dev := newDevice(infoPayload, deviceCertPayload, uplinkPayload, &gotServerCert, &gotDownlink)

	var gotInfo, gotDeviceCert, gotUplink []byte
	var sessionOK, finished bool

	var b *Broker
	ep := Endpoints{
		Info: &BufReceiver{Done: func(d []byte, ok bool) {
			gotInfo = append([]byte(nil), d...)
			// The device in this test is unprovisioned, so both certificate
			// phases must run.
			b.Node().ServerCertProvisioned = false
			b.Node().DeviceCertProvisioned = false
		}},
		ServerCert: &BufSender{Data: func() []byte { return serverCertPayload },
			Done: func(ok bool) { b.Node().ServerCertProvisioned = ok }},
		DeviceCert: &BufReceiver{Done: func(d []byte, ok bool) {
			gotDeviceCert = append([]byte(nil), d...)
			b.Node().DeviceCertProvisioned = ok
		}},
		Downlink: &BufSender{Data: func() []byte { return downlinkPayload }},
		Uplink: &BufReceiver{Done: func(d []byte, ok bool) {
			gotUplink = append([]byte(nil), d...)
		}},
	}

	b = NewBroker(ep, discardLogger(), func(ok bool) { finished, sessionOK = true, ok }, func() {})
	b.Start()

	pump(t, b, dev, 64)

	if !finished || !sessionOK {
		t.Fatalf("session did not complete successfully (finished=%v ok=%v)", finished, sessionOK)
	}

	// Device -> broker.
	if !bytes.Equal(gotInfo, infoPayload) {
		t.Errorf("info = %q, want %q", gotInfo, infoPayload)
	}
	if !bytes.Equal(gotDeviceCert, deviceCertPayload) {
		t.Errorf("device cert = %q, want %q", gotDeviceCert, deviceCertPayload)
	}
	if !bytes.Equal(gotUplink, uplinkPayload) {
		t.Errorf("uplink = %q, want %q", gotUplink, uplinkPayload)
	}

	// Broker -> device.
	if !bytes.Equal(gotServerCert.Bytes(), serverCertPayload) {
		t.Errorf("server cert = %q, want %q", gotServerCert.Bytes(), serverCertPayload)
	}
	if !bytes.Equal(gotDownlink.Bytes(), downlinkPayload) {
		t.Errorf("downlink = %q, want %q", gotDownlink.Bytes(), downlinkPayload)
	}
}

// A payload larger than one frame must be fragmented and reassembled.
func TestExchangeFragments(t *testing.T) {
	big := bytes.Repeat([]byte("0123456789abcdef"), 40) // 640 bytes

	var gotServerCert, gotDownlink bytes.Buffer
	dev := newDevice([]byte("i"), []byte("c"), big, &gotServerCert, &gotDownlink)

	var gotUplink []byte
	var finished, ok bool
	var b *Broker
	b = NewBroker(Endpoints{
		Info:       &BufReceiver{},
		ServerCert: &BufSender{Data: func() []byte { return []byte("sc") }, Done: func(ok bool) { b.Node().ServerCertProvisioned = ok }},
		DeviceCert: &BufReceiver{Done: func(_ []byte, o bool) { b.Node().DeviceCertProvisioned = o }},
		Downlink:   &BufSender{Data: func() []byte { return big }},
		Uplink:     &BufReceiver{Done: func(d []byte, _ bool) { gotUplink = append([]byte(nil), d...) }},
	}, discardLogger(), func(o bool) { finished, ok = true, o }, func() {})
	b.Start()

	pump(t, b, dev, 32) // 31 payload bytes per frame forces fragmentation

	if !finished || !ok {
		t.Fatalf("session did not complete (finished=%v ok=%v)", finished, ok)
	}
	if !bytes.Equal(gotUplink, big) {
		t.Errorf("uplink round trip failed: got %d bytes, want %d", len(gotUplink), len(big))
	}
	if !bytes.Equal(gotDownlink.Bytes(), big) {
		t.Errorf("downlink round trip failed: got %d bytes, want %d", gotDownlink.Len(), len(big))
	}
}

// A provisioned device must skip both certificate phases.
func TestProvisionedDeviceSkipsCertPhases(t *testing.T) {
	var gotServerCert, gotDownlink bytes.Buffer
	dev := newDevice([]byte("i"), []byte("c"), []byte("u"), &gotServerCert, &gotDownlink)

	serverCertSent := false
	deviceCertRead := false
	var finished bool
	var b *Broker
	b = NewBroker(Endpoints{
		Info: &BufReceiver{Done: func(_ []byte, _ bool) {
			b.Node().ServerCertProvisioned = true
			b.Node().DeviceCertProvisioned = true
		}},
		ServerCert: &BufSender{Data: func() []byte { serverCertSent = true; return []byte("sc") }},
		DeviceCert: &BufReceiver{Done: func(_ []byte, _ bool) { deviceCertRead = true }},
		Downlink:   &BufSender{Data: func() []byte { return []byte("d") }},
		Uplink:     &BufReceiver{},
	}, discardLogger(), func(bool) { finished = true }, func() {})
	b.Start()

	pump(t, b, dev, 64)

	if !finished {
		t.Fatal("session did not complete")
	}
	if serverCertSent {
		t.Error("server certificate was pushed to an already-provisioned device")
	}
	if deviceCertRead {
		t.Error("device certificate was collected from an already-provisioned device")
	}
	if gotServerCert.Len() != 0 {
		t.Errorf("device received %d server-cert bytes, want none", gotServerCert.Len())
	}
}
