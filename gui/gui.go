package gui

import (
	"fmt"
	"os"
	"os/exec"
	"path/filepath"
	"strings"
	"sync"
	"time"

	"fyne.io/fyne/v2"
	"fyne.io/fyne/v2/app"
	"fyne.io/fyne/v2/container"
	"fyne.io/fyne/v2/dialog"
	"fyne.io/fyne/v2/widget"
	"github.com/ncruces/zenity"
	"github.com/Jilwer/Proton-Inject/config"
	"github.com/Jilwer/Proton-Inject/embedded/injector"
	"github.com/Jilwer/Proton-Inject/utils"
)

var Version = "0.1.0"

func Run() {
	a := app.New()
	w := a.NewWindow("Proton-Inject")
	w.Resize(fyne.NewSize(640, 600))
	w.CenterOnScreen()

	s := newApp(w)
	w.SetContent(s.buildUI())
	w.ShowAndRun()
}

type appState struct {
	window fyne.Window

	appIDEntry    *widget.Entry
	exeEntry      *widget.Entry
	dllEntry      *widget.Entry
	useLoader     *widget.Check
	profileSelect *widget.Select
	injectButton  *widget.Button

	newProfileName *widget.Entry
	newAppID       *widget.Entry
	newExe         *widget.Entry
	newDll         *widget.Entry
	profileList    *widget.List

	logText     *widget.Entry
	statusLabel *widget.Label

	loaderMods      []string
	loaderList      *widget.List
	loaderPathLabel *widget.Label

	profiles          []string
	selectedProfile   string
	logs              []string
	logsMu            sync.Mutex
	logLen            int
	isInjecting       bool
	syncDllInputState func()
}

func newApp(w fyne.Window) *appState {
	s := &appState{
		window:      w,
		statusLabel: widget.NewLabel("Ready"),
	}

	s.appIDEntry = widget.NewEntry()
	s.appIDEntry.SetPlaceHolder("e.g. 123456")
	s.exeEntry = widget.NewEntry()
	s.exeEntry.SetPlaceHolder("e.g. MyGame.exe")
	s.dllEntry = widget.NewEntry()
	s.dllEntry.SetPlaceHolder("e.g. ~/mods/hook.dll")
	s.useLoader = widget.NewCheck("Use embedded loader", nil)
	s.useLoader.SetChecked(true)
	s.profileSelect = widget.NewSelect([]string{"None"}, nil)
	s.profileSelect.SetSelected("None")

	s.newProfileName = widget.NewEntry()
	s.newProfileName.SetPlaceHolder("e.g. MyGame")
	s.newAppID = widget.NewEntry()
	s.newAppID.SetPlaceHolder("e.g. 123456")
	s.newExe = widget.NewEntry()
	s.newExe.SetPlaceHolder("e.g. MyGame.exe")
	s.newDll = widget.NewEntry()
	s.newDll.SetPlaceHolder("Leave empty for embedded loader")

	s.logText = widget.NewMultiLineEntry()
	s.logText.Disable()

	utils.LogFunc = s.appendLog
	s.refreshProfiles()
	return s
}

func pickFile(title string, filters ...zenity.FileFilter) string {
	path, err := zenity.SelectFile(
		zenity.Title(title),
		zenity.FileFilters(filters),
	)
	if err != nil {
		return ""
	}
	return path
}

func (s *appState) appendLog(msg string) {
	s.logsMu.Lock()
	timestamp := time.Now().Format("15:04:05")
	formattedMsg := "[" + timestamp + "] " + msg
	s.logs = append(s.logs, formattedMsg)
	if len(s.logs) > 1000 {
		s.logs = s.logs[len(s.logs)-1000:]
	}
	s.logsMu.Unlock()
}

func (s *appState) startLogPoller() {
	go func() {
		for {
			time.Sleep(500 * time.Millisecond)
			s.logsMu.Lock()
			n := len(s.logs)
			if n != s.logLen {
				s.logLen = n
				text := strings.Join(s.logs, "\n")
				s.logsMu.Unlock()
				fyne.Do(func() {
					s.logText.SetText(text)
					s.logText.CursorRow = n - 1
				})
			} else {
				s.logsMu.Unlock()
			}
		}
	}()
}

