package injector

import (
	"crypto/rand"
	"encoding/hex"
	"fmt"
	"os"
	"os/exec"
	"path/filepath"
	"strconv"
	"strings"
	"time"

	"github.com/proton-inject-go/embedded"
	"github.com/proton-inject-go/utils"
)

func randName(ext string) string {
	b := make([]byte, 8)
	_, _ = rand.Read(b)
	return hex.EncodeToString(b) + ext
}

type Manager struct{}

func New() (*Manager, error) {
	return &Manager{}, nil
}

// writes uniquely named binaries into cwd, waits for the game process, then runs protontricks-launch.
// when useLoader is false, dllPath must be a readable file on the host; only its basename is passed through.
func (m *Manager) Inject(appID, targetExe, dllPath string, useLoader bool) error {
	cwd, err := os.Getwd()
	if err != nil {
		return fmt.Errorf("failed to get current directory: %w", err)
	}

	injectorName := randName(".exe")
	localInjector := filepath.Join(cwd, injectorName)
	if err := os.WriteFile(localInjector, embedded.InjectorEXE, 0755); err != nil {
		return fmt.Errorf("failed to write embedded injector: %w", err)
	}
	defer os.Remove(localInjector)
	utils.Debug("wrote embedded injector as %s (%d bytes)", injectorName, len(embedded.InjectorEXE))

	var dllFilename string
	if useLoader {
		loaderName := randName(".dll")
		localLoader := filepath.Join(cwd, loaderName)
		if err := os.WriteFile(localLoader, embedded.LoaderDLL, 0644); err != nil {
			return fmt.Errorf("failed to write loader: %w", err)
		}
		defer os.Remove(localLoader)
		dllFilename = loaderName
		utils.Debug("using embedded loader as %s (%d bytes)", loaderName, len(embedded.LoaderDLL))
	} else {
		dllFilename = filepath.Base(dllPath)
		if dllFilename == "" || dllFilename == "." {
			return fmt.Errorf("invalid DLL filename")
		}
	}

	utils.Debug("waiting for game process to be ready...")
	if err := m.waitForProcess(targetExe, 30*time.Second); err != nil {
		return fmt.Errorf("game process not ready: %w", err)
	}
	utils.Debug("game process is ready")

	args := []string{
		"--no-bwrap",
		"--appid", appID,
		injectorName,
		"-n", targetExe,
		"-i",
		dllFilename,
	}

	utils.Debug("executing: protontricks-launch %s", strings.Join(args, " "))

	cmd := exec.Command("protontricks-launch", args...)
	cmd.Dir = cwd
	cmd.Stdout = os.Stdout
	cmd.Stderr = os.Stderr
	if err := cmd.Run(); err != nil {
		return fmt.Errorf("protontricks-launch failed: %w", err)
	}
	return nil
}

func (m *Manager) waitForProcess(processName string, timeout time.Duration) error {
	deadline := time.Now().Add(timeout)
	for time.Now().Before(deadline) {
		if m.isProcessRunning(processName) {
			return nil
		}
		time.Sleep(time.Second)
	}
	return fmt.Errorf("process %s not found within %v", processName, timeout)
}

func (m *Manager) isProcessRunning(processName string) bool {
	procDir := "/proc"
	entries, err := os.ReadDir(procDir)
	if err != nil {
		return false
	}
	for _, e := range entries {
		if !e.IsDir() {
			continue
		}
		if _, err := parsePID(e.Name()); err != nil {
			continue
		}
		cmdline, err := os.ReadFile(filepath.Join(procDir, e.Name(), "cmdline"))
		if err != nil {
			continue
		}
		// /proc/*/cmdline joins argv with NUL bytes; normalize before substring match
		if strings.Contains(strings.ReplaceAll(string(cmdline), "\x00", " "), processName) {
			return true
		}
	}
	return false
}

func parsePID(s string) (int, error) {
	n, err := strconv.ParseInt(s, 10, 32)
	if err != nil {
		return 0, err
	}
	return int(n), nil
}
