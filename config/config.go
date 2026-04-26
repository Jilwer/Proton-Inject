package config

import (
	"encoding/json"
	"fmt"
	"os"
	"path/filepath"
	"sort"
	"strings"
)

// persisted injection fields; CLI uses --dll vs loader, GUI also saves UseLoader per profile.
type Config struct {
	AppID     *string `json:"appid,omitempty"`
	TargetExe *string `json:"exe,omitempty"`
	DLLPath   *string `json:"dll_path,omitempty"`
	UseLoader *bool   `json:"use_loader,omitempty"`
}

// true when unset so empty configs and older profiles still prefer the embedded loader.
func (c *Config) UseLoaderOrDefault() bool {
	if c == nil || c.UseLoader == nil {
		return true
	}
	return *c.UseLoader
}

// reads and writes XDG or ~/.config proton-inject paths.
type ProfileManager struct {
	configDir   string
	profilesDir string
}

// creates config and profiles directories when missing.
func New() (*ProfileManager, error) {
	configDir, err := getConfigDir()
	if err != nil {
		return nil, err
	}
	profilesDir := filepath.Join(configDir, "profiles")
	if err := os.MkdirAll(configDir, 0755); err != nil {
		return nil, fmt.Errorf("failed to create config directory %s: %w", configDir, err)
	}
	if err := os.MkdirAll(profilesDir, 0755); err != nil {
		return nil, fmt.Errorf("failed to create profiles directory %s: %w", profilesDir, err)
	}
	return &ProfileManager{configDir: configDir, profilesDir: profilesDir}, nil
}

// loads config.json or profiles/<name>.json when profile is non-empty.
func (pm *ProfileManager) LoadConfig(profile *string) (*Config, error) {
	var configPath string
	if profile != nil && *profile != "" {
		configPath = filepath.Join(pm.profilesDir, *profile+".json")
		if _, err := os.Stat(configPath); err != nil {
			if os.IsNotExist(err) {
				return nil, fmt.Errorf("profile %q does not exist (use --profile-list to see available profiles)", *profile)
			}
			return nil, err
		}
	} else {
		configPath = filepath.Join(pm.configDir, "config.json")
	}
	return loadConfigFile(configPath)
}

func loadConfigFile(path string) (*Config, error) {
	data, err := os.ReadFile(path)
	if err != nil {
		if os.IsNotExist(err) {
			return &Config{}, nil
		}
		return nil, fmt.Errorf("failed to read config file %s: %w", path, err)
	}
	var c Config
	if err := json.Unmarshal(data, &c); err != nil {
		return nil, fmt.Errorf("failed to parse config file %s: %w", path, err)
	}
	return &c, nil
}

// writes using the same path rules as LoadConfig.
func (pm *ProfileManager) SaveConfig(c *Config, profile *string) error {
	var configPath string
	if profile != nil && *profile != "" {
		configPath = filepath.Join(pm.profilesDir, *profile+".json")
	} else {
		configPath = filepath.Join(pm.configDir, "config.json")
	}
	data, err := json.MarshalIndent(c, "", "  ")
	if err != nil {
		return fmt.Errorf("failed to serialize config: %w", err)
	}
	if err := os.WriteFile(configPath, data, 0644); err != nil {
		return fmt.Errorf("failed to write config file %s: %w", configPath, err)
	}
	return nil
}

// when useLoader is true, dll is ignored.
func (pm *ProfileManager) CreateProfile(name string, appID, exe, dll *string, useLoader *bool) error {
	name = strings.TrimSpace(name)
	if name == "" {
		return fmt.Errorf("profile name cannot be empty")
	}
	if appID == nil || *appID == "" {
		return fmt.Errorf("appid is required when creating a profile")
	}
	if exe == nil || *exe == "" {
		return fmt.Errorf("exe is required when creating a profile")
	}
	profilePath := filepath.Join(pm.profilesDir, name+".json")
	if _, err := os.Stat(profilePath); err == nil {
		return fmt.Errorf("profile %q already exists", name)
	}
	usingLoader := useLoader != nil && *useLoader
	if !usingLoader && (dll == nil || *dll == "") {
		return fmt.Errorf("dll is required when not using embedded loader")
	}
	c := &Config{
		AppID:     strPtr(*appID),
		TargetExe: strPtr(*exe),
	}
	if useLoader != nil {
		c.UseLoader = useLoader
	}
	if usingLoader {
		c.DLLPath = nil
	} else {
		c.DLLPath = strPtr(*dll)
	}
	return pm.SaveConfig(c, &name)
}

// returns sorted profile names (filename without .json).
func (pm *ProfileManager) ListProfiles() ([]string, error) {
	entries, err := os.ReadDir(pm.profilesDir)
	if err != nil {
		return nil, fmt.Errorf("failed to read profiles directory %s: %w", pm.profilesDir, err)
	}
	var names []string
	for _, e := range entries {
		if e.IsDir() {
			continue
		}
		if filepath.Ext(e.Name()) == ".json" {
			names = append(names, e.Name()[:len(e.Name())-5])
		}
	}
	sort.Strings(names)
	return names, nil
}

// pairs each profile name with its config (for --profile-list).
func (pm *ProfileManager) ListProfilesWithConfig() ([]struct {
	Name   string
	Config *Config
}, error) {
	names, err := pm.ListProfiles()
	if err != nil {
		return nil, err
	}
	var out []struct {
		Name   string
		Config *Config
	}
	for _, name := range names {
		c, err := pm.LoadConfig(&name)
		if err != nil {
			continue
		}
		out = append(out, struct {
			Name   string
			Config *Config
		}{Name: name, Config: c})
	}
	return out, nil
}

// removes profiles/<name>.json.
func (pm *ProfileManager) DeleteProfile(name string) error {
	profilePath := filepath.Join(pm.profilesDir, name+".json")
	if _, err := os.Stat(profilePath); err != nil {
		if os.IsNotExist(err) {
			return fmt.Errorf("profile %q does not exist", name)
		}
		return err
	}
	if err := os.Remove(profilePath); err != nil {
		return fmt.Errorf("failed to delete profile: %w", err)
	}
	return nil
}

// directory containing config.json and the profiles/ subdirectory.
func (pm *ProfileManager) GetConfigDir() string {
	return pm.configDir
}

func getConfigDir() (string, error) {
	if xdg := os.Getenv("XDG_CONFIG_HOME"); xdg != "" {
		return filepath.Join(xdg, "proton-inject"), nil
	}
	home, err := os.UserHomeDir()
	if err != nil {
		return "", fmt.Errorf("failed to get home directory: %w", err)
	}
	return filepath.Join(home, ".config", "proton-inject"), nil
}

func strPtr(s string) *string {
	return &s
}
