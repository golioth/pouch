// Command pouch-rpmsg-gateway brokers Pouch sessions between a Zephyr device on
// an MCU core and a Pouch-compatible cloud, over rpmsg.
//
// The MCU is the authenticated Pouch endpoint: it holds its own certificate and
// the session is encrypted end to end, so this process forwards ciphertext and
// never sees plaintext or a key.
package main

import (
	"context"
	"errors"
	"flag"
	"fmt"
	"log/slog"
	"os"
	"os/signal"
	"path/filepath"
	"syscall"
	"time"

	"github.com/golioth/pouch/examples/linux/rpmsg_gateway/internal/cloud"
	"github.com/golioth/pouch/examples/linux/rpmsg_gateway/internal/gateway"
	"github.com/golioth/pouch/examples/linux/rpmsg_gateway/internal/link"
)

func main() {
	var (
		dev      = flag.String("device", "/dev/rpmsg0", "rpmsg endpoint to attach to")
		server   = flag.String("cloud", "https://gw.golioth.io", "Pouch server base URL")
		interval = flag.Duration("interval", 30*time.Second, "delay between sync sessions")
		once     = flag.Bool("once", false, "run a single session and exit")
		timeout  = flag.Duration("timeout", 60*time.Second, "cloud request timeout")
		certPath = flag.String("cert", "", "gateway certificate for upstream mTLS (DER or PEM)")
		keyPath  = flag.String("key", "", "gateway private key for upstream mTLS (DER or PEM)")
		insecure = flag.Bool("insecure", false, "skip upstream TLS verification (testing only)")
		firmware = flag.Bool("firmware", false, "accept MCU-mediated firmware relay")
		fwDir    = flag.String("firmware-dir", "", "install verified images here (implies -firmware)")
		rproc    = flag.String("remoteproc", "",
			"remoteproc node to apply firmware through, e.g. /sys/class/remoteproc/remoteproc1")
		fwReject = flag.Bool("firmware-reject", false,
			"report a hash failure for images that verify, to exercise the device's retry path")
		verbose = flag.Bool("v", false, "debug logging")
	)
	flag.Parse()

	level := slog.LevelInfo
	if *verbose {
		level = slog.LevelDebug
	}
	log := slog.New(slog.NewTextHandler(os.Stderr, &slog.HandlerOptions{Level: level}))

	ctx, stop := signal.NotifyContext(context.Background(), os.Interrupt, syscall.SIGTERM)
	defer stop()

	l, err := link.Open(*dev)
	if err != nil {
		log.Error("open rpmsg endpoint", "err", err)
		os.Exit(1)
	}
	defer func() { _ = l.Close() }()

	// A session spends most of its life waiting for the device to say
	// something. Closing the link is what breaks that wait, so a signal has to
	// reach the descriptor - cancelling the context alone would leave the
	// process sitting in poll(2) until it was killed outright.
	closeOnCancel := func(l *link.RPMsg) {
		go func() {
			<-ctx.Done()
			_ = l.Close()
		}()
	}
	closeOnCancel(l)

	// The gateway authenticates upstream with its own certificate. It is a
	// separate identity from the device's: the device's key never leaves the
	// MCU, and the pouches passing through here stay encrypted end to end.
	var c *cloud.Client
	switch {
	case *certPath != "" && *keyPath != "":
		cert, err := cloud.LoadKeyPair(*certPath, *keyPath)
		if err != nil {
			log.Error("load gateway credentials", "err", err)
			os.Exit(1)
		}
		c = cloud.NewMTLS(*server, *timeout, cert, *insecure)
	case *certPath != "" || *keyPath != "":
		log.Error("-cert and -key must be given together")
		os.Exit(1)
	default:
		c = cloud.New(*server, *timeout)
	}

	gw := gateway.New(l, c, link.MaxFrame, log)
	var fwOpts *gateway.FirmwareOptions

	// applied is set when an image lands on disk. The core is restarted after
	// the session ends, not during it: the device is owed its apply verdict
	// first, and restarting mid-session would take the link down under it.
	var applied string

	if *firmware || *fwDir != "" || *fwReject || *rproc != "" {
		opts := &gateway.FirmwareOptions{ForceReject: *fwReject}
		if *fwDir != "" {
			dir := *fwDir
			opts.Apply = func(pkg, version string, image []byte) error {
				if err := os.MkdirAll(dir, 0o755); err != nil {
					return err
				}
				path := filepath.Join(dir, fmt.Sprintf("%s-%s.elf", pkg, version))
				if err := os.WriteFile(path, image, 0o644); err != nil {
					return err
				}
				applied = path
				log.Info("firmware installed", "path", path, "bytes", len(image))
				return nil
			}
		}
		gw.Firmware = opts
		fwOpts = opts
	}
	log.Info("gateway started", "device", *dev, "cloud", *server, "mtls", *certPath != "")

	for {
		start := time.Now()
		switch err := gw.RunSession(ctx); {
		case errors.Is(err, link.ErrClosed) || ctx.Err() != nil:
			log.Info("shutting down")
			return
		case err != nil:
			log.Error("session failed", "err", err, "elapsed", time.Since(start))
		default:
			log.Info("session complete", "elapsed", time.Since(start))
		}

		if applied != "" && *rproc != "" {
			if err := applyViaRemoteproc(log, *rproc, applied); err != nil {
				log.Error("apply through remoteproc", "err", err)
			}
			applied = ""
			// The core dropped its rpmsg endpoint on the way down and brings a
			// new one up on the way back, so the link has to be reopened.
			_ = l.Close()
			time.Sleep(3 * time.Second)
			nl, err := link.Open(*dev)
			if err != nil {
				log.Error("reopen rpmsg endpoint after restart", "err", err)
				return
			}
			l = nl
			closeOnCancel(l)
			gw = gateway.New(l, c, link.MaxFrame, log)
			gw.Firmware = fwOpts
			log.Info("reattached after firmware apply", "device", *dev)
		}

		if *once {
			return
		}
		select {
		case <-ctx.Done():
			return
		case <-time.After(*interval):
		}
	}
}

// applyViaRemoteproc installs an image as the core's firmware and restarts it.
// This is the "host-side apply" half of an MCU-mediated update: the core has no
// flash of its own, so applying means putting the file where remoteproc loads
// from and cycling the core.
func applyViaRemoteproc(log *slog.Logger, node, image string) error {
	name := filepath.Base(image)
	dst := filepath.Join("/lib/firmware", name)

	if dst != image {
		data, err := os.ReadFile(image)
		if err != nil {
			return err
		}
		if err := os.WriteFile(dst, data, 0o644); err != nil {
			return err
		}
	}

	if err := os.WriteFile(filepath.Join(node, "state"), []byte("stop"), 0o644); err != nil {
		return fmt.Errorf("stop: %w", err)
	}
	if err := os.WriteFile(filepath.Join(node, "firmware"), []byte(name), 0o644); err != nil {
		return fmt.Errorf("select firmware: %w", err)
	}
	if err := os.WriteFile(filepath.Join(node, "state"), []byte("start"), 0o644); err != nil {
		return fmt.Errorf("start: %w", err)
	}

	log.Info("core restarted on the new image", "firmware", name, "node", node)
	return nil
}
