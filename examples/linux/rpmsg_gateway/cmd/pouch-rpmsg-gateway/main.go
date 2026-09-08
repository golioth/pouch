// Command pouch-rpmsg-gateway brokers Pouch sessions between a Zephyr device on
// an MCU core and a Pouch-compatible cloud, over rpmsg.
//
// The MCU is the authenticated Pouch endpoint: it holds its own certificate and
// the session is encrypted end to end, so this process forwards ciphertext and
// never sees plaintext or a key.
package main

import (
	"context"
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

	if *firmware || *fwDir != "" || *fwReject {
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
				log.Info("firmware installed", "path", path, "bytes", len(image))
				return nil
			}
		}
		gw.Firmware = opts
	}
	log.Info("gateway started", "device", *dev, "cloud", *server, "mtls", *certPath != "")

	for {
		start := time.Now()
		if err := gw.RunSession(ctx); err != nil {
			log.Error("session failed", "err", err, "elapsed", time.Since(start))
		} else {
			log.Info("session complete", "elapsed", time.Since(start))
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