func (s *appState) setStatus(msg string) {
	s.statusLabel.SetText(msg)
}

func (s *appState) setError(err error) {
	s.setStatus(fmt.Sprintf("Error: %v", err))
}

func (s *appState) setErrorf(format string, args ...interface{}) {
	s.setStatus(fmt.Sprintf("Error: "+format, args...))
}

func (s *appState) refreshProfiles() {
	pm, err := config.New()
	if err != nil {
		s.setError(err)
		return
	}
	list, err := pm.ListProfiles()
	if err != nil {
		s.setErrorf("loading profiles: %v", err)
		return
	}
	s.profiles = list
	opts := append([]string{"None"}, list...)
	s.profileSelect.Options = opts
	s.profileSelect.Refresh()
	if s.profileList != nil {
		s.profileList.Refresh()
	}
}

func (s *appState) loadProfile(name string) {
	if name == "" || name == "None" {
		s.selectedProfile = ""
		return
	}
	pm, err := config.New()
	if err != nil {
		s.setError(err)
		return
	}
	cfg, err := pm.LoadConfig(&name)
	if err != nil {
		s.setErrorf("loading profile: %v", err)
		return
	}
	s.appIDEntry.SetText(deref(cfg.AppID))
	s.exeEntry.SetText(deref(cfg.TargetExe))
	useLoader := cfg.UseLoaderOrDefault()
	s.useLoader.SetChecked(useLoader)
	if useLoader {
		s.dllEntry.SetText("")
	} else {
		s.dllEntry.SetText(deref(cfg.DLLPath))
	}
	if s.syncDllInputState != nil {
		s.syncDllInputState()
	}
	s.selectedProfile = name
	s.setStatus("Loaded profile: " + name)
}

func (s *appState) saveCurrentAsProfile() {
	appID := s.appIDEntry.Text
	exe := s.exeEntry.Text
	dll := strings.TrimSpace(s.dllEntry.Text)
	if appID == "" || exe == "" {
		s.setStatus("Fill in AppID and EXE before saving")
		return
	}
	if !s.useLoader.Checked && dll == "" {
		s.setStatus("DLL path is required when not using embedded loader")
		return
	}

	nameEntry := widget.NewEntry()
	nameEntry.SetPlaceHolder("e.g. MyGame")
	d := dialog.NewForm("Save as profile", "Save", "Cancel",
		[]*widget.FormItem{
			widget.NewFormItem("Profile name", nameEntry),
		},
		func(ok bool) {
			if !ok {
				return
			}
			name := strings.TrimSpace(nameEntry.Text)
			if name == "" {
				s.setStatus("Profile name cannot be empty")
				return
			}
			pm, err := config.New()
			if err != nil {
				s.setError(err)
				return
			}
			useLoader := dll == ""
			if err := pm.CreateProfile(name, &appID, &exe, &dll, &useLoader); err != nil {
				s.setError(err)
				return
			}
			s.setStatus("Saved profile: " + name)
			s.refreshProfiles()
			s.profileSelect.SetSelected(name)
		},
		s.window,
	)
	d.Resize(fyne.NewSize(400, 160))
	d.Show()
}

func (s *appState) createProfile() {
	name := strings.TrimSpace(s.newProfileName.Text)
	appID := s.newAppID.Text
	exe := s.newExe.Text
	dll := strings.TrimSpace(s.newDll.Text)
	useLoader := dll == ""

	if name == "" {
		s.setStatus("Profile name cannot be empty")
		return
	}
	if appID == "" || exe == "" {
		s.setStatus("AppID and EXE are required")
		return
	}
	if !useLoader && dll == "" {
		s.setStatus("Either check use loader or provide a DLL path")
		return
	}
	pm, err := config.New()
	if err != nil {
		s.setError(err)
		return
	}
	if err := pm.CreateProfile(name, &appID, &exe, &dll, &useLoader); err != nil {
		s.setError(err)
		return
	}
	s.newProfileName.SetText("")
	s.newAppID.SetText("")
	s.newExe.SetText("")
	s.newDll.SetText("")
	s.setStatus("Profile '" + name + "' created")
	s.refreshProfiles()
}

