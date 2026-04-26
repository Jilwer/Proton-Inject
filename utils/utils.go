package utils

import (
	"fmt"
	"os"
	"path/filepath"
	"regexp"
	"strings"
	"unicode"
)

// optional sink for Debug output (e.g. GUI log pane).
var LogFunc func(string)

// replaces leading ~ or ~/ with the user's home directory.
func ExpandPath(path string) string {
	path = strings.TrimSpace(path)
	if path == "" {
		return path
	}
	if path == "~" {
		if home, err := os.UserHomeDir(); err == nil {
			return home
		}
		return path
	}
	if strings.HasPrefix(path, "~/") {
		if home, err := os.UserHomeDir(); err == nil {
			return filepath.Join(home, path[2:])
		}
	}
	return path
}

// merges library roots from every known libraryfolders.vdf path so compatdata on extra libraries is visible.
func steamLibraryRoots() []string {
	home, err := os.UserHomeDir()
	if err != nil {
		return nil
	}
	vdfPaths := []string{
		filepath.Join(home, ".steam", "steam", "libraryfolders.vdf"),
		filepath.Join(home, ".steam", "steam", "config", "libraryfolders.vdf"),
		filepath.Join(home, ".steam", "root", "steam", "libraryfolders.vdf"),
		filepath.Join(home, ".local", "share", "Steam", "steam", "libraryfolders.vdf"),
		filepath.Join(home, ".var", "app", "com.valvesoftware.Steam", ".steam", "steam", "libraryfolders.vdf"),
	}
	seen := make(map[string]bool)
	var out []string
	for _, vdfPath := range vdfPaths {
		for _, p := range parseLibraryFoldersVDF(vdfPath) {
			if !seen[p] {
				seen[p] = true
				out = append(out, p)
			}
		}
	}
	return out
}

// extracts quoted "path" entries; Steam may emit Windows-style paths, left unchanged for os.Stat.
var vdfPathRe = regexp.MustCompile(`"path"\s+"([^"]+)"`)

func parseLibraryFoldersVDF(path string) []string {
	data, err := os.ReadFile(path)
	if err != nil {
		return nil
	}
	matches := vdfPathRe.FindAllStringSubmatch(string(data), -1)
	if len(matches) == 0 {
		return nil
	}
	out := make([]string, 0, len(matches))
	seen := make(map[string]bool)
	for _, m := range matches {
		if len(m) < 2 {
			continue
		}
		p := strings.TrimSpace(m[1])
		if p != "" && !seen[p] {
			seen[p] = true
			out = append(out, p)
		}
	}
	return out
}

// one candidate Documents/proton-inject-mods path per library root; does not stat paths.
func modsDirCandidates(appID string) []string {
	appID = strings.TrimSpace(appID)
	if appID == "" {
		return nil
	}
	home, err := os.UserHomeDir()
	if err != nil {
		return nil
	}
	suffix := filepath.Join("pfx", "drive_c", "users", "steamuser", "Documents", "proton-inject-mods")

	var out []string
	seen := make(map[string]bool)
	add := func(compatRoot string) {
		modsPath := filepath.Join(compatRoot, suffix)
		if seen[modsPath] {
			return
		}
		seen[modsPath] = true
		out = append(out, modsPath)
	}

	if roots := steamLibraryRoots(); len(roots) > 0 {
		for _, libRoot := range roots {
			add(filepath.Join(libRoot, "steamapps", "compatdata", appID))
		}
	}
	add(filepath.Join(home, ".steam", "steam", "steamapps", "compatdata", appID))
	add(filepath.Join(home, ".local", "share", "Steam", "steamapps", "compatdata", appID))
	add(filepath.Join(home, ".var", "app", "com.valvesoftware.Steam", ".steam", "steam", "steamapps", "compatdata", appID))
	return out
}

// first existing proton-inject-mods directory for appID, or "" until the loader creates it.
func ModsDirForAppID(appID string) string {
	for _, modsPath := range modsDirCandidates(appID) {
		if _, err := os.Stat(modsPath); err == nil {
			return modsPath
		}
	}
	return ""
}

// writes a DEBUG line to stderr and forwards to LogFunc when set.
func Debug(format string, args ...interface{}) {
	msg := format
	if len(args) > 0 {
		msg = fmt.Sprintf(format, args...)
	}
	if len(msg) > 0 && unicode.IsLower(rune(msg[0])) {
		msg = strings.ToUpper(string(msg[0])) + msg[1:]
	}
	msg = "DEBUG: " + msg
	os.Stderr.WriteString(msg + "\n")
	if LogFunc != nil {
		LogFunc(msg)
	}
}
