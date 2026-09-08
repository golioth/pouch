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
	syncStarted  bool
	uplinkDone   bool
	downlinkDone bool

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
	b.syncStarted = false
	b.uplinkDone = false
	b.downlinkDone = false
	b.next()
}

// next advances the verb sequence: read the device's info, provision whichever
// certificates are missing, then exchange pouches.
func (b *Broker) next() {
	switch {
	case !b.infoRead:
		b.infoRead = true
		b.channels[ChInfo].ready()

	case !b.node.ServerCertProvisioned:
		b.channels[ChServerCert].ready()

	case !b.node.DeviceCertProvisioned:
		b.channels[ChDeviceCert].ready()

	case !b.syncStarted:
		b.syncStarted = true
		// Both directions are collected in the same phase.
		b.channels[ChUplink].ready()
		b.channels[ChDownlink].ready()

	case b.uplinkDone && b.downlinkDone:
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
		b.syncStarted = false
		b.uplinkDone = false
		b.downlinkDone = false
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

// NotifyUplink re-arms the uplink channel, for a caller that has learned the
// device has more to say.
func (b *Broker) NotifyUplink() {
	b.channels[ChUplink].ready()
	if b.wake != nil {
		b.wake()
	}
}