func (s *appState) deleteProfile(name string) {
	pm, err := config.New()
	if err != nil {
		s.setError(err)
		return
	}
	if err := pm.DeleteProfile(name); err != nil {
		s.setError(err)
		return
	}
	s.setStatus("Deleted profile: " + name)
	s.refreshProfiles()
	if s.selectedProfile == name {
		s.selectedProfile = ""
		s.profileSelect.SetSelected("None")
	}
}

func (s *appState) startInjection() {
	appID := s.appIDEntry.Text
	exe := s.exeEntry.Text
	dll := strings.TrimSpace(s.dllEntry.Text)
	useLoader := s.useLoader.Checked
	if appID == "" || exe == "" {
		s.setStatus("AppID and EXE are required")
		return
	}
	if !useLoader {
		if dll == "" {
			s.setStatus("DLL path is required when not using embedded loader")
			return
		}
		expanded := utils.ExpandPath(dll)
		if _, err := os.Stat(expanded); err != nil {
			s.setStatus("DLL not found at: " + expanded)
			return
		}
	}
	var expanded string
	if dll != "" {
		expanded = utils.ExpandPath(dll)
	}
	s.isInjecting = true
	s.injectButton.Disable()
	s.setStatus("Injecting...")

	go func() {
		var err error
		defer func() {
			fyne.Do(func() {
				s.isInjecting = false
				s.injectButton.Enable()
				if err != nil {
					s.setStatus("Injection failed: " + err.Error())
				} else {
					s.setStatus("Injection successful!")
				}
			})
		}()
		mgr, e := injector.New()
		if e != nil {
			err = e
			return
		}
		err = mgr.Inject(appID, exe, expanded, useLoader)
	}()
}

func (s *appState) buildUI() fyne.CanvasObject {
	injectTab := s.buildInjectTab()
	profilesTab := s.buildProfilesTab()
	loaderTab := s.buildLoaderTab()
	logsTab := s.buildLogsTab()

	profilesItem := container.NewTabItem("Profiles", profilesTab)
	loaderItem := container.NewTabItem("Loader", loaderTab)
	tabs := container.NewAppTabs(
		container.NewTabItem("Inject", injectTab),
		profilesItem,
		loaderItem,
		container.NewTabItem("Logs", logsTab),
	)
	tabs.OnSelected = func(t *container.TabItem) {
		if t == profilesItem {
			s.refreshProfiles()
		}
		if t == loaderItem {
			s.refreshLoaderMods()
		}
	}

	s.profileSelect.OnChanged = func(sel string) {
		if sel == "None" {
			s.selectedProfile = ""
			return
		}
		s.loadProfile(sel)
	}

	s.startLogPoller()

	s.statusLabel.Wrapping = fyne.TextWrapWord
	return container.NewBorder(nil, container.NewPadded(s.statusLabel), nil, nil, container.NewPadded(tabs))
}

func (s *appState) buildInjectTab() fyne.CanvasObject {
	profileCard := widget.NewCard("Profile", "Load a saved profile to fill the fields below.",
		container.NewPadded(s.profileSelect),
	)

	exeBrowse := widget.NewButton("Browse", func() {
		go func() {
			path := pickFile("Select game executable", zenity.FileFilter{Name: "Executables", Patterns: []string{"*.exe"}})
			if path != "" {
				fyne.Do(func() { s.exeEntry.SetText(path) })
			}
		}()
	})
	dllBrowse := widget.NewButton("Browse", func() {
		go func() {
			path := pickFile("Select DLL to inject", zenity.FileFilter{Name: "DLL files", Patterns: []string{"*.dll"}})
			if path != "" {
				fyne.Do(func() { s.dllEntry.SetText(path) })
			}
		}()
	})

	setDllInputEnabled := func(enabled bool) {
		if enabled {
			s.dllEntry.Enable()
			dllBrowse.Enable()
		} else {
			s.dllEntry.Disable()
			dllBrowse.Disable()
		}
	}
	s.useLoader.OnChanged = func(checked bool) {
		setDllInputEnabled(!checked)
	}
	s.syncDllInputState = func() { setDllInputEnabled(!s.useLoader.Checked) }
	s.syncDllInputState()

	settingsForm := widget.NewForm(
		widget.NewFormItem("Steam AppID", s.appIDEntry),
		widget.NewFormItem("Game .exe", container.NewBorder(nil, nil, nil, exeBrowse, s.exeEntry)),
		widget.NewFormItem("DLL path", container.NewBorder(nil, nil, nil, dllBrowse, s.dllEntry)),
		widget.NewFormItem("", s.useLoader),
	)
	settingsForm.SubmitText = ""
	settingsForm.CancelText = ""

	settingsCard := widget.NewCard("Injection settings", "Configure your injection settings.",
		container.NewPadded(settingsForm),
	)

	saveProfileBtn := widget.NewButton("Save as profile...", func() { s.saveCurrentAsProfile() })

	s.injectButton = widget.NewButton("Inject DLL", func() { s.startInjection() })
	s.injectButton.Importance = widget.HighImportance

	return container.NewScroll(container.NewPadded(container.NewVBox(
		profileCard,
		settingsCard,
		saveProfileBtn,
		s.injectButton,
	)))
}

