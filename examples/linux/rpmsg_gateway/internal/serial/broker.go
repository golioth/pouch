package serial

import (
	"fmt"
	"log/slog"
)

// Node carries what the broker learns about the device across one session.
type Node struct {
	// ServerCertProvisioned is true once the device holds the same server
	// certificate we would push, so the SERVER_CERT step can be skipped.
	ServerCertProvisioned bool
	// DeviceCertProvisioned is true once the device reports that the cloud has
	// its certificate, so the DEVICE_CERT step can be skipped.
	DeviceCertProvisioned bool
	// SignedURLCapable is true once the device says it can be handed firmware
	// as a signed URL. Until it does we neither report the time nor poll for a
	// URL, so a device without those channels is never left waiting on one.
	SignedURLCapable bool
}

// Endpoints supplies the five channel implementations. Info, DeviceCert and
// Uplink are things the device sends us; ServerCert and Downlink are things we
// send it.
type Endpoints struct {
	Info       Receiver
	ServerCert Sender
	DeviceCert Receiver
	Downlink   Sender
	Uplink     Receiver

	// Fw and FwStatus carry MCU-mediated firmware updates. Both are optional:
	// leaving them nil leaves the channels unconfigured, which is what a
	// gateway that does not apply firmware wants.
	Fw       Receiver
	FwStatus Sender

	// Time and FwURL carry the signed-URL handoff: we report our clock, the
	// device hands back a URL for us to fetch. Also optional, and additionally
	// gated on the device advertising support.
	Time  Sender
	FwURL Receiver
}

// Broker drives one Pouch Serial session against a single device.
//
// It is a pure state machine: it never touches the link itself. Feed it frames
// with Recv and drain them with FrameGet. All methods must be called from one
// goroutine.
type Broker struct {
	channels [ChannelCount]channel
	node     Node
	log      *slog.Logger

	infoRead     bool
	timeDone     bool
	syncStarted  bool
	uplinkDone   bool
	downlinkDone bool
	fwDone       bool
	fwURLStarted bool
	fwURLDone    bool

	// fwStatusPending is set when a verdict is waiting to go out, and cleared
	// once it has been delivered. The session does not end while it is set.
	fwStatusPending bool

	// done reports the end of a session.
	done func(success bool)
	// wake signals that the broker has a frame ready to send.
	wake func()
}

// NewBroker wires the endpoints into a session. done is called once per
// session; wake is called whenever a frame becomes available.
func NewBroker(ep Endpoints, log *slog.Logger, done func(bool), wake func()) *Broker {
	b := &Broker{log: log, done: done, wake: wake}

	b.channels[ChInfo] = channel{id: ChInfo, receiver: ep.Info}
	b.channels[ChServerCert] = channel{id: ChServerCert, sender: ep.ServerCert}
	b.channels[ChDeviceCert] = channel{id: ChDeviceCert, receiver: ep.DeviceCert}
	b.channels[ChDownlink] = channel{id: ChDownlink, sender: ep.Downlink}
	b.channels[ChUplink] = channel{id: ChUplink, receiver: ep.Uplink}
	b.channels[ChFwStatus] = channel{id: ChFwStatus, sender: ep.FwStatus}
	b.channels[ChFw] = channel{id: ChFw, receiver: ep.Fw}
	b.channels[ChTime] = channel{id: ChTime, sender: ep.Time}
	b.channels[ChFwURL] = channel{id: ChFwURL, receiver: ep.FwURL}

	for i := range b.channels {
		b.channels[i].closed = b.channelClosed
	}
	return b
}

// Node exposes the provisioning state the INFO endpoint fills in.
func (b *Broker) Node() *Node { return &b.node }

// Start begins a session.
func (b *Broker) Start() {
	b.infoRead = false
	b.timeDone = b.channels[ChTime].sender == nil
	b.syncStarted = false
	b.uplinkDone = false
	b.downlinkDone = false
	b.fwDone = b.channels[ChFw].receiver == nil
	b.fwURLStarted = false
	b.fwURLDone = b.channels[ChFwURL].receiver == nil
	b.fwStatusPending = false
	b.next()
}

