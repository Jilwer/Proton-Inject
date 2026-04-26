package cli

import (
	"flag"
	"fmt"
	"os"
	"os/exec"

	"github.com/proton-inject-go/config"
	"github.com/proton-inject-go/embedded/injector"
	"github.com/proton-inject-go/utils"
)

func Run() error {
	flag.Usage = printHelp
	appID := flag.String("appid", "", "Steam AppID")
	exe := flag.String("exe", "", "Target game executable name")
	dll := flag.String("dll", "", "Path to DLL to inject")
	profile := flag.String("profile", "", "Load configuration from profile")
	profileNew := flag.String("profile-new", "", "Create new profile with current configuration")
	profileList := flag.Bool("profile-list", false, "List all available profiles")
	loader := flag.Bool("loader", false, "Use embedded loader (when no --dll); if unset, profile/config use_loader is used")
	flag.Parse()

	var loaderFlagSet bool
	flag.Visit(func(f *flag.Flag) {
		if f.Name == "loader" {
			loaderFlagSet = true
		}
	})

	if *profileList {
		pm, err := config.New()
		if err != nil {
			return err
		}
		listProfiles(pm)
		return nil
	}

	if *profileNew != "" {
		pm, err := config.New()
		if err != nil {
			return err
		}
		useLoader := *dll == ""
		err = pm.CreateProfile(*profileNew, appID, exe, dll, &useLoader)
		if err != nil {
			return err
		}
		fmt.Printf("Profile %q created successfully\n", *profileNew)
		return nil
	}

	return run(appID, exe, dll, profile, loader, loaderFlagSet)
}

func run(appID, exe, dll, profile *string, loader *bool, loaderFlagSet bool) error {
	if _, err := exec.LookPath("protontricks-launch"); err != nil {
		return fmt.Errorf("protontricks-launch not found in PATH: install protontricks and ensure it is available")
	}

	pm, err := config.New()
	if err != nil {
		return err
	}

	var profileName *string
	if profile != nil && *profile != "" {
		profileName = profile
	}
	cfg, err := pm.LoadConfig(profileName)
	if err != nil {
		return err
	}

	if appID != nil && *appID != "" {
		cfg.AppID = appID
	}
	if exe != nil && *exe != "" {
		cfg.TargetExe = exe
	}
	if dll != nil && *dll != "" {
		cfg.DLLPath = dll
	}

	var aid, exeName, dllPath string
	if cfg.AppID != nil {
		aid = *cfg.AppID
	}
	if cfg.TargetExe != nil {
		exeName = *cfg.TargetExe
	}
	if cfg.DLLPath != nil && *cfg.DLLPath != "" {
		dllPath = *cfg.DLLPath
	}

	if aid == "" {
		return fmt.Errorf("appid is required (use --appid or set in config)")
	}
	if exeName == "" {
		return fmt.Errorf("exe is required (use --exe or set in config)")
	}

	var useLoader bool
	if dllPath != "" {
		useLoader = false
	} else {
		if loaderFlagSet {
			useLoader = *loader
			if !useLoader {
				return fmt.Errorf("no DLL specified: provide --dll <path> or pass --loader to use the embedded loader")
			}
			fmt.Fprintln(os.Stderr, "Using embedded loader (mods from Documents/proton-inject-mods)")
		} else {
			useLoader = cfg.UseLoaderOrDefault()
			if !useLoader {
				return fmt.Errorf("no DLL specified and profile/config has use_loader=false: provide --dll <path> or pass --loader to use the embedded loader")
			}
			fmt.Fprintln(os.Stderr, "Using embedded loader (from profile/config)")
		}
	}

	var expandedDll string
	if dllPath != "" {
		expandedDll = utils.ExpandPath(dllPath)
		if !useLoader {
			if info, err := os.Stat(expandedDll); err != nil || info.IsDir() {
				return fmt.Errorf("dll not found at %s", expandedDll)
			}
		}
	}

	mgr, err := injector.New()
	if err != nil {
		return err
	}

	if useLoader {
		cfg.DLLPath = nil
	}

	if err := pm.SaveConfig(cfg, profileName); err != nil {
		return err
	}

	return mgr.Inject(aid, exeName, expandedDll, useLoader)
}

func listProfiles(pm *config.ProfileManager) {
	list, err := pm.ListProfilesWithConfig()
	if err != nil {
		fmt.Fprintln(os.Stderr, "error:", err)
		os.Exit(1)
	}
	if len(list) == 0 {
		fmt.Println("No profiles found.")
		return
	}
	fmt.Printf("Available profiles (%d):\n", len(list))
	for _, p := range list {
		appid := "N/A"
		exe := "N/A"
		if p.Config.AppID != nil {
			appid = *p.Config.AppID
		}
		if p.Config.TargetExe != nil {
			exe = *p.Config.TargetExe
		}
		fmt.Printf("  %s (AppID: %s, Exe: %s)\n", p.Name, appid, exe)
	}
}

func printHelp() {
	fmt.Println("proton-inject - DLL injection for Proton games via protontricks-launch")
	fmt.Println()
	fmt.Println("usage:")
	fmt.Println("  proton-inject [OPTIONS]")
	fmt.Println()
	fmt.Println("OPTIONS:")
	fmt.Println("  --appid <APPID>           Steam AppID")
	fmt.Println("  --exe <EXE>               Target game executable name")
	fmt.Println("  --dll <DLL>               Path to DLL to inject (if both --dll and --loader are given, --dll wins)")
	fmt.Println("  --loader                  Use embedded loader (when no --dll); if omitted, profile/config use_loader is used")
	fmt.Println("  --profile <PROFILE>       Load configuration from profile")
	fmt.Println("  --profile-new <NAME>      Create new profile with current configuration")
	fmt.Println("  --profile-list             List all available profiles")
	fmt.Println("  -h, --help                Show help")
	fmt.Println()
	fmt.Println("examples:")
	fmt.Println("  # Embedded loader (mods from Documents/proton-inject-mods):")
	fmt.Println("  proton-inject --appid 123456 --exe MyGame.exe --loader")
	fmt.Println("  proton-inject --appid 123456 --exe MyGame.exe --profile mygame   # use_loader from profile")
	fmt.Println()
	fmt.Println("  # Custom DLL (inject that file directly):")
	fmt.Println("  proton-inject --appid 123456 --exe MyGame.exe --dll ~/mods/hook.dll")
	fmt.Println()
	fmt.Println("  # Both --dll and --loader: --dll wins, custom DLL is injected")
	fmt.Println("  proton-inject --appid 123456 --exe MyGame.exe --dll ~/mods/hook.dll --loader")
	fmt.Println()
	fmt.Println("  proton-inject --profile-list")
}