func (s *appState) buildProfilesTab() fyne.CanvasObject {
	s.profileList = widget.NewList(
		func() int { return len(s.profiles) },
		func() fyne.CanvasObject {
			nameLabel := widget.NewLabel("profile-name-placeholder")
			loadBtn := widget.NewButton("Load", nil)
			deleteBtn := widget.NewButton("Delete", nil)
			return container.NewBorder(nil, nil, nameLabel, container.NewHBox(loadBtn, deleteBtn))
		},
		func(id widget.ListItemID, o fyne.CanvasObject) {
			row := o.(*fyne.Container)
			name := s.profiles[id]
			row.Objects[0].(*widget.Label).SetText(name)
			btns := row.Objects[1].(*fyne.Container)
			btns.Objects[0].(*widget.Button).OnTapped = func() { s.loadProfile(name) }
			btns.Objects[1].(*widget.Button).OnTapped = func() { s.deleteProfile(name) }
		},
	)

	refreshBtn := widget.NewButton("Refresh", func() { s.refreshProfiles() })
	openConfigBtn := widget.NewButton("Open config directory", func() {
		pm, err := config.New()
		if err != nil {
			s.setStatus("Failed to get config path: " + err.Error())
			return
		}
		dir := pm.GetConfigDir()
		if err := exec.Command("xdg-open", dir).Start(); err != nil {
			s.setStatus("Failed to open: " + err.Error())
			return
		}
		s.setStatus("Opened " + dir)
	})
	header := container.NewBorder(nil, nil, nil, container.NewHBox(openConfigBtn, refreshBtn))
	listSection := container.NewBorder(header, nil, nil, nil, s.profileList)
	listCard := widget.NewCard("Saved profiles", "Load or delete existing profiles. Select one and click Load to use it on the Inject tab.",
		container.NewPadded(listSection),
	)

	exeBrowse := widget.NewButton("Browse", func() {
		go func() {
			path := pickFile("Select game executable", zenity.FileFilter{Name: "Executables", Patterns: []string{"*.exe"}})
			if path != "" {
				fyne.Do(func() { s.newExe.SetText(path) })
			}
		}()
	})
	dllBrowse := widget.NewButton("Browse", func() {
		go func() {
			path := pickFile("Select DLL", zenity.FileFilter{Name: "DLL files", Patterns: []string{"*.dll"}})
			if path != "" {
				fyne.Do(func() { s.newDll.SetText(path) })
			}
		}()
	})

	createForm := widget.NewForm(
		widget.NewFormItem("Profile name", s.newProfileName),
		widget.NewFormItem("Steam AppID", s.newAppID),
		widget.NewFormItem("Game .exe", container.NewBorder(nil, nil, nil, exeBrowse, s.newExe)),
		widget.NewFormItem("DLL path", container.NewBorder(nil, nil, nil, dllBrowse, s.newDll)),
	)
	createForm.SubmitText = ""
	createForm.CancelText = ""

	createBtn := widget.NewButton("Create profile", func() { s.createProfile() })
	createCard := widget.NewCard("Create new profile", "Save a new named profile.",
		container.NewPadded(container.NewVBox(createForm, createBtn)),
	)

	split := container.NewVSplit(
		container.NewPadded(listCard),
		container.NewScroll(container.NewPadded(createCard)),
	)
	split.SetOffset(0.5)
	return split
}

