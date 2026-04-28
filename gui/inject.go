package gui

import (
	"os"
	"strings"

	"fyne.io/fyne/v2"
	"fyne.io/fyne/v2/container"
	"fyne.io/fyne/v2/widget"
	"github.com/Jilwer/Proton-Inject/embedded/injector"
	"github.com/Jilwer/Proton-Inject/utils"
	"github.com/ncruces/zenity"
)

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