// next advances the verb sequence: read the device's info, provision whichever
// certificates are missing, then exchange pouches.
func (b *Broker) next() {
	switch {
	case !b.infoRead:
		b.infoRead = true
		b.channels[ChInfo].ready()

	// Before anything that depends on it. A device loaded by remoteproc has no
	// clock of its own, and it signs artifact URLs against a validity window,
	// so a URL signed before this arrives would be refused by the cloud.
	case !b.timeDone:
		b.timeDone = true
		if !b.node.SignedURLCapable {
			b.next()
			return
		}
		b.channels[ChTime].ready()

	case !b.node.ServerCertProvisioned:
		b.channels[ChServerCert].ready()

	case !b.node.DeviceCertProvisioned:
		b.channels[ChDeviceCert].ready()

	case !b.syncStarted:
		b.syncStarted = true
		// Both pouch directions are collected in the same phase, and so is the
		// firmware channel: the relay is fed by the very downlink it runs
		// alongside, so collecting it afterwards would deadlock a device whose
		// buffer fills mid-transfer.
		b.channels[ChUplink].ready()
		b.channels[ChDownlink].ready()
		if b.channels[ChFw].receiver != nil {
			b.channels[ChFw].ready()
		}

	// Collected after the downlink rather than alongside it: the manifest that
	// makes the device announce an artifact arrives on that downlink, so asking
	// first would usually find nothing and cost a session.
	case b.downlinkDone && !b.fwURLDone && !b.fwURLStarted:
		if !b.node.SignedURLCapable {
			b.fwURLDone = true
			b.next()
			return
		}
		b.fwURLStarted = true
		b.channels[ChFwURL].ready()

	// A verdict became available after the firmware transfer closed.
	case b.fwStatusPending && !b.channels[ChFwStatus].pending && !b.channels[ChFwStatus].open:
		b.channels[ChFwStatus].ready()

	case b.uplinkDone && b.downlinkDone && b.fwDone && b.fwURLDone && !b.fwStatusPending:
		if b.done != nil {
			b.done(true)
		}
		return

	default:
		return
	}

	if b.wake != nil {
		b.wake()
	}
}

func (b *Broker) channelClosed(ch Channel, success bool) {
	if !success {
		b.infoRead = false
		b.timeDone = false
		b.syncStarted = false
		b.uplinkDone = false
		b.downlinkDone = false
		b.fwDone = false
		b.fwURLStarted = false
		b.fwURLDone = false
		b.fwStatusPending = false
		if b.done != nil {
			b.done(false)
		}
		return
	}

	b.log.Debug("channel completed", "channel", ch)

	switch ch {
	case ChUplink:
		b.uplinkDone = true
	case ChDownlink:
		b.downlinkDone = true
	case ChFw:
		b.fwDone = true
	case ChFwURL:
		b.fwURLDone = true
	case ChFwStatus:
		b.fwStatusPending = false
	}

	b.next()
}

// Recv feeds one received frame into the session.
func (b *Broker) Recv(frame []byte) error {
	if len(frame) == 0 {
		return fmt.Errorf("serial: empty frame")
	}

	h, err := DecodeHeader(frame[0])
	if err != nil {
		return fmt.Errorf("serial: header 0x%02x (len %d): %w", frame[0], len(frame), err)
	}
	if h.Channel >= ChannelCount {
		return fmt.Errorf("serial: unknown channel %d", h.Channel)
	}

	return b.channels[h.Channel].recv(h, frame[HeaderLen:])
}

// FrameGet returns the next frame to transmit, or nil when there is nothing to
// send. Channels are served round-robin so no one channel can starve another.
func (b *Broker) FrameGet(maxLen int) []byte {
	for i := range b.channels {
		if f := b.channels[i].frameGet(maxLen); f != nil {
			return f
		}
	}
	return nil
}

// SendFwStatus asks the broker to deliver a firmware apply verdict before the
// session ends. Call it from the firmware channel's End, while the transfer
// that produced the image is closing.
func (b *Broker) SendFwStatus() {
	if b.channels[ChFwStatus].sender != nil {
		b.fwStatusPending = true
	}
}

// NotifyUplink re-arms the uplink channel, for a caller that has learned the
// device has more to say.
func (b *Broker) NotifyUplink() {
	b.channels[ChUplink].ready()
	if b.wake != nil {
		b.wake()
	}
}
