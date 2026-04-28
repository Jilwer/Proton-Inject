package gui

import (
	"os/exec"
	"strings"

	"fyne.io/fyne/v2"
	"fyne.io/fyne/v2/container"
	"fyne.io/fyne/v2/dialog"
	"fyne.io/fyne/v2/widget"
	"github.com/Jilwer/Proton-Inject/config"
	"github.com/ncruces/zenity"
)

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

func deref(s *string) string {
	if s == nil {
		return ""
	}
	return *s
}