func (s *appState) buildLogsTab() fyne.CanvasObject {
	clearBtn := widget.NewButton("Clear", func() {
		s.logsMu.Lock()
		s.logs = nil
		s.logLen = 0
		s.logsMu.Unlock()
		s.logText.SetText("")
	})

	toolbar := container.NewHBox(clearBtn)
	return container.NewBorder(container.NewPadded(toolbar), nil, nil, nil, container.NewPadded(s.logText))
}

func (s *appState) buildLoaderTab() fyne.CanvasObject {
	info := widget.NewLabel(
		"The embedded loader (loader.dll) runs inside the game and loads all .dll files from " +
			"Documents/proton-inject-mods in the game's Proton prefix. Drop mod DLLs there; they are " +
			"loaded when the game starts and the folder is watched for new, changed, or removed DLLs.")
	info.Wrapping = fyne.TextWrapWord

	s.loaderPathLabel = widget.NewLabel("Set AppID in the Inject tab, then click Refresh.")
	s.loaderPathLabel.Wrapping = fyne.TextWrapWord

	s.loaderList = widget.NewList(
		func() int { return len(s.loaderMods) },
		func() fyne.CanvasObject { return widget.NewLabel("mod.dll") },
		func(id widget.ListItemID, o fyne.CanvasObject) {
			if id < len(s.loaderMods) {
				o.(*widget.Label).SetText(s.loaderMods[id])
			}
		},
	)

	openBtn := widget.NewButton("Open mods directory", func() {
		appID := strings.TrimSpace(s.appIDEntry.Text)
		if appID == "" {
			s.setStatus("Set AppID in the Inject tab first")
			return
		}
		modsDir := utils.ModsDirForAppID(appID)
		if modsDir == "" {
			s.setStatus("No mods directory found yet. Inject the loader once (Inject tab, use embedded loader), then click Refresh.")
			return
		}
		if err := exec.Command("xdg-open", modsDir).Start(); err != nil {
			s.setStatus("Failed to open: " + err.Error())
			return
		}
		s.setStatus("Opened " + modsDir)
		s.refreshLoaderMods()
	})

	refreshBtn := widget.NewButton("Refresh", func() { s.refreshLoaderMods() })

	pathCard := widget.NewCard("Mods directory", "Scanned from all Steam libraries for this AppID. If none is found, inject the loader once so it creates the folder.",
		container.NewPadded(container.NewVBox(s.loaderPathLabel, container.NewHBox(openBtn, refreshBtn))))
	modsCard := widget.NewCard("DLL files in mods directory", "Mods the loader will load (or has loaded) from the folder above.",
		container.NewPadded(s.loaderList))

	return container.NewScroll(container.NewPadded(container.NewVBox(
		info,
		pathCard,
		modsCard,
	)))
}

func (s *appState) refreshLoaderMods() {
	appID := strings.TrimSpace(s.appIDEntry.Text)
	if appID == "" {
		s.loaderPathLabel.SetText("Set AppID in the Inject tab, then click Refresh.")
		s.loaderMods = nil
		if s.loaderList != nil {
			s.loaderList.Refresh()
		}
		return
	}
	modsDir := utils.ModsDirForAppID(appID)
	if modsDir == "" {
		s.loaderPathLabel.SetText("No proton-inject-mods folder found for this AppID.\n\nInject the loader once (Inject tab: use embedded loader, then run the game and inject). The loader creates the folder. Then click Refresh.")
		s.loaderMods = nil
	} else {
		s.loaderPathLabel.SetText(modsDir)
		s.loaderMods = nil
		entries, err := os.ReadDir(modsDir)
		if err == nil {
			for _, e := range entries {
				if !e.IsDir() && strings.EqualFold(filepath.Ext(e.Name()), ".dll") {
					s.loaderMods = append(s.loaderMods, e.Name())
				}
			}
		}
	}
	if s.loaderList != nil {
		s.loaderList.Refresh()
	}
}

func deref(s *string) string {
	if s == nil {
		return ""
	}
	return *s
}
