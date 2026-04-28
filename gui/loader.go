package gui

import (
	"os"
	"os/exec"
	"path/filepath"
	"strings"

	"fyne.io/fyne/v2"
	"fyne.io/fyne/v2/container"
	"fyne.io/fyne/v2/widget"
	"github.com/Jilwer/Proton-Inject/utils"
)

func (s *appState) buildLoaderTab() fyne.CanvasObject {


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

	pathCardDesc := widget.NewLabel("Scanned from all Steam libraries for this AppID. If none is found, inject the loader once so it creates the folder.")
	pathCardDesc.Wrapping = fyne.TextWrapWord
	pathCard := widget.NewCard("Mods directory", "",
		container.NewPadded(container.NewVBox(pathCardDesc, s.loaderPathLabel, container.NewHBox(openBtn, refreshBtn))))
	modsCard := widget.NewCard("DLL files in mods directory", "Mods the loader will load (or has loaded) from the folder above.",
		container.NewPadded(s.loaderList))

	return container.NewScroll(container.NewPadded(container.NewVBox(
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
